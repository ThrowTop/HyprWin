#pragma once

#include <Windows.h>
#include <format>
#include <iostream>

// Override
//#define DBG // Starts Console for debug printing, Enables LOG Functions even in release mode, enabled by default in Debug
//#define LOG_HOOKS //Logs inside hook threads (will slow down mouse)
#define CONFIG_DEBUG

#ifndef DBG
#ifdef _DEBUG
#define DBG
#endif
#endif

#ifdef DBG
// each thread stores their own name
inline thread_local const char* threadName = "Unnamed";

inline void SetThreadNameInternal(const char* name) {
    threadName = name;
}

inline const char* GetThreadName() {
    return threadName;
}

class DebugConsole {
public:
    DebugConsole() {
        AllocConsole();

        // stdout/stderr to console
        FILE* out = nullptr; FILE* err = nullptr;
        freopen_s(&out, "CONOUT$", "w", stdout);
        freopen_s(&err, "CONOUT$", "w", stderr);

        // stdin to NUL so console input cannot block
        FILE* in = nullptr;
        freopen_s(&in, "NUL", "r", stdin);

        // Enable VT on output
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode = 0;
        if (GetConsoleMode(hOut, &outMode)) {
            outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, outMode);
        }

        // Disable Quick Edit and mouse selection so clicks never pause I/O
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD inMode = 0;
        if (GetConsoleMode(hIn, &inMode)) {
            inMode |= ENABLE_EXTENDED_FLAGS;              // required to change quick-edit
            inMode &= ~ENABLE_QUICK_EDIT_MODE;            // no selection pause
            inMode &= ~ENABLE_INSERT_MODE;                // optional
            inMode &= ~ENABLE_MOUSE_INPUT;                // optional: block mouse events in console
            SetConsoleMode(hIn, inMode);
        }

        // Show without stealing focus, move off-screen if you like
        HWND cw = GetConsoleWindow();
        ShowWindow(cw, SW_SHOWNOACTIVATE);
        SetWindowPos(cw, nullptr, -1000, 50, 100, 100,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    ~DebugConsole() {
        FreeConsole();
    }
};

#define CONSOLE() DebugConsole dc
#define LOG(fmt, ...) printf("[+] " fmt "\n", __VA_ARGS__)
#define WLOG(fmt, ...) wprintf(L"[+] " fmt "\n", __VA_ARGS__)
#define LOG_THREAD() LOG("Thread %d (%s) | %s",GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#define SET_THREAD_NAME(name) SetThreadNameInternal(name); LOG("Thread Named %d (%s) | %s", GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#else
#define CONSOLE() do {} while (0) // compiler optimizes to nop while still allowing empty if statements
#define LOG(...) do {} while (0)
#define WLOG(...) do {} while (0)
#define LOG_THREAD() do {} while (0)
#define SET_THREAD_NAME(name) do {} while (0)
#endif

#if defined(CONFIG_DEBUG) && defined(DBG)
#define LOG_CONFIG(fmt, ...) printf("[CFG] " fmt "\n", __VA_ARGS__)
#define WLOG_CONFIG(fmt, ...) wprintf(L"[CFG] " fmt L"\n", __VA_ARGS__)
#else
#define LOG_CONFIG(...) do {} while (0)
#define WLOG_CONFIG(...) do {} while (0)
#endif

#ifdef LOG_HOOKS
#define HOOK_INSTALL() LOG("HOOK INSTALL %d (%s) | %s",GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#define HOOK_REMOVE() LOG("HOOK REMOVE %d (%s) | %s",GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#else
#define HOOK_INSTALL(...) do {} while (0)
#define HOOK_REMOVE(...) do {} while (0)
#endif

namespace utils {
    // ---- Root selection ----
    // Returns the root top-level HWND for 'hwnd' (no strict filtering).
    // Returns nullptr if not a usable top-level (invisible, iconic, cloaked, etc.).
    HWND TopLevel(HWND hwnd);

    // Returns a filtered root top-level HWND for 'hwnd' using stricter rules
    // (excludes toolwindows, noactivate, likely exclusive fullscreen, etc.).
    // Returns nullptr if it does not pass filters.
    HWND FilteredTopLevel(HWND hwnd);

    // ---- Hit-testing using visual rects ----
    // Permissive: any valid top-level whose visual rect contains the point.
    HWND GetWindow(const POINT& pt);

    // Strict/filtered: only if it passes FilteredTopLevel and visual rect contains the point.
    HWND GetFilteredWindow(const POINT& pt);

    // Convenience overloads that use the current cursor position.
    HWND GetWindow();
    HWND GetFilteredWindow();

    inline POINT Center(const RECT& r) { return POINT{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 }; }

    inline void logWindowData(HWND hwnd) {
        if (!hwnd) {
            return;
        }
        wchar_t className[256] = { 0 };
        GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));
        wchar_t title[256] = { 0 };
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        RECT rect;
        GetWindowRect(hwnd, &rect);
        WLOG("Window: (0x%zX) - Class: %ls, Title: %ls, Rect: (%ld, %ld, %ld, %ld)",
            (size_t)hwnd,
            className, title,
            rect.left, rect.top, rect.right, rect.bottom);
        //std::cout << std::format("[+] " "Window: {} - Class: {}, Title: {}, Rect: ({}, {}, {}, {})", static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(hwnd)), className, title, rect.left, rect.top, rect.right, rect.bottom) << '\n';
    }

    // Focus and elevation
    bool EnsureRunAsAdminAndExitIfNot();

    // Window rect helpers
    bool GetNormalRect(HWND hwnd, RECT& out);               // WINDOWPLACEMENT.rcNormalPosition
    RECT ClampRectToWork(const RECT& r, const RECT& work);
    void SetWindowRect(HWND hwnd, const RECT& r);           // SetWindowPos wrapper (window space)

    // High level helper: set a bordered window filling the current monitor's work area with padding
    void SetBorderedWindow(HWND hwnd, int borderPx);
} // namespace utils