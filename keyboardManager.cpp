#include "pch.hpp"

#include "keyboardManager.hpp"
#include "utils.hpp"
#include "overlay.hpp"

#include "settings/config.hpp"
#include "settings/action_registry.hpp"

// Encode key state into VK code
constexpr UINT KEY_DOWN_FLAG = 0x8000u;

static __forceinline UINT EncodeKey(UINT vkCode, WPARAM wParam) {
    return (wParam == WM_KEYDOWN) ? (vkCode | KEY_DOWN_FLAG) : vkCode;
}

static inline UINT DecodeKey(UINT encodedKey) {
    return encodedKey & ~KEY_DOWN_FLAG;
}

static inline bool IsKeyDown(UINT encodedKey) {
    return (encodedKey & KEY_DOWN_FLAG) != 0;
}

namespace km {
    KeyboardManager::KeyboardManager(Config* cfg) : config(cfg) {
        instance = this;

        inputThread = std::jthread([this](std::stop_token st) {
            (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            InputLoop(st);
            CoUninitialize();
        });

        hookThread = std::jthread([this](std::stop_token st) {
            HookLoop(st);
        });
    }

    KeyboardManager::~KeyboardManager() {
        inputThread.request_stop();
        hookThread.request_stop();

        UninstallHook();

        cv.notify_all();
        hookCv.notify_all();

        if (inputThread.joinable()) inputThread.join();
        if (hookThread.joinable()) hookThread.join();
    }

    void KeyboardManager::InstallHook() {
        {
            std::scoped_lock lock(hookCvMutex);
            installHookRequested = true;
        }
        hookCv.notify_one();
    }

    void KeyboardManager::UninstallHook() {
        {
            std::scoped_lock lock(hookCvMutex);
            uninstallHookRequested = true;
        }
        hookCv.notify_one();

        // Unblock GetMessage so thread can rejoin
        PostThreadMessage(hookThreadId, WM_NULL, 0, 0);
        ClearAllKeys();
        cv.notify_one();
    }

    void KeyboardManager::SetSuperReleasedCallback(std::function<void()> cb) {
        superReleasedCallback = std::move(cb);
    }

    LRESULT CALLBACK KeyboardManager::HookProc(int code, WPARAM wParam, LPARAM lParam) {
        if (code >= 0 && instance && lParam) {
            const KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (kb->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, wParam, lParam);

            bool dwn = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

            instance->keyQueue.push((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? (kb->vkCode | KEY_DOWN_FLAG) : kb->vkCode);
            instance->cv.notify_one();

            if (dwn)
                return 1;
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void KeyboardManager::InputLoop(std::stop_token st) {
        SET_THREAD_NAME("KB INPUT");
        while (!st.stop_requested()) {
            std::unique_lock lock(cvMutex);
            cv.wait(lock, [&] {
                return st.stop_requested() || (hookHandle && !keyQueue.empty());
            });

            if (st.stop_requested() || !hookHandle)
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
            if (IsKeySet(decodedVK)) return;
            SetKey(decodedVK);
        }
        else {
            ClearKey(decodedVK);
            if (decodedVK == config->m_settings.SUPER && superReleasedCallback)
                superReleasedCallback();
            return;
        }

        switch (decodedVK) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        return;
        }

        uint8_t modMask = 0;
        if (IsKeySet(VK_LSHIFT)) modMask |= ModMask::LSHIFT;
        if (IsKeySet(VK_RSHIFT)) modMask |= ModMask::RSHIFT;
        if (IsKeySet(VK_LCONTROL)) modMask |= ModMask::LCTRL;
        if (IsKeySet(VK_RCONTROL)) modMask |= ModMask::RCTRL;
        if (IsKeySet(VK_LMENU)) modMask |= ModMask::LALT;
        if (IsKeySet(VK_RMENU)) modMask |= ModMask::RALT;

        KeyEvent key{ decodedVK, modMask };

        auto it = config->m_keybinds.find(key);
        if (it == config->m_keybinds.end()) return;

        const auto& vec = it->second;
        for (uint8_t i = 0; i < vec.count; ++i)
            DispatchAction(vec.items[i], &config->m_settings);
    }

    void KeyboardManager::HookLoop(std::stop_token st) {
        hookThreadId = GetCurrentThreadId();
        SET_THREAD_NAME("KB HOOK");

        while (!st.stop_requested()) {
            {
                std::unique_lock lock(hookCvMutex);
                hookCv.wait(lock, [&] {
                    return st.stop_requested() || installHookRequested;
                });

                if (st.stop_requested()) break;

                if (installHookRequested && !hookHandle) {
                    LOG_T("Installing keyboard hook...");
                    hookHandle = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, nullptr, 0);
                    if (hookHandle) {
                        installHookRequested = false;
                        uninstallHookRequested = false;

                        ClearAllKeys();

                        constexpr UINT modifierKeys[] = {
                            VK_LSHIFT, VK_RSHIFT,
                            VK_LCONTROL, VK_RCONTROL,
                            VK_LMENU, VK_RMENU
                        };

                        for (UINT vk : modifierKeys) {
                            if (GetAsyncKeyState(vk) & 0x8000)
                                SetKey(vk);
                        }
                    }
                }
            }

            if (!hookHandle) continue;

            MSG msg;
            while (!st.stop_requested() && !uninstallHookRequested) {
                BOOL result = GetMessage(&msg, nullptr, 0, 0);
                if (result <= 0) break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (hookHandle) {
                LOG_T("Uninstalling keyboard hook...");
                UnhookWindowsHookEx(hookHandle);
                hookHandle = nullptr;
            }

            uninstallHookRequested = false;
        }
    }
}