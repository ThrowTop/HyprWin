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

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <ShlObj.h> // Icon Definitions
#pragma comment(lib, "shell32.lib")

#define VERSION "0.1.2"

constexpr int NOT_ADMIN = 1;
constexpr int ALREADY_RUNNING = 2;
constexpr int CONFIG_ERROR = 3;
constexpr int MUTEX_ERROR = 4;

struct AppState {
    Config cfg;
    std::atomic_bool running{true};
    DWORD mainTid = 0;

    void PostQuitToMain() const noexcept {
        if (mainTid)
            PostThreadMessageW(mainTid, WM_QUIT, 0, 0);
    }
};

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

    SET_THREAD_NAME("Main+Tray");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppState state{};
    state.mainTid = GetCurrentThreadId();

    if (!state.cfg.LoadConfig()) {
        MessageBoxW(nullptr, L"Config Fuarked SUPER is required", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return CONFIG_ERROR;
    }
    utils::DisableProcessThrottling();
    mm::MouseManager mm(hInstance, &state.cfg);
    km::KeyboardManager km(&state.cfg);

    km.SetSuperPressedCallback([&]() { mm.InstallHook(); });
    km.SetSuperReleasedCallback([&]() { mm.UninstallHook(); });

    // Tray on main thread
    try {
        Tray::Icon HW_ICON(IDI_HWICON);
        Tray::Tray sys_tray(L"HyprWin " VERSION, HW_ICON);

        sys_tray.setTooltip(L"HyprWin");
        sys_tray.DarkMode(Tray::dark::AppModeForceDark);
        sys_tray.onLeftClick([&] { return true; });
        sys_tray.onDoubleClick([&] { return false; });

        auto reloadBtn = sys_tray.addEntry(Tray::Button(L"Reload Config", [&] {
            Config newCfg;
            if (!newCfg.LoadConfig()) {
                MessageBoxW(nullptr, L"Config Fuarked", L"Error", MB_OK | MB_ICONERROR);
                return;
            }
            state.cfg = std::move(newCfg);
        }));
        reloadBtn->setGlyphIcon(Tray::Icon(IDI_HWICON));
        reloadBtn->setDefault(true);

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

        sys_tray
          .addEntry(Tray::Button(L"Exit",
            [&] {
                state.running.store(false, std::memory_order_relaxed);
                sys_tray.exit();
            }))
          ->setGlyphIcon(Tray::Icon(LoadIconW(nullptr, IDI_HAND)));

        sys_tray.run();
    } catch (std::runtime_error& e) {
        MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
    }

    if (mutex)
        CloseHandle(mutex);
    return 0;
}