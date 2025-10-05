#include "pch.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "utils/utils.hpp"
#include "tray/tray.hpp"
#include "keyboardManager.hpp"
#include "mouseManager.hpp"
#include "settings/config.hpp"
#include "resource.h"
#include "tinylog.hpp"
#include "superhook.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <ShlObj.h> // Icon Definitions
#pragma comment(lib, "shell32.lib")

constexpr int NOT_ADMIN = 1;
constexpr int ALREADY_RUNNING = 2;
constexpr int CONFIG_ERROR = 3;
constexpr int MUTEX_ERROR = 4;
constexpr int SUPER_WATCHER = 5;

struct AppState {
    Config cfg;
    std::atomic_bool running{true};
    DWORD mainTid = 0;
    DWORD trayTid = 0;

    void PostQuitToMain() const noexcept {
        if (mainTid)
            PostThreadMessageW(mainTid, WM_QUIT, 0, 0);
    }
    void PostQuitToTray() const noexcept {
        if (trayTid)
            PostThreadMessageW(trayTid, WM_QUIT, 0, 0);
    }
};

static void KM_Install(void* p) noexcept {
    static_cast<km::KeyboardManager*>(p)->InstallHook();
}
static void KM_Uninstall(void* p) noexcept {
    static_cast<km::KeyboardManager*>(p)->UninstallHook();
}
static void MM_Install(void* p) noexcept {
    static_cast<mm::MouseManager*>(p)->InstallHook();
}
static void MM_Uninstall(void* p) noexcept {
    static_cast<mm::MouseManager*>(p)->UninstallHook();
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
    if (!utils::EnsureRunAsAdminAndExitIfNot())
        return NOT_ADMIN;

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"hyprwin.throwtop.dev");
    if (!mutex)
        return MUTEX_ERROR;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Another instance is already running.", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return ALREADY_RUNNING;
    }

    SetCurrentProcessExplicitAppUserModelID(L"hyprwin.throwtop.dev");

    tinylog::init({.console = true,
      .file_path = "hyprwin.log",
      .console_level = tinylog::Level::Debug,
      .file_level = tinylog::Level::Trace,
      .date_format = L"MM'-'dd",
      .time_format = L"HH':'mm':'ss"});

    SET_THREAD_NAME("Main");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppState state;
    state.mainTid = GetCurrentThreadId();

    if (!state.cfg.LoadConfig()) {
        MessageBoxW(nullptr, L"Config Fuarked SUPER is required", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return CONFIG_ERROR;
    }

    if (!super_watcher::Install(state.cfg.m_settings.SUPER)) {
        MessageBoxW(nullptr, L"Failed to install SUPER watcher.", L"Error", MB_OK | MB_ICONERROR);
        return SUPER_WATCHER;
    }

    std::atomic<UINT> superVk{state.cfg.m_settings.SUPER};

    // Tray thread (its own message loop)
    std::jthread trayThread([&state, &superVk] {
        SET_THREAD_NAME("tray");
        state.trayTid = GetCurrentThreadId();

        Tray::Icon HW_ICON(IDI_HWICON);
        Tray::Tray sys_tray(L"HyprWin", HW_ICON);

        sys_tray.setTooltip(L"HyprWin");
        sys_tray.DarkMode(Tray::dark::AppModeForceDark);

        sys_tray.onLeftClick([&] { return true; });
        sys_tray.onDoubleClick([&] { return false; });

        // Reload Config
        auto reloadBtn = sys_tray.addEntry(Tray::Button(L"Reload Config", [&] {
            Config newCfg;
            if (!newCfg.LoadConfig()) {
                MessageBoxW(nullptr, L"Config Fuarked", L"Error", MB_OK | MB_ICONERROR);
                return;
            }

            state.cfg = std::move(newCfg);

            super_watcher::SetSuperVk(state.cfg.m_settings.SUPER, true);
            superVk.store(state.cfg.m_settings.SUPER, std::memory_order_relaxed);
        }));
        reloadBtn->setGlyphIcon(Tray::Icon(IDI_HWICON));
        reloadBtn->setDefault(true);

        // Open Config Folder
        sys_tray
          .addEntry(Tray::Button(L"Open Config Folder",
            [&] {
                wchar_t path[MAX_PATH]{};
                GetModuleFileNameW(nullptr, path, MAX_PATH);
                if (wchar_t* last = wcsrchr(path, L'\\'))
                    *last = L'\0';
                ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
            }))
          ->setGlyphIcon(Tray::Icon::FromStock(SIID_FOLDEROPEN));

        sys_tray.addEntry(Tray::Separator());

        // Exit button
        auto btnExit = sys_tray.addEntry(Tray::Button(L"Exit", [&] {
            state.running.store(false, std::memory_order_relaxed);
            state.PostQuitToMain();
            sys_tray.exit();
        }));
        btnExit->setGlyphIcon(Tray::Icon(LoadIconW(nullptr, IDI_HAND)));

        sys_tray.run();
    });

    mm::MouseManager mm(hInstance, &state.cfg);
    km::KeyboardManager km(&state.cfg);

    super_watcher::SetCallbacks(&KM_Install, &KM_Uninstall, &MM_Install, &MM_Uninstall, &km, &mm);

    MSG msg;
    while (state.running && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    super_watcher::Uninstall();

    if (mutex)
        CloseHandle(mutex);

    return 0;
}
