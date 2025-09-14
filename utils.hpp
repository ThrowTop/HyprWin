#pragma once

#include <Windows.h>
#include <format>
#include <iostream>
#include "tinylog.hpp"
#include "settings/parser.hpp"

// Override
//#define DBG // Starts Console for debug printing, Enables LOG Functions even in release mode, enabled by default in Debug
//#define LOG_HOOKS //Logs inside hook threads (will slow down mouse)

#if !defined(DBG) && defined(_DEBUG)
#define DBG
#endif

#ifdef DBG
#define CONFIG_DEBUG
//#define HOOK_DEBUG

// each thread stores their own name
inline thread_local const char* threadName = "Unnamed";

inline void SetThreadNameInternal(const char* name) {
    threadName = name;
}

inline const char* GetThreadName() {
    return threadName;
}

#define LOG_THREAD() LOG_T("Thread {} ({}) | {}", GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#define SET_THREAD_NAME(name) SetThreadNameInternal(name); LOG_T("Thread {} ({}) | {}", GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#else
#define CONSOLE() (void)0 // compiler optimizes to nop while still allowing empty if statements
#define LOG_THREAD() (void)0
#define SET_THREAD_NAME(name) (void)0
#endif

#if defined(CONFIG_DEBUG) && defined(DBG)
#define LOG_CONFIG(...) LOG_D(__VA_ARGS__)
#else
#define LOG_CONFIG(...) (void)0
#endif

#ifdef HOOK_DEBUG
#define HOOK_INSTALL() LOG_D("Hook Installed {} ({}) | {}", GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#define HOOK_REMOVE() LOG_D("Hook Removed {} ({}) | {}", GetCurrentThreadId(), GetThreadName(), __FUNCTION__)
#else
#define HOOK_INSTALL(...) (void)0
#define HOOK_REMOVE(...) (void)0
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
        RECT rect;
        GetWindowRect(hwnd, &rect);
        LOG_T("Hw: (0x{:X}) CLS: {}, Rect: {}",
            reinterpret_cast<size_t>(hwnd),
            tinylog::WideToUtf8(std::wstring(className)),
            parse::rectToStr(rect));
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