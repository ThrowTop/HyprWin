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

    AllowSetForegroundWindow(ASFW_ANY);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
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