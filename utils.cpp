// utils.cpp
#include "pch.hpp"
#include "utils.hpp"

#include "helpers/dwm.hpp"
#include "helpers/mon.hpp"

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#include <shellapi.h>
#include <userenv.h>
#pragma comment(lib, "Userenv.lib")

#include <limits>

namespace utils {
    // -------- Window discovery --------
    static bool IsShellProtected(HWND h) {
        if (!h) return false;

        // Hard handles
        if (h == GetDesktopWindow() || h == GetShellWindow()) return true;

        // Class filters
        wchar_t cls[64] = { 0 };
        if (GetClassNameW(h, cls, 64)) {
            if (lstrcmpW(cls, L"Progman") == 0) return true;                 // legacy desktop host
            if (lstrcmpW(cls, L"WorkerW") == 0) return true;                 // wallpaper hosts
            if (lstrcmpW(cls, L"SHELLDLL_DefView") == 0) return true;        // desktop view
            if (lstrcmpW(cls, L"Shell_TrayWnd") == 0) return true;           // taskbar primary
            if (lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0) return true;  // taskbar secondary
        }

        // Desktop containers often host SHELLDLL_DefView as a child
        if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) return true;

        return false;
    }

    static bool IsLikelyExclusiveFullscreen(HWND hwnd) {
        if (!IsWindow(hwnd)) return false;

        RECT wr{};
        if (!GetWindowRect(hwnd, &wr)) return false;

        MONITORINFO mi{ sizeof(mi) };
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(mon, &mi)) return false;

        const bool covers_monitor = EqualRect(&wr, &mi.rcMonitor);

        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        if (style & WS_MAXIMIZE) return false;

        const bool borderless_popup =
            (style & WS_POPUP) && !(style & WS_CAPTION) && !(style & WS_THICKFRAME);

        const bool no_sysmenu = (style & WS_SYSMENU) == 0;
        const bool topmost = (ex & WS_EX_TOPMOST) != 0;

        RECT vr{};
        const bool vr_ok = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &vr, sizeof(vr)));
        const bool no_extra_frame = !vr_ok || EqualRect(&wr, &vr);

        return covers_monitor && borderless_popup && no_sysmenu && (topmost || no_extra_frame);
    }

    // Any valid top-level (no exclusive/toolwindow filtering)
    HWND TopLevel(HWND hwnd) {
        if (!IsWindow(hwnd)) return nullptr;

        // climb to root
        HWND top = hwnd;
        for (int i = 0; i < 16 && top; ++i) {
            HWND parent = GetParent(top);
            LONG_PTR style = GetWindowLongPtrW(top, GWL_STYLE);
            if (!parent || (style & WS_CHILD) == 0) break;
            top = parent;
        }
        if (!top) return nullptr;

        top = GetAncestor(top, GA_ROOT);
        if (!IsWindow(top)) return nullptr;

        // basic sanity so it can actually be "under the cursor"
        if (!IsWindowVisible(top)) return nullptr;
        if (IsIconic(top)) return nullptr;

        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(top, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
            return nullptr;

        return top;
    }

    // Filtered top-level (your previous rules)
    HWND FilteredTopLevel(HWND hwnd) {
        HWND top = TopLevel(hwnd);
        if (!top) return nullptr;

        LONG_PTR style = GetWindowLongPtrW(top, GWL_STYLE);
        LONG_PTR ex = GetWindowLongPtrW(top, GWL_EXSTYLE);

        if (style & WS_CHILD) return nullptr;
        if (ex & WS_EX_TOOLWINDOW) return nullptr;
        if (ex & WS_EX_NOACTIVATE) return nullptr;

        if (IsLikelyExclusiveFullscreen(top)) return nullptr;

        DWORD pid = 0;
        GetWindowThreadProcessId(top, &pid);
        if (!pid) return nullptr;

        return top;
    }

    // ------- Hit-testing helpers using visual rect -------

    static inline bool ContainsPointVisual(HWND hwnd, const POINT& pt) {
        RECT r{};
        return helpers::dwm::GetWindowVisualRect(hwnd, r) && PtInRect(&r, pt);
    }

    // Walk down Z from a given top-level to find first whose visual rect contains pt.
    // The mapper decides how to map intermediate HWNDs to a considered root (TopLevel vs FilteredTopLevel).
    template <typename MapToRootFn>
    static HWND HitTestZOrder(HWND startTop, const POINT& pt, MapToRootFn mapToRoot) {
        HWND seen = nullptr;
        for (HWND h = GetWindow(startTop, GW_HWNDNEXT); h; h = GetWindow(h, GW_HWNDNEXT)) {
            HWND root = mapToRoot(h);
            if (!root) continue;
            if (root == seen) continue;
            seen = root;
            if (ContainsPointVisual(root, pt))
                return root;
        }
        return nullptr;
    }

    // ------- Public API: GetWindow / GetFilteredWindow (POINT + cursor) -------

    // Permissive: any top-level that visually contains the point.
    HWND GetWindow(const POINT& pt) {
        HWND hit = WindowFromPoint(pt);
        if (!hit) return nullptr;

        HWND top = TopLevel(hit);
        if (!top) return nullptr;

        if (IsShellProtected(top)) {
            // Search z-order for the next eligible top-level under the point
            return HitTestZOrder(top, pt, [](HWND h) -> HWND {
                HWND t = TopLevel(h);
                return (t && !IsShellProtected(t)) ? t : nullptr;
                });
        }

        if (ContainsPointVisual(top, pt))
            return top;

        return HitTestZOrder(top, pt, [](HWND h) -> HWND {
            HWND t = TopLevel(h);
            return (t && !IsShellProtected(t)) ? t : nullptr;
            });
    }

    // Strict/filtered: obeys your previous filtering rules.
    HWND GetFilteredWindow(const POINT& pt) {
        HWND hit = WindowFromPoint(pt);
        if (!hit) return nullptr;

        HWND top = FilteredTopLevel(hit);
        if (!top) return nullptr;

        if (ContainsPointVisual(top, pt))
            return top;

        return HitTestZOrder(top, pt, [](HWND h) { return FilteredTopLevel(h); });
    }

    // Cursor-position overloads
    HWND GetWindow() {
        POINT pt{};
        GetCursorPos(&pt);
        return GetWindow(pt);
    }

    HWND GetFilteredWindow() {
        POINT pt{};
        GetCursorPos(&pt);
        return GetFilteredWindow(pt);
    }

    // -------- Focus and elevation --------

    bool EnsureRunAsAdminAndExitIfNot() {
        BOOL isAdmin = FALSE;
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID adminGroup = nullptr;

        if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0,
            &adminGroup)) {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }

        if (!isAdmin) {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, MAX_PATH);

            SHELLEXECUTEINFOW sei{ sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = path;
            sei.hwnd = nullptr;
            sei.nShow = SW_SHOWNORMAL;

            if (ShellExecuteExW(&sei)) {
                return false; // elevated instance launched, caller should exit
            }
            MessageBoxW(nullptr, L"Elevation failed.", L"Error", MB_OK | MB_ICONERROR);
            return false;
        }

        return true;
    }

    // -------- Window rect helpers --------

    bool GetNormalRect(HWND hwnd, RECT& out) {
        WINDOWPLACEMENT wp{ sizeof(wp) };
        if (!GetWindowPlacement(hwnd, &wp)) return false;
        out = wp.rcNormalPosition;
        return true;
    }

    RECT ClampRectToWork(const RECT& r, const RECT& work) {
        RECT out = r;
        long w = r.right - r.left;
        long h = r.bottom - r.top;
        long ww = work.right - work.left;
        long wh = work.bottom - work.top;

        if (w > ww) w = ww;
        if (h > wh) h = wh;

        if (out.left < work.left) { out.left = work.left; out.right = out.left + w; }
        if (out.top < work.top) { out.top = work.top;  out.bottom = out.top + h; }

        if (out.right > work.right) { out.right = work.right;  out.left = out.right - w; }
        if (out.bottom > work.bottom) { out.bottom = work.bottom; out.top = out.bottom - h; }

        return out;
    }

    void SetWindowRect(HWND hwnd, const RECT& r) {
        SetWindowPos(hwnd, nullptr,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetBorderedWindow(HWND hwnd, int borderPx) {
        if (!IsWindow(hwnd)) return;

        // Restore if needed
        WINDOWPLACEMENT wp{ sizeof(wp) };
        if (GetWindowPlacement(hwnd, &wp)) {
            if (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED)
                ShowWindow(hwnd, SW_RESTORE);
        }

        HMONITOR mon = helpers::mon::GetMonitorFromCursor();
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(mon, &mi)) return;

        RECT vr{};
        vr.left = mi.rcWork.left + borderPx;
        vr.top = mi.rcWork.top + borderPx;
        vr.right = mi.rcWork.right - borderPx;
        vr.bottom = mi.rcWork.bottom - borderPx;

        helpers::dwm::SetWindowVisualRect(hwnd, vr, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }
} // namespace utils