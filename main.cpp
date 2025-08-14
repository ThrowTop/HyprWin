#include "pch.hpp"

#include "utils.hpp"
#include "lockfreequeue.hpp"

#include "lib/tray/include/tray.hpp"
#include "keyboardManager.hpp"
#include "mouseManager.hpp"

#include "settings/config.hpp"

#include "resource.h"

#include "tinylog.hpp"

constexpr int NOT_ADMIN = 1;
constexpr int ALREADY_RUNNING = 2;
constexpr int CONFIG_ERROR = 3;
constexpr int MUTEX_ERROR = 4;

struct AppState {
    Config cfg;
    bool running = true;
    bool superReleased = false;
    std::mutex mtx;
    std::condition_variable cv;
};

static bool RegisterSuperKey(UINT vk) {
    if (vk == 0) return false;
    switch (vk) {
    case VK_LWIN: case VK_RWIN:
    case VK_SHIFT: case VK_CONTROL: case VK_MENU:
    case VK_LSHIFT: case VK_RSHIFT:
    case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU:
    return false;
    default: break;
    }

    constexpr int kId = 1;
    static UINT super_vk = 0;

    if (super_vk == vk) return true;

    UnregisterHotKey(nullptr, kId);

    const UINT mods = MOD_NOREPEAT;
    if (RegisterHotKey(nullptr, kId, mods, vk)) {
        super_vk = vk;
        return true;
    }
    return false;
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd
) {
    if (!utils::EnsureRunAsAdminAndExitIfNot()) return NOT_ADMIN;

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"HyprWindows");
    if (!mutex) return MUTEX_ERROR;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Another instance is already running.", L"Error", MB_OK | MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        return ALREADY_RUNNING;
    }

    tinylog::init({
        .console = true,
        .file_path = "hyprwin.log",
        .console_level = tinylog::Level::Debug,
        .file_level = tinylog::Level::Trace,
        .date_format = L"MM'-'dd",
        .time_format = L"HH':'mm':'ss"
        });

    SET_THREAD_NAME("Main");

    AppState state;
    if (!state.cfg.LoadConfig()) {
        MessageBoxW(nullptr, L"Config Fuarked SUPER is required", L"Error", MB_OK | MB_ICONERROR);
        return CONFIG_ERROR;
    }

    if (!RegisterSuperKey(state.cfg.m_settings.SUPER)) {
        MessageBoxW(nullptr, L"Failed to register super key.", L"Error", MB_OK | MB_ICONERROR);
        return CONFIG_ERROR;
    }

    DWORD mainThreadId = GetCurrentThreadId();
    std::jthread trayThread([&state, mainThreadId] {
        Tray::Tray tray("Shortcut Manager", (HICON)LoadImageW(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDI_HWICON),
            IMAGE_ICON,
            0, 0,
            LR_DEFAULTSIZE | LR_SHARED));

        SET_THREAD_NAME("tray");

        tray.addEntry(Tray::Button("Exit", [&] {
            {
                std::scoped_lock lock(state.mtx);
                state.running = false;
            }
            state.cv.notify_one();
            PostThreadMessage(mainThreadId, WM_NULL, 0, 0); // Notify main loop to exit so it doesnt keep waiting WM_HOTKEY
            PostQuitMessage(0);
        }));

        tray.addEntry(Tray::Button("Reload", [&] {
            Config newCfg;
            if (!newCfg.LoadConfig()) {
                MessageBoxW(nullptr, L"Config Fuarked", L"Error", MB_OK | MB_ICONERROR);
                return;
            }
            {
                std::scoped_lock lock(state.mtx);
                state.cfg = std::move(newCfg);
            }
            if (!RegisterSuperKey(state.cfg.m_settings.SUPER)) {
                MessageBoxW(nullptr, L"Failed to register super key after reload.", L"Error", MB_OK | MB_ICONERROR);
            }
        }));
        tray.addEntry(Tray::Separator());

        tray.addEntry(Tray::Button("Config Folder", [&] {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);

            wchar_t* lastSlash = wcsrchr(path, L'\\');
            if (lastSlash) {
                *lastSlash = L'\0';
            }

            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
        }));

        tray.run();
    });

    mm::MouseManager mm(hInstance, &state.cfg);
    km::KeyboardManager km(&state.cfg);

    km.SetSuperReleasedCallback([&] {
        {
            std::scoped_lock lock(state.mtx);
            state.superReleased = true;
        }
        state.cv.notify_one();
    });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        std::unique_lock lock(state.mtx);
        if (!state.running)
            break;

        if (msg.message == WM_HOTKEY && msg.wParam == 1) {
            km.InstallHook();
            mm.InstallHook();

            state.cv.wait(lock, [&] { return state.superReleased || !state.running; });
            state.superReleased = false;

            km.UninstallHook();
            mm.UninstallHook();
        }
    }

    if (mutex) CloseHandle(mutex);
    return EXIT_SUCCESS;
}