#include "pch.hpp"

#include <stop_token>
#include <thread>
#include <utility>

#include "keyboardManager.hpp"
#include "utils/utils.hpp"

#include "settings/config.hpp"
#include "settings/action_registry.hpp"

// Encode key state into VK code
static bool g_superDown = false;
constexpr UINT KEY_DOWN_FLAG = 0x8000u;

static __forceinline constexpr UINT EncodeKey(UINT vkCode, WPARAM wParam) noexcept {
    return (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? (vkCode | KEY_DOWN_FLAG) : vkCode;
}
static inline constexpr UINT DecodeKey(UINT encodedKey) noexcept {
    return encodedKey & ~KEY_DOWN_FLAG;
}
static inline constexpr bool IsKeyDown(UINT encodedKey) noexcept {
    return (encodedKey & KEY_DOWN_FLAG) != 0;
}

namespace km {
KeyboardManager::KeyboardManager(Config* cfg) : config(cfg) {
    instance = this;

    inputThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();
        (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        InputLoop(st);
        CoUninitialize();
    });

    hookThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();
        HookLoop(st);
    });
}

KeyboardManager::~KeyboardManager() {
    inputThread.request_stop();
    hookThread.request_stop();

    PostThreadMessage(hookThreadId, WM_NULL, 0, 0);
    cv.notify_one();
    dispatcher::IPCMessage({0xBEEF00FF, L"PCSTATUS_REFRESH_MSG", L"D2DOverlayStatusWnd"});

    cv.notify_all();
    hookCv.notify_all();

    if (inputThread.joinable())
        inputThread.join();
    if (hookThread.joinable())
        hookThread.join();
}

void KeyboardManager::SeedModifierStates() noexcept {
    static constexpr UINT modifiers[] = {VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU};
    for (UINT vk : modifiers) {
        if (GetAsyncKeyState(vk) & 0x8000)
            SetKey(vk);
        else
            ClearKey(vk);
    }
}

void KeyboardManager::SetSuperReleasedCallback(std::function<void()> cb) {
    superReleasedCallback = std::move(cb);
}

void KeyboardManager::SetSuperPressedCallback(std::function<void()> cb) {
    superPressedCallback = std::move(cb);
}

LRESULT CALLBACK KeyboardManager::HookProc(int code, WPARAM wParam, LPARAM lParam) noexcept {
    if (code != HC_ACTION || !instance || !lParam)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
    const UINT vk = kb->vkCode;
    const UINT superVk = instance->config->m_settings.SUPER;
    if ((kb->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    if (vk != superVk) {
        if (g_superDown) {
            instance->keyQueue.push(EncodeKey(vk, wParam));
            instance->cv.notify_one();
            return 1;
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    g_superDown = down;
    instance->keyQueue.push(EncodeKey(vk, wParam));
    instance->cv.notify_one();
    return 1;
}

void KeyboardManager::InputLoop(std::stop_token st) {
    SET_THREAD_NAME("KB INPUT");
    while (!st.stop_requested()) {
        std::unique_lock lock(cvMutex);
        cv.wait(lock, [&] { return st.stop_requested() || !keyQueue.empty(); });

        if (st.stop_requested())
            continue;

        while (!keyQueue.empty()) {
            UINT vk;
            if (keyQueue.pop(vk)) {
                ProcessKey(vk);
            }
        }
    }
}

void KeyboardManager::ProcessKey(UINT vk) {
    UINT decodedVK = DecodeKey(vk);

    if (IsKeyDown(vk)) {
        if (IsKeySet(decodedVK))
            return;

        if (decodedVK == config->m_settings.SUPER) {
            SeedModifierStates();
            if (superPressedCallback)
                superPressedCallback();
        }

        SetKey(decodedVK);
    } else {
        ClearKey(decodedVK);
        if (decodedVK == config->m_settings.SUPER && superReleasedCallback) {
            superReleasedCallback();
            ClearAllKeys();
            dispatcher::IPCMessage({0xBEEF00FF, L"PCSTATUS_REFRESH_MSG", L"D2DOverlayStatusWnd"});
        }
        return;
    }

    switch (decodedVK) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return;
    }

    LOG_E("Key event: {} {}", decodedVK, IsKeyDown(vk) ? "DOWN" : "UP");

    uint8_t modMask = 0;
    if (IsKeySet(VK_LSHIFT))
        modMask |= ModMask::LSHIFT;
    if (IsKeySet(VK_RSHIFT))
        modMask |= ModMask::RSHIFT;
    if (IsKeySet(VK_LCONTROL))
        modMask |= ModMask::LCTRL;
    if (IsKeySet(VK_RCONTROL))
        modMask |= ModMask::RCTRL;
    if (IsKeySet(VK_LMENU))
        modMask |= ModMask::LALT;
    if (IsKeySet(VK_RMENU))
        modMask |= ModMask::RALT;

    KeyEvent key{decodedVK, modMask};

    auto it = config->m_keybinds.find(key);
    if (it == config->m_keybinds.end())
        return;

    const auto& vec = it->second;
    for (uint8_t i = 0; i < vec.count; ++i)
        DispatchAction(vec.items[i], &config->m_settings);
}

void KeyboardManager::HookLoop(std::stop_token st) {
    hookThreadId = GetCurrentThreadId();
    SET_THREAD_NAME("KB HOOK");

    hookHandle = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, nullptr, 0);
    if (hookHandle) {
        ClearAllKeys();

        MSG msg;
        while (!st.stop_requested() && GetMessageW(&msg, nullptr, 0, 0) > 0) {}
        // no Translate/Dispatch needed for LL hook

        UnhookWindowsHookEx(hookHandle);
        hookHandle = nullptr;
    }
}
} // namespace km
