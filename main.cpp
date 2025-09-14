#include "pch.hpp"

#include "utils.hpp"
#include "lockfreequeue.hpp"

#include "lib/tray/include/tray.hpp"
#include "keyboardManager.hpp"
#include "mouseManager.hpp"

#include "settings/config.hpp"

#include "resource.h"

#include "tinylog.hpp"

#include <atomic>
#include <chrono>
#include <thread>

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
        CloseHandle(mutex);
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppState state;
    if (!state.cfg.LoadConfig()) {
        MessageBoxW(nullptr, L"Config Fuarked SUPER is required", L"Error", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return CONFIG_ERROR;
    }

    // Shared SUPER vk for lock-free reads in the supervisor loop.
    std::atomic<UINT> superVk{ state.cfg.m_settings.SUPER };

    // Tray thread (its own message loop)
    std::jthread trayThread([&state, &superVk] {
        SET_THREAD_NAME("tray");

        Tray::Tray sys_tray("Shortcut Manager", (HICON)LoadImageW(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDI_HWICON),
            IMAGE_ICON,
            0, 0,
            LR_DEFAULTSIZE | LR_SHARED));

        sys_tray.addEntry(Tray::Button("Exit", [&] {
            {
                std::scoped_lock lock(state.mtx);
                state.running = false;
            }
            state.cv.notify_one();   // wake main supervisor if waiting
            PostQuitMessage(0);      // end tray loop only
        }));

        sys_tray.addEntry(Tray::Button("Reload", [&] {
            Config newCfg;
            if (!newCfg.LoadConfig()) {
                MessageBoxW(nullptr, L"Config Fuarked", L"Error", MB_OK | MB_ICONERROR);
                return;
            }
            {
                std::scoped_lock lock(state.mtx);
                state.cfg = std::move(newCfg);
            }
            superVk.store(state.cfg.m_settings.SUPER, std::memory_order_relaxed);
        }));

        sys_tray.addEntry(Tray::Separator());

        sys_tray.addEntry(Tray::Button("Config Folder", [&] {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (wchar_t* last = wcsrchr(path, L'\\')) *last = L'\0';
            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
        }));

        sys_tray.run();
    });

    // Managers
    mm::MouseManager mm(hInstance, &state.cfg);
    km::KeyboardManager km(&state.cfg);
    km.SetSuperReleasedCallback([&] {
        {
            std::scoped_lock lock(state.mtx);
            state.superReleased = true;
        }
        state.cv.notify_one();
    });

    bool lastDown = false;
    UINT lastVk = superVk.load(std::memory_order_relaxed);

    while (state.running) {
        const UINT vk = superVk.load(std::memory_order_relaxed);
        if (vk != lastVk) {
            // If SUPER vk changed via reload, reset edge detection.
            lastDown = false;
            lastVk = vk;
        }

        const bool superDown = (GetAsyncKeyState(vk) & 0x8000) != 0;

        if (superDown && !lastDown) {
            km.InstallHook();
            mm.InstallHook();

            // Block until SUPER release (or exit)
            std::unique_lock lk(state.mtx);
            state.cv.wait(lk, [&] { return state.superReleased || !state.running; });
            state.superReleased = false;
            lk.unlock();

            km.UninstallHook();
            mm.UninstallHook();
        }

        lastDown = superDown;

        // Lightweight throttle (CPU ~0). If system timer is default, this is ~15.6 ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Cleanup

    if (mutex) CloseHandle(mutex);
    return EXIT_SUCCESS;
}