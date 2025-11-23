#pragma once
// tray.hpp
// Concrete tray implementation using Win32 shell notify icon.
// Best-effort dark mode via undocumented uxtheme exports and DWM attribute.
// Stock menus only (no owner-draw).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <shellapi.h>
#include <strsafe.h>
#include <dwmapi.h>
#include <map>
#include <vector>
#include <memory>
#include <stdexcept>
#include <functional>
#include <utility>

#include <Uxtheme.h>

#pragma comment(lib, "dwmapi.lib")

#include "components.hpp"

static constexpr UINT WM_TRAY = WM_APP + 1;
static constexpr UINT WM_TRAY_EXIT = WM_APP + 100;

static UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

namespace Tray {
namespace dark {
    // Undocumented exports for app dark mode preference
    enum PreferredAppMode { AppModeDefault = 0, AppModeAllowDark = 1, AppModeForceDark = 2, AppModeForceLight = 3, AppModeMax };
    using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);

    static fnSetPreferredAppMode gSetPreferredAppMode = nullptr;
    static fnAllowDarkModeForWindow gAllowDarkModeForWindow = nullptr;

    static void InitDarkModeAPIs() {
        static bool inited = false;
        if (inited)
            return;
        inited = true;

        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (!hUx)
            return;

        gSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUx, MAKEINTRESOURCEA(135)));
        gAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUx, MAKEINTRESOURCEA(133)));
    }

    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (new), 19 (old)
    static BOOL SetImmersiveDarkMode(HWND hwnd, BOOL enable) {
        const DWORD attrNew = 20;
        const DWORD attrOld = 19;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, attrNew, &enable, sizeof(enable))))
            return TRUE;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, attrOld, &enable, sizeof(enable))))
            return TRUE;
        return FALSE;
    }
} // namespace dark

class Tray : public BaseTray {
    HWND hwnd = nullptr;
    HMENU menu = nullptr;
    WNDCLASSEXW windowClass{};
    NOTIFYICONDATAW notifyData{};

    dark::PreferredAppMode appMode = dark::AppModeDefault;
    std::vector<std::shared_ptr<wchar_t[]>> allocations;

    inline static std::map<HWND, std::reference_wrapper<Tray>> trayList;

    std::function<bool()> leftClickCb;
    std::function<bool()> doubleClickCb;
    std::function<bool()> rightClickCb;

    bool isExiting = false;

  public:
    void requestExit() {
        if (hwnd && IsWindow(hwnd)) {
            PostMessageW(hwnd, WM_TRAY_EXIT, 0, 0);
        }
    }

    ~Tray() {
        allocations.clear();
    }

    // primary ctor: Icon
    // primary ctor: Icon
    Tray(std::wstring id, Icon ic) : BaseTray(std::move(id), std::move(ic)) {
        ZeroMemory(&windowClass, sizeof(windowClass));
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS; // ensure dbl-clicks get coalesced properly
        windowClass.lpfnWndProc = wndProc;
        windowClass.lpszClassName = this->identifier.c_str();
        windowClass.hInstance = GetModuleHandle(nullptr);

        if (RegisterClassExW(&windowClass) == 0) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("Failed to register class");
        }

        hwnd = CreateWindowExW(0L, this->identifier.c_str(), nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, windowClass.hInstance, nullptr);
        if (!hwnd)
            throw std::runtime_error("Failed to create window");

        if (!UpdateWindow(hwnd))
            throw std::runtime_error("Failed to update window");

        ZeroMemory(&notifyData, sizeof(NOTIFYICONDATAW));
        notifyData.cbSize = sizeof(NOTIFYICONDATAW);
        notifyData.hWnd = hwnd;
        notifyData.uID = 1;
        notifyData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        notifyData.uCallbackMessage = WM_TRAY;
        notifyData.hIcon = this->icon;

        // Optional initial tooltip
        StringCchCopyW(notifyData.szTip, ARRAYSIZE(notifyData.szTip), this->identifier.c_str());

        if (!Shell_NotifyIconW(NIM_ADD, &notifyData))
            throw std::runtime_error("Failed to register tray icon");

        notifyData.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconA(NIM_SETVERSION, reinterpret_cast<PNOTIFYICONDATAA>(&notifyData));
        // Shell_NotifyIconW(NIM_SETVERSION, &notifyData); // Completly breaks system tray, nothing works EVER

        trayList.insert({hwnd, *this});
    }

    // convenience overloads
    Tray(std::wstring id, HICON hicon) : Tray(std::move(id), Icon(hicon)) {}
    Tray(std::wstring id, const wchar_t* iconPath) : Tray(std::move(id), Icon(iconPath)) {}
    Tray(std::wstring id, WORD resid) : Tray(std::move(id), Icon(resid)) {}

    template <typename... Ts>
    Tray(std::wstring id, Icon ic, const Ts&... es) : Tray(std::move(id), std::move(ic)) {
        addEntries(es...);
    }

    void setTooltip(const std::wstring& tip) {
        NOTIFYICONDATAW nid = notifyData; // copy
        nid.uFlags = NIF_TIP;
        StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    // In class Tray (inside tray.hpp)

    void showNotification(const std::wstring& title,
      const std::wstring& body,
      DWORD infoFlags = NIIF_INFO,
      UINT timeoutMs = 10000,
      const Icon& ic = Icon()) // default: empty => use tray icon
    {
        NOTIFYICONDATAW nid = notifyData; // copy of registered data
        nid.uFlags = NIF_INFO;
        StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title.c_str());
        StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), body.c_str());

        // Win10/11 mostly ignore this, but keep it for older builds.
        nid.uTimeout = timeoutMs;

        nid.dwInfoFlags = infoFlags;

        // Use provided icon if non-empty; otherwise reuse our tray icon.
        HICON hBalloon = ic.get() ? ic.get() : notifyData.hIcon;

        // Only mark NIIF_USER when we are explicitly specifying a glyph.
        // Using NIIF_USER with our tray icon is also fine; it avoids the info/warn/error glyphs.
        nid.dwInfoFlags &= ~(NIIF_INFO | NIIF_WARNING | NIIF_ERROR); // remove built-ins
        nid.dwInfoFlags |= NIIF_USER;
        nid.hBalloonIcon = hBalloon;

        // Fire it
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    bool DarkMode(dark::PreferredAppMode mode = dark::AppModeAllowDark) {
        appMode = mode;
        dark::InitDarkModeAPIs();
        if (dark::gSetPreferredAppMode)
            dark::gSetPreferredAppMode(mode);
        BOOL ok1 = dark::SetImmersiveDarkMode(hwnd, TRUE);
        BOOL ok2 = dark::gAllowDarkModeForWindow ? dark::gAllowDarkModeForWindow(hwnd, TRUE) : FALSE;
        return ok1 || ok2;
    }

    // Install handlers. If unset, defaults are: right click opens menu;
    // left click opens menu; double click activates default item.
    void onLeftClick(std::function<bool()> cb) {
        leftClickCb = std::move(cb);
    }
    void onDoubleClick(std::function<bool()> cb) {
        doubleClickCb = std::move(cb);
    }
    void onRightClick(std::function<bool()> cb) {
        rightClickCb = std::move(cb);
    }

    // Change the tray icon at runtime.
    void setIcon(Icon ic) {
        icon = std::move(ic);
        notifyData.hIcon = icon;
        Shell_NotifyIconW(NIM_MODIFY, &notifyData);
    }

    // Show/hide the icon without removing it (Windows ignores timeout etc. on new add).
    void setVisible(bool visible) {
        NOTIFYICONDATAW nid = notifyData;
        nid.uFlags = NIF_STATE;
        nid.dwStateMask = NIS_HIDDEN;
        nid.dwState = visible ? 0 : NIS_HIDDEN;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void run() {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void exit() {
        isExiting = true;

        Shell_NotifyIconW(NIM_DELETE, &notifyData);

        if (menu) {
            DestroyMenu(menu);
            menu = nullptr;
        }

        HWND old = hwnd;

        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }

        trayList.erase(old);
        UnregisterClassW(identifier.c_str(), GetModuleHandleW(nullptr));

        allocations.clear();
        PostQuitMessage(0);
    }

    void update() {
        if (isExiting)
            return;
        if (menu) {
            DestroyMenu(menu);
            menu = nullptr;
        }
        menu = construct(entries, this, true);

        if (!Shell_NotifyIconW(NIM_MODIFY, &notifyData)) {
            // explorer may have restarted; re-add then modify
            Shell_NotifyIconW(NIM_ADD, &notifyData);
            notifyData.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &notifyData);
            Shell_NotifyIconW(NIM_MODIFY, &notifyData);
        }
    }

  private:
    // static
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // Guard: ignore messages if this hwnd is no longer in the map
        auto it = trayList.find(hwnd);
        if (it == trayList.end()) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        if (msg == WM_TASKBARCREATED) {
            auto& tray = it->second.get();
            Shell_NotifyIconW(NIM_ADD, &tray.notifyData);
            Shell_NotifyIconW(NIM_SETVERSION, &tray.notifyData);
            tray.DarkMode(tray.appMode);
            return 0;
        }

        switch (msg) {
            case WM_TRAY: {
                auto& tray = it->second.get();

                switch (lParam) {
                    case WM_LBUTTONUP: {
                        if (tray.leftClickCb && tray.leftClickCb()) {
                            return 0;
                        }
                        // default: open menu at cursor
                        POINT p;
                        GetCursorPos(&p);
                        SetForegroundWindow(hwnd);
                        if (!tray.menu)
                            tray.menu = construct(tray.entries, &tray, true);
                        UINT cmd = TrackPopupMenuEx(tray.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, hwnd, nullptr);
                        if (cmd)
                            SendMessageW(hwnd, WM_COMMAND, cmd, 0);
                        PostMessageW(hwnd, WM_NULL, 0, 0);
                        return 0;
                    }

                    case WM_LBUTTONDBLCLK: {
                        if (tray.doubleClickCb && tray.doubleClickCb())
                            return 0;

                        if (!tray.menu)
                            tray.menu = construct(tray.entries, &tray, true);
                        UINT defId = GetMenuDefaultItem(tray.menu, FALSE, 0);
                        if (defId != (UINT)-1)
                            SendMessageW(hwnd, WM_COMMAND, defId, 0);
                        return 0;
                    }

                    case WM_RBUTTONUP:
                    case WM_CONTEXTMENU: { // shell callback: no coords -> always use cursor pos
                        if (tray.rightClickCb && tray.rightClickCb())
                            return 0;

                        POINT p;
                        GetCursorPos(&p);
                        SetForegroundWindow(hwnd);
                        if (!tray.menu)
                            tray.menu = construct(tray.entries, &tray, true);
                        UINT cmd = TrackPopupMenuEx(tray.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, hwnd, nullptr);
                        if (cmd)
                            SendMessageW(hwnd, WM_COMMAND, cmd, 0);
                        PostMessageW(hwnd, WM_NULL, 0, 0);
                        return 0;
                    }
                }
                break;
            }
            case WM_COMMAND: {
                MENUITEMINFOW mi{};
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_DATA | MIIM_ID;

                auto& tray = it->second.get();
                if (tray.menu && GetMenuItemInfoW(tray.menu, static_cast<UINT>(wParam), FALSE, &mi)) {
                    auto* item = reinterpret_cast<TrayEntry*>(mi.dwItemData);
                    if (auto* button = dynamic_cast<Button*>(item)) {
                        button->clicked();
                    } else if (auto* toggle = dynamic_cast<Toggle*>(item)) {
                        toggle->onToggled();
                        tray.update();
                    }
                }
                return 0;
            }
            case WM_TRAY_EXIT: {
                auto& tray = it->second.get();
                tray.exit();
                return 0;
            }
            case WM_NCDESTROY: {
                trayList.erase(hwnd);
                break;
            }
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // static
    static HMENU construct(const std::vector<std::shared_ptr<TrayEntry>>& entries, Tray* parent, bool cleanup = false) {
        static UINT id = 0;
        if (cleanup) {
            parent->allocations.clear();
            id = 0;
        }

        HMENU hmenu = CreatePopupMenu();
        if (!hmenu)
            throw std::runtime_error("CreatePopupMenu failed");

        // Make sure bitmaps OR checks render with themed menus
        MENUINFO miMenu{};
        miMenu.cbSize = sizeof(miMenu);
        miMenu.fMask = MIM_STYLE;
        miMenu.dwStyle = MNS_CHECKORBMP; // allow hbmpItem next to text
        SetMenuInfo(hmenu, &miMenu);

        // Track the default item ID so GetMenuDefaultItem works on dbl-click
        UINT defaultId = (UINT)-1;

        for (const auto& sp : entries) {
            TrayEntry* item = sp.get();
            if (!item)
                continue; // robust: skip nulls

            MENUITEMINFOW mi{};
            mi.cbSize = sizeof(mi);

            // true separator: only type
            if (dynamic_cast<Separator*>(item)) {
                mi.fMask = MIIM_FTYPE;
                mi.fType = MFT_SEPARATOR;
                if (!InsertMenuItemW(hmenu, GetMenuItemCount(hmenu), TRUE, &mi))
                    throw std::runtime_error("InsertMenuItemW separator failed");
                continue;
            }

            // persistent text buffer
            const std::wstring& txt = item->getText();
            auto buf = std::make_unique<wchar_t[]>(txt.size() + 1);
            StringCchCopyW(buf.get(), txt.size() + 1, txt.c_str());
            parent->allocations.emplace_back(std::move(buf));
            LPWSTR lpText = parent->allocations.back().get();

            bool isSubmenu = (dynamic_cast<Submenu*>(item) != nullptr);
            bool isLabelOnly = (dynamic_cast<Label*>(item) != nullptr);
            bool isToggle = (dynamic_cast<Toggle*>(item) != nullptr);
            bool needsId = !isSubmenu && !isLabelOnly;

            mi.fMask = MIIM_STRING | MIIM_STATE | MIIM_DATA;
            mi.dwTypeData = lpText;
            mi.dwItemData = reinterpret_cast<ULONG_PTR>(item);

            if (needsId) {
                mi.fMask |= MIIM_ID;
                mi.wID = ++id;
            }

            // submenu
            if (isSubmenu) {
                auto* submenu = static_cast<Submenu*>(item);
                mi.fMask |= MIIM_SUBMENU;
                mi.hSubMenu = construct(submenu->getEntries(), parent);
                // Propagate menu style to child, too
                MENUINFO miChild{};
                miChild.cbSize = sizeof(miChild);
                miChild.fMask = MIM_STYLE;
                miChild.dwStyle = MNS_CHECKORBMP;
                SetMenuInfo(mi.hSubMenu, &miChild);
            }

            // toggle: checked/unchecked + optional custom checkmark bitmaps
            if (isToggle) {
                auto* t = static_cast<Toggle*>(item);
                mi.fState |= t->isToggled() ? MFS_CHECKED : MFS_UNCHECKED;

                if (t->getCheckedBitmap() || t->getUncheckedBitmap()) {
                    mi.fMask |= MIIM_CHECKMARKS;
                    mi.hbmpChecked = t->getCheckedBitmap();
                    mi.hbmpUnchecked = t->getUncheckedBitmap();
                }
            }

            // glyph (icon/bitmap) next to text
            {
                const int cx = GetSystemMetrics(SM_CXMENUCHECK);
                const int cy = GetSystemMetrics(SM_CYMENUCHECK);
                if (HBITMAP hb = item->getOrBuildGlyphBitmap(cx, cy)) {
                    mi.fMask |= MIIM_BITMAP | MIIM_FTYPE;
                    mi.fType = MFT_STRING; // string with a bitmap
                    mi.hbmpItem = hb;
                }
            }

            // default / disabled / label
            if (item->isDefault() && needsId) {
                mi.fState |= MFS_DEFAULT;
                defaultId = mi.wID;
            }
            if (isLabelOnly) {
                mi.fState = MFS_DISABLED;
            } else if (item->isDisabled()) {
                mi.fState = MFS_DISABLED;
            }

            if (!InsertMenuItemW(hmenu, GetMenuItemCount(hmenu), TRUE, &mi))
                throw std::runtime_error("InsertMenuItemW failed");
        }

        if (defaultId != (UINT)-1)
            SetMenuDefaultItem(hmenu, defaultId, FALSE);

        return hmenu;
    }
};
} // namespace Tray
