#include <pch.hpp>
#include "mon.hpp"
#include <vector>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

namespace utils::mon {
// -------- Monitors and work areas --------

HMONITOR GetMonitorFromCursor() {
    POINT pt;
    GetCursorPos(&pt);
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

RECT GetWorkAreaFromWindow(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    return mi.rcWork;
}

RECT GetWorkArea(HMONITOR mon) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    return mi.rcWork;
}

// helpers/mon.cpp
HMONITOR FindAdjacentMonitorX(HWND hwnd, bool toRight) {
    HMONITOR cur = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO cmi{sizeof(cmi)};
    if (!GetMonitorInfoW(cur, &cmi))
        return nullptr;

    const long cx = (cmi.rcWork.left + cmi.rcWork.right) / 2;

    std::vector<MonInfo> mons;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&mons));

    long bestDist = LONG_MAX;
    HMONITOR best = nullptr;

    for (const auto& m : mons) {
        if (m.mon == cur)
            continue; // do not consider current monitor
        long dx = m.center.x - cx;

        if (toRight) {
            if (dx <= 0)
                continue; // must be strictly to the right
            if (dx < bestDist) {
                bestDist = dx;
                best = m.mon;
            }
        } else {
            if (dx >= 0)
                continue;   // must be strictly to the left
            long pdx = -dx; // distance magnitude
            if (pdx < bestDist) {
                bestDist = pdx;
                best = m.mon;
            }
        }
    }

    return best; // nullptr if none found
}

bool IsBorderlessFullscreen(HWND hwnd, const RECT& wr) {
    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi))
        return false;

    const bool covers_monitor = EqualRect(&wr, &mi.rcMonitor);

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool borderless = (style & WS_CAPTION) == 0 && (style & WS_THICKFRAME) == 0;

    return covers_monitor && borderless;
}
} // namespace utils::mon