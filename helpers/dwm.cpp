#include <pch.hpp>
// helpers/dwm.cpp
#include "dwm.hpp"
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

namespace helpers::dwm {
    bool GetVisual(HWND hwnd, RECT& win, RECT& vis) {
        if (!GetWindowRectSafe(hwnd, win)) return false;
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof(vis))))
            vis = win;
        return true;
    }

    bool GetDwmVisualOffsets(HWND hwnd, RECT& offsets) {
        RECT wr{}, vr{};
        if (!GetVisual(hwnd, wr, vr)) return false;
        offsets.left = vr.left - wr.left;
        offsets.top = vr.top - wr.top;
        offsets.right = vr.right - wr.right;
        offsets.bottom = vr.bottom - wr.bottom;
        return true;
    }

    bool GetMinMax(HWND hwnd, RECT& mm) {
        if (!IsWindow(hwnd)) return false;
        MINMAXINFO mmi{};
        SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
        const int minW = (mmi.ptMinTrackSize.x > 0) ? mmi.ptMinTrackSize.x : 100;
        const int minH = (mmi.ptMinTrackSize.y > 0) ? mmi.ptMinTrackSize.y : 38;
        const int maxW = (mmi.ptMaxTrackSize.x > 0) ? mmi.ptMaxTrackSize.x : INT_MAX;
        const int maxH = (mmi.ptMaxTrackSize.y > 0) ? mmi.ptMaxTrackSize.y : INT_MAX;
        mm.left = minW;
        mm.top = minH;
        mm.right = maxW;
        mm.bottom = maxH;
        return true;
    }

    bool SetWindowVisualRect(HWND hwnd, const RECT& visualRect, UINT flags) {
        if (!IsWindow(hwnd)) return false;

        // Ensure restored so SetWindowPos applies correctly
        WINDOWPLACEMENT wp{ sizeof(wp) };
        if (GetWindowPlacement(hwnd, &wp)) {
            if (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED)
                ShowWindow(hwnd, SW_RESTORE);
        }

        RECT wr{}, vr{};
        if (!dwm::GetVisual(hwnd, wr, vr)) return false;

        const int offL = vr.left - wr.left;
        const int offT = vr.top - wr.top;
        const int offR = wr.right - vr.right;
        const int offB = wr.bottom - vr.bottom;

        const int wx = visualRect.left - offL;
        const int wy = visualRect.top - offT;
        const int ww = (visualRect.right - visualRect.left) + offL + offR;
        const int wh = (visualRect.bottom - visualRect.top) + offT + offB;

        SetFocusToWindow(hwnd);
        return SetWindowPos(hwnd, nullptr, wx, wy, ww, wh, flags) != FALSE;
    }

    void CenterCursorInVisual(HWND hwnd) {
        RECT wr{}, vr{};
        if (GetVisual(hwnd, wr, vr)) {
            POINT c{ (vr.left + vr.right) / 2, (vr.top + vr.bottom) / 2 };
            SetCursorPos(c.x, c.y);
        }
    }
} // namespace helpers::dwm