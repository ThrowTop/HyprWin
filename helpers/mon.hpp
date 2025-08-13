// helpers/mon.hpp

#include <windows.h>

// Internal helpers and data
namespace {
    struct MonInfo {
        HMONITOR mon{};
        RECT work{};
        POINT center{};
    };

    static BOOL CALLBACK EnumMonProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
        auto* out = reinterpret_cast<std::vector<MonInfo>*>(lp);
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(hMon, &mi)) return TRUE;
        MonInfo m;
        m.mon = hMon;
        m.work = mi.rcWork;
        m.center.x = (mi.rcWork.left + mi.rcWork.right) / 2;
        m.center.y = (mi.rcWork.top + mi.rcWork.bottom) / 2;
        out->push_back(m);
        return TRUE;
    }
} // namespace

namespace helpers::mon {
    // Monitors and work areas
    HMONITOR GetMonitorFromCursor();
    RECT GetWorkAreaFromWindow(HWND hwnd);
    RECT GetWorkArea(HMONITOR mon);
    HMONITOR FindAdjacentMonitorX(HWND hwnd, bool toRight); // nearest strictly left/right along X

    bool IsBorderlessFullscreen(HWND hwnd, const RECT& wr);

    inline bool RectApproxEq(const RECT& a, const RECT& b, int tol = 2) {
        return abs(a.left - b.left) <= tol &&
            abs(a.top - b.top) <= tol &&
            abs(a.right - b.right) <= tol &&
            abs(a.bottom - b.bottom) <= tol;
    }
} // namespace helpers::mon