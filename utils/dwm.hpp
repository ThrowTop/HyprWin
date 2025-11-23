// helpers/dwm.hpp
#pragma once
#include <Windows.h>
#include "../tinylog.hpp"
namespace utils::dwm {
inline bool GetWindowRectSafe(HWND hwnd, RECT& win) {
    return IsWindow(hwnd) && GetWindowRect(hwnd, &win);
}

inline void SetFocusToWindow(HWND hwnd) {
    if (!IsWindow(hwnd))
        return;

    // force-update last input timestamp to bypass ForegroundLockTimeout
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE; // zero-delta still increments input time
    SendInput(1, &in, sizeof(in));

    // thread IDs: fg = current foreground owner, tg = target window, cur = caller
    DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD tg = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD cur = GetCurrentThreadId();

    // merge input queues; required for SetForegroundWindow to succeed
    // activation fails if caller, fg, and target threads are isolated
    AttachThreadInput(cur, fg, TRUE);
    AttachThreadInput(cur, tg, TRUE);

    // mark caller as allowed foreground setter
    AllowSetForegroundWindow(ASFW_ANY);

    // activation stack: global focus, z-order raise, WM_ACTIVATE, keyboard focus
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    // restore input queue separation
    AttachThreadInput(cur, fg, FALSE);
    AttachThreadInput(cur, tg, FALSE);
}

bool GetVisual(HWND hwnd, RECT& win, RECT& vis);

// fill offsets rect as (L = visL - winL, T = visT - winT, R = visR - winR, B = visB - winB)
bool GetDwmVisualOffsets(HWND hwnd, RECT& offsets);

// get min/max track sizes as a RECT: { left=minW, top=minH, right=maxW, bottom=maxH }
bool GetMinMax(HWND hwnd, RECT& mm);

bool SetWindowVisualRect(HWND hwnd, const RECT& visualRect, UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

void CenterCursorInVisual(HWND hwnd);

// Convenience: position by x,y,w,h in visual space
inline bool SetWindowVisualPos(HWND hwnd, int vx, int vy, int vw, int vh, UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW) {
    RECT vr{vx, vy, vx + vw, vy + vh};
    return SetWindowVisualRect(hwnd, vr, flags);
}

// Just the visual rect
inline bool GetWindowVisualRect(HWND hwnd, RECT& out) {
    RECT win{};
    return GetVisual(hwnd, win, out);
}

// Cursor helper tied to visual rect
void CenterCursorInVisual(HWND hwnd);
} // namespace utils::dwm