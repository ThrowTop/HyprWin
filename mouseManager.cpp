#include "pch.hpp"
#include "mouseManager.hpp"
#include "utils.hpp"
#include "overlay.hpp"
#include "settings/config.hpp"
#include "helpers/dwm.hpp"
#include "helpers/mon.hpp"

namespace mm {
    MouseManager::MouseManager(HINSTANCE hi, Config* cfg) : hInstance(hi), config(cfg) {
        instance = this;

        inputThread = std::jthread([this](std::stop_token st) {
            inputToken = st;
            InputLoop(st);
        });

        hookThread = std::jthread([this](std::stop_token st) {
            hookToken = st;
            HookLoop(st);
        });

        overlayThread = std::jthread([this](std::stop_token st) {
            overlayToken = st;
            OverlayLoop(st);
        });
    }

    MouseManager::~MouseManager() {
        inputThread.request_stop();
        hookThread.request_stop();
        overlayThread.request_stop();

        UninstallHook();

        cv.notify_all();
        hookCv.notify_all();
        overlayCv.notify_all();

        if (inputThread.joinable()) inputThread.join();
        if (hookThread.joinable()) hookThread.join();
        if (overlayThread.joinable()) overlayThread.join();
    }

    void MouseManager::InstallHook() {
        {
            std::scoped_lock lock(hookCvMutex);
            installHookRequested = true;
        }

        hookCv.notify_one();
        overlayCv.notify_one();
    }

    void MouseManager::UninstallHook() {
        {
            std::scoped_lock lock(hookCvMutex);
            uninstallHookRequested = true;
        }
        mouseQueue.push(WM_LBUTTONUP);
        mouseQueue.push(WM_RBUTTONUP);

        hookCv.notify_one();
        PostThreadMessage(hookThreadId, WM_QUIT, 0, 0);

        cv.notify_one();
    }

    LRESULT CALLBACK MouseManager::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
        if (code >= 0 && instance && lParam) {
            MSLLHOOKSTRUCT* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

            if (wParam == WM_MOUSEMOVE) {
                instance->latestMousePos.store(ms->pt, std::memory_order_relaxed);
            }
            else {
                instance->mouseQueue.push(wParam);
                instance->cv.notify_one();

                return 1;

                switch (wParam) {
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN:
                case WM_MOUSEWHEEL:
                case WM_MOUSEHWHEEL:
                return 1;
                case WM_LBUTTONUP:
                case WM_RBUTTONUP:
                case WM_MBUTTONUP:
                return CallNextHookEx(nullptr, code, wParam, lParam); // allow release
                default:
                return 1;
                }
            }
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void MouseManager::OverlayLoop(std::stop_token st) {
        OverlayWindow overlay;
        overlay.Init(hInstance);
        SET_THREAD_NAME("Overlay");

        std::unique_lock lock(overlayCvMutex);
        while (!st.stop_requested()) {
            MSG msg;
            while (PeekMessage(&msg, overlay.GetHwnd(), 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            overlayCv.wait(lock, [&] {
                return st.stop_requested() || windowAction.load(std::memory_order_acquire) != 0;
            });

            if (st.stop_requested()) break;

            short action = windowAction.load(std::memory_order_acquire);
            if (action == 1) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
            }
            else if (action == 2) {
                switch (resizeCorner) {
                case ResizeCorner::TopLeft:
                case ResizeCorner::BottomRight:
                SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                break;
                case ResizeCorner::TopRight:
                case ResizeCorner::BottomLeft:
                SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
                break;
                default:
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                break;
                }
            }

            D2D1_COLOR_F c1 = config->m_settings.color;

            if (config->m_settings.gradient) {
                D2D1_COLOR_F c2 = config->m_settings.color2;

                overlay.SetGradient(
                    c1,
                    c2,
                    config->m_settings.gradientAngleDeg,
                    config->m_settings.rotating,
                    config->m_settings.rotationSpeed
                );
            }
            else {
                overlay.SetColor(c1);
            }

            overlay.SetBorderThickness(config->m_settings.borderThickness);

            overlay.PreRender([&] { return !st.stop_requested() && windowAction.load(std::memory_order_acquire) != 0; }, [&] {
                POINT pt = latestMousePos.load(std::memory_order_relaxed);
                short action = windowAction.load(std::memory_order_relaxed);

                RECT newBounds{};
                if (action == 1) {
                    RECT r = overlayBounds.load(std::memory_order_relaxed);
                    int w = r.right - r.left;
                    int h = r.bottom - r.top;
                    newBounds = { pt.x - dragOffset.x, pt.y - dragOffset.y, pt.x - dragOffset.x + w, pt.y - dragOffset.y + h };
                }
                else if (action == 2) {
                    int dx = pt.x - resizeStartCursor.x;
                    int dy = pt.y - resizeStartCursor.y;

                    RECT r = resizeStartRect;
                    RECT nb = r;

                    switch (resizeCorner) {
                    case ResizeCorner::TopLeft:
                    nb.left += dx;
                    nb.top += dy;
                    break;
                    case ResizeCorner::TopRight:
                    nb.right += dx;
                    nb.top += dy;
                    break;
                    case ResizeCorner::BottomLeft:
                    nb.left += dx;
                    nb.bottom += dy;
                    break;
                    case ResizeCorner::BottomRight:
                    nb.right += dx;
                    nb.bottom += dy;
                    break;
                    default:
                    break;
                    }

                    // Clamp width/height
                    int newW = nb.right - nb.left;
                    int newH = nb.bottom - nb.top;

                    if (newW < minSize.cx) {
                        if (resizeCorner == ResizeCorner::TopLeft || resizeCorner == ResizeCorner::BottomLeft)
                            nb.left = nb.right - minSize.cx;
                        else
                            nb.right = nb.left + minSize.cx;
                    }
                    if (newH < minSize.cy) {
                        if (resizeCorner == ResizeCorner::TopLeft || resizeCorner == ResizeCorner::TopRight)
                            nb.top = nb.bottom - minSize.cy;
                        else
                            nb.bottom = nb.top + minSize.cy;
                    }

                    if (maxSize.cx > 0 && (nb.right - nb.left) > maxSize.cx) {
                        if (resizeCorner == ResizeCorner::TopLeft || resizeCorner == ResizeCorner::BottomLeft)
                            nb.left = nb.right - maxSize.cx;
                        else
                            nb.right = nb.left + maxSize.cx;
                    }

                    if (maxSize.cy > 0 && (nb.bottom - nb.top) > maxSize.cy) {
                        if (resizeCorner == ResizeCorner::TopLeft || resizeCorner == ResizeCorner::TopRight)
                            nb.top = nb.bottom - maxSize.cy;
                        else
                            nb.bottom = nb.top + maxSize.cy;
                    }

                    newBounds = nb;
                }

                overlayBounds.store(newBounds, std::memory_order_relaxed);
                RECT renderRect = {
                    newBounds.left + overlayVisualOffset.left,
                    newBounds.top + overlayVisualOffset.top,
                    newBounds.right + overlayVisualOffset.right,
                    newBounds.bottom + overlayVisualOffset.bottom
                };

                overlay.Move(renderRect.left, renderRect.top);
                overlay.Resize(renderRect.right - renderRect.left, renderRect.bottom - renderRect.top);
            });

            overlay.Hide();
        }
    }

    void MouseManager::InputLoop(std::stop_token st) {
        SET_THREAD_NAME("Mouse Input");
        while (!st.stop_requested()) {
            std::unique_lock lock(cvMutex);
            cv.wait(lock, [&] {
                return st.stop_requested() || (hookHandle && !mouseQueue.empty());
            });

            if (st.stop_requested() || !hookHandle) continue;

            while (!mouseQueue.empty()) {
                WPARAM wp;
                if (mouseQueue.pop(wp)) {
                    ProcessMouse(wp);
                }
            }
        }
    }

    void MouseManager::HookLoop(std::stop_token st) {
        hookThreadId = GetCurrentThreadId();
        SET_THREAD_NAME("Mouse Hook");

        while (!st.stop_requested()) {
            {
                std::unique_lock lock(hookCvMutex);
                hookCv.wait(lock, [&] {
                    return st.stop_requested() || installHookRequested;
                });

                if (st.stop_requested()) break;

                if (installHookRequested && !hookHandle) {
                    HOOK_INSTALL();
                    hookHandle = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, nullptr, 0);
                    installHookRequested = false;
                    uninstallHookRequested = false;
                }
            }

            if (!hookHandle) continue;

            MSG msg;
            while (!st.stop_requested() && !uninstallHookRequested) {
                BOOL result = GetMessage(&msg, nullptr, 0, 0);
                if (result <= 0) break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (hookHandle) {
                HOOK_REMOVE();
                UnhookWindowsHookEx(hookHandle);
                hookHandle = nullptr;
            }

            const int buttons[] = { VK_LBUTTON, VK_RBUTTON };
            const DWORD flags[] = { MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP };

            for (int i = 0; i < 2; ++i) {
                if (GetAsyncKeyState(buttons[i]) & 0x8000) {
                    INPUT in{};
                    in.type = INPUT_MOUSE;
                    in.mi.dwFlags = flags[i];
                    SendInput(1, &in, sizeof(in));
                }
            }

            uninstallHookRequested = false;
        }
    }

    void MouseManager::ProcessMouse(WPARAM wp) {
        const POINT pt = latestMousePos.load(std::memory_order_relaxed);

        switch (wp) {
        case WM_RBUTTONDOWN:
        case WM_LBUTTONDOWN:
        if (windowAction == 0) {
            HWND parent = utils::GetFilteredWindow(pt);
            if (!parent) break;
            targetWindow = parent;

            utils::logWindowData(targetWindow);

            RECT windowRect{};
            GetWindowRect(targetWindow, &windowRect);

            if (helpers::mon::IsBorderlessFullscreen(targetWindow, windowRect)) {
                LOG("NOT RESIZABLE/MOVABLE - BORDERLESS FULLSCREEN");
                break;
            }

            helpers::dwm::SetFocusToWindow(targetWindow);
            if (wp == WM_RBUTTONDOWN) {
                LONG_PTR style = GetWindowLongPtrW(targetWindow, GWL_STYLE);
                if ((style & WS_THICKFRAME) == 0) {
                    LOG("NOT RESIZABLE");
                    break;
                }

                MINMAXINFO mmi = {};
                SendMessageW(targetWindow, WM_GETMINMAXINFO, 0, (LPARAM)&mmi);

                minSize.cx = mmi.ptMinTrackSize.x > 0 ? mmi.ptMinTrackSize.x : 100;
                minSize.cy = mmi.ptMinTrackSize.y > 0 ? mmi.ptMinTrackSize.y : 38;

                maxSize.cx = mmi.ptMaxTrackSize.x > 0 ? mmi.ptMaxTrackSize.x : INT_MAX;
                maxSize.cy = mmi.ptMaxTrackSize.y > 0 ? mmi.ptMaxTrackSize.y : INT_MAX;

                LOG("MinMAX: %d x %d, %d x %d", minSize.cx, minSize.cy, maxSize.cx, maxSize.cy);
            }

            if (IsZoomed(targetWindow)) {
                // Save anchor before restore
                double anchorXPercent = static_cast<double>(pt.x - windowRect.left) / (windowRect.right - windowRect.left);
                double anchorYPercent = static_cast<double>(pt.y - windowRect.top) / (windowRect.bottom - windowRect.top);

                ShowWindow(targetWindow, SW_RESTORE);
                GetWindowRect(targetWindow, &windowRect); // post-restore

                int width = windowRect.right - windowRect.left;
                int height = windowRect.bottom - windowRect.top;

                dragOffset.x = static_cast<int>(width * anchorXPercent);
                dragOffset.y = static_cast<int>(height * anchorYPercent);

                int newLeft = pt.x - dragOffset.x;
                int newTop = pt.y - dragOffset.y;

                SetWindowPos(targetWindow, nullptr, newLeft, newTop, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
                GetWindowRect(targetWindow, &windowRect); // final post-restore
            }

            if (wp == WM_LBUTTONDOWN) {
                dragOffset = { pt.x - windowRect.left, pt.y - windowRect.top };
            }
            else {
                resizeStartCursor = pt;
                resizeStartRect = windowRect;

                if (config->m_settings.resize_corner == ResizeCorner::None) {
                    int xRel = pt.x - windowRect.left;
                    int yRel = pt.y - windowRect.top;
                    int w = windowRect.right - windowRect.left;
                    int h = windowRect.bottom - windowRect.top;

                    float xRatio = static_cast<float>(xRel) / w;
                    float yRatio = static_cast<float>(yRel) / h;

                    if (xRatio < 0.5f) {
                        resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopLeft : ResizeCorner::BottomLeft;
                    }
                    else {
                        resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopRight : ResizeCorner::BottomRight;
                    }
                }
                else {
                    resizeCorner = config->m_settings.resize_corner;
                }

                MINMAXINFO mmi = {};
                SendMessageW(targetWindow, WM_GETMINMAXINFO, 0, (LPARAM)&mmi);
                minSize.cx = mmi.ptMinTrackSize.x > 0 ? mmi.ptMinTrackSize.x : 100;
                minSize.cy = mmi.ptMinTrackSize.y > 0 ? mmi.ptMinTrackSize.y : 38;
                maxSize.cx = mmi.ptMaxTrackSize.x > 0 ? mmi.ptMaxTrackSize.x : INT_MAX;
                maxSize.cy = mmi.ptMaxTrackSize.y > 0 ? mmi.ptMaxTrackSize.y : INT_MAX;
            }

            overlayBounds.store(windowRect, std::memory_order_relaxed);

            RECT visual{};
            if (SUCCEEDED(DwmGetWindowAttribute(targetWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &visual, sizeof(visual)))) {
                overlayVisualOffset.left = visual.left - windowRect.left;
                overlayVisualOffset.top = visual.top - windowRect.top;
                overlayVisualOffset.right = visual.right - windowRect.right;
                overlayVisualOffset.bottom = visual.bottom - windowRect.bottom;
            }
            else {
                overlayVisualOffset = {};
            }

            windowAction.store((wp == WM_LBUTTONDOWN) ? 1 : 2, std::memory_order_release);
            overlayCv.notify_one();
        }
        break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        short action = windowAction.load(std::memory_order_acquire);
        if (targetWindow && ((action == 1 && wp == WM_LBUTTONUP) || (action == 2 && wp == WM_RBUTTONUP))) {
            RECT r = overlayBounds.load(std::memory_order_relaxed);
            SetWindowPos(targetWindow, nullptr, r.left, r.top,
                r.right - r.left, r.bottom - r.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            windowAction.store(0, std::memory_order_release);
            targetWindow = nullptr;
        }

        break;
        }
    }
    //

    //
}