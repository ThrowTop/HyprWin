#include "pch.hpp"
#include "mouseManager.hpp"
#include "utils/utils.hpp"
#include "overlay.hpp"
#include "settings/config.hpp"
#include "utils/dwm.hpp"
#include "utils/mon.hpp"

#include "tinylog.hpp"

namespace mm {
MouseManager::MouseManager(HINSTANCE hi, Config* cfg) : hInstance(hi), config(cfg) {
    instance = this;

    inputThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();

        InputLoop(st);
    });

    hookThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();

        HookLoop(st);
    });

    overlayThread = std::jthread([this](std::stop_token st) { OverlayLoop(st); });
}

MouseManager::~MouseManager() {
    inputThread.request_stop();
    hookThread.request_stop();
    overlayThread.request_stop();

    UninstallHook();

    cv.notify_all();
    hookCv.notify_all();
    overlayCv.notify_all();
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
    PostThreadMessage(hookThreadId, WM_NULL, 0, 0);

    cv.notify_one();
}

LRESULT CALLBACK MouseManager::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || !instance || !lParam)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    MSLLHOOKSTRUCT* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    switch (wParam) {
        case WM_MOUSEMOVE:
            instance->latestMousePos.store(ms->pt, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, wParam, lParam);

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            instance->lastDownPt.store(ms->pt, std::memory_order_relaxed);
            instance->mouseQueue.push(wParam);
            instance->cv.notify_one();
            return 1;

        case WM_LBUTTONUP: {
            instance->mouseQueue.push(wParam);
            instance->cv.notify_one();
            if (instance->allowLUpPassthrough) {
                instance->allowLUpPassthrough = false;
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }
            return 1;
        }

        case WM_RBUTTONUP: {
            instance->mouseQueue.push(wParam);
            instance->cv.notify_one();
            if (instance->allowRUpPassthrough) {
                instance->allowRUpPassthrough = false;
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }
            return 1;
        }

        case WM_MBUTTONUP:
            return CallNextHookEx(nullptr, code, wParam, lParam);

        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return 1;

        default:
            return CallNextHookEx(nullptr, code, wParam, lParam);
    }
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

        overlayCv.wait(lock, [&] { return st.stop_requested() || windowAction.load(std::memory_order_acquire) != 0; });

        if (st.stop_requested())
            break;

        static HCURSOR g_curSizeAll = LoadCursor(nullptr, IDC_SIZEALL);
        static HCURSOR g_curNWSE = LoadCursor(nullptr, IDC_SIZENWSE);
        static HCURSOR g_curNESW = LoadCursor(nullptr, IDC_SIZENESW);
        static HCURSOR g_curArrow = LoadCursor(nullptr, IDC_ARROW);

        short action = windowAction.load(std::memory_order_acquire);
        if (action == 1) {
            SetCursor(g_curSizeAll);
        } else if (action == 2) {
            switch (resizeCorner) {
                case ResizeCorner::TopLeft:
                case ResizeCorner::BottomRight:
                    SetCursor(g_curNWSE);
                    break;
                case ResizeCorner::TopRight:
                case ResizeCorner::BottomLeft:
                    SetCursor(g_curNESW);
                    break;
                default:
                    SetCursor(g_curArrow);
                    break;
            }
        }

        D2D1_COLOR_F c1 = config->m_settings.color;

        if (config->m_settings.gradient) {
            D2D1_COLOR_F c2 = config->m_settings.color2;

            overlay.SetGradient(c1, c2, config->m_settings.gradientAngleDeg, config->m_settings.rotating, config->m_settings.rotationSpeed);
        } else {
            overlay.SetColor(c1);
        }

        overlay.SetBorderThickness(config->m_settings.borderThickness);

        overlay.PreRender([&] { return !st.stop_requested() && windowAction.load(std::memory_order_acquire) != 0; },
          [&] {
              POINT pt = latestMousePos.load(std::memory_order_relaxed);
              short action = windowAction.load(std::memory_order_relaxed);

              RECT newBounds{};
              if (action == 1) {
                  RECT r = overlayBounds.load(std::memory_order_relaxed);
                  int w = r.right - r.left;
                  int h = r.bottom - r.top;
                  newBounds = {pt.x - dragOffset.x, pt.y - dragOffset.y, pt.x - dragOffset.x + w, pt.y - dragOffset.y + h};
              } else if (action == 2) {
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
              RECT renderRect = {newBounds.left + overlayVisualOffset.left,
                newBounds.top + overlayVisualOffset.top,
                newBounds.right + overlayVisualOffset.right,
                newBounds.bottom + overlayVisualOffset.bottom};

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
        cv.wait(lock, [&] { return st.stop_requested() || !mouseQueue.empty(); });

        if (st.stop_requested())
            continue;

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
            hookCv.wait(lock, [&] { return st.stop_requested() || installHookRequested; });

            if (st.stop_requested())
                break;

            if (installHookRequested && !hookHandle) {
                HOOK_INSTALL();
                hookHandle = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, nullptr, 0);
                installHookRequested = false;
                uninstallHookRequested = false;

                allowLUpPassthrough = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                allowRUpPassthrough = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            }
        }

        if (!hookHandle)
            continue;

        MSG msg;
        while (!st.stop_requested() && !uninstallHookRequested) {
            BOOL result = GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0)
                break;
        }

        if (hookHandle) {
            HOOK_REMOVE();
            UnhookWindowsHookEx(hookHandle);
            hookHandle = nullptr;
        }

        const int buttons[] = {VK_LBUTTON, VK_RBUTTON};
        const DWORD flags[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP};

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
    switch (wp) {
        case WM_RBUTTONDOWN:
        case WM_LBUTTONDOWN:
            if (windowAction.load(std::memory_order_acquire) == 0) {
                const POINT pt = lastDownPt.load(std::memory_order_acquire);
                latestMousePos.store(pt, std::memory_order_relaxed);

                HWND parent = utils::GetFilteredWindow(pt);
                if (!parent)
                    break;
                targetWindow = parent;

                std::wstring name = utils::GetProcessName(targetWindow);
                if (!_wcsicmp(name.c_str(), L"cs2.exe")) {
                    break;
                }

                utils::logWindowData(targetWindow);

                RECT windowRect{};
                if (!utils::dwm::GetWindowRectSafe(targetWindow, windowRect)) {
                    LOG_E("Failed to get window rect for target window");
                    break;
                }

                if (utils::mon::IsBorderlessFullscreen(targetWindow, windowRect)) {
                    LOG_D("BORDERLESS FULLSCREEN");
                    return;
                }

                LOG_D("Target window rect: {}", parse::rectToStr(windowRect));
                utils::dwm::SetFocusToWindow(targetWindow);

                if (wp == WM_RBUTTONDOWN) {
                    LONG_PTR style = GetWindowLongPtrW(targetWindow, GWL_STYLE);
                    if ((style & WS_THICKFRAME) == 0) {
                        LOG_I("NOT RESIZABLE");
                        break;
                    }

                    // min/max via helper
                    RECT mm{};
                    if (utils::dwm::GetMinMax(targetWindow, mm)) {
                        minSize.cx = mm.left;
                        minSize.cy = mm.top;
                        maxSize.cx = mm.right;
                        maxSize.cy = mm.bottom;
                        LOG_T("MinMAX: {}", parse::rectToStr(mm));
                    }
                }

                if (IsZoomed(targetWindow)) {
                    // Save anchor before restore (based on WINDOW rect)
                    const int w0 = windowRect.right - windowRect.left;
                    const int h0 = windowRect.bottom - windowRect.top;
                    if (w0 > 0 && h0 > 0) {
                        const double anchorXPercent = static_cast<double>(pt.x - windowRect.left) / w0;
                        const double anchorYPercent = static_cast<double>(pt.y - windowRect.top) / h0;

                        ShowWindow(targetWindow, SW_RESTORE);

                        if (!utils::dwm::GetWindowRectSafe(targetWindow, windowRect))
                            break;

                        const int width = windowRect.right - windowRect.left;
                        const int height = windowRect.bottom - windowRect.top;

                        dragOffset.x = static_cast<int>(width * anchorXPercent);
                        dragOffset.y = static_cast<int>(height * anchorYPercent);

                        const int newLeft = pt.x - dragOffset.x;
                        const int newTop = pt.y - dragOffset.y;

                        SetWindowPos(targetWindow, nullptr, newLeft, newTop, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

                        if (!utils::dwm::GetWindowRectSafe(targetWindow, windowRect))
                            break;
                    }
                }

                if (wp == WM_LBUTTONDOWN) {
                    dragOffset = {pt.x - windowRect.left, pt.y - windowRect.top};
                } else {
                    resizeStartCursor = pt;
                    resizeStartRect = windowRect;

                    if (config->m_settings.resize_corner == ResizeCorner::None) {
                        const int xRel = pt.x - windowRect.left;
                        const int yRel = pt.y - windowRect.top;
                        const int w = windowRect.right - windowRect.left;
                        const int h = windowRect.bottom - windowRect.top;

                        if (w > 0 && h > 0) {
                            const float xRatio = static_cast<float>(xRel) / w;
                            const float yRatio = static_cast<float>(yRel) / h;

                            if (xRatio < 0.5f) {
                                resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopLeft : ResizeCorner::BottomLeft;
                            } else {
                                resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopRight : ResizeCorner::BottomRight;
                            }
                        } else {
                            // fallback
                            resizeCorner = ResizeCorner::BottomRight;
                        }
                    } else {
                        resizeCorner = config->m_settings.resize_corner;
                    }

                    RECT mm{};
                    if (utils::dwm::GetMinMax(targetWindow, mm)) {
                        minSize.cx = mm.left;
                        minSize.cy = mm.top;
                        maxSize.cx = mm.right;
                        maxSize.cy = mm.bottom;
                    }
                }

                overlayBounds.store(windowRect, std::memory_order_relaxed);

                {
                    RECT offs{};
                    if (utils::dwm::GetDwmVisualOffsets(targetWindow, offs)) {
                        overlayVisualOffset = offs;
                    } else {
                        overlayVisualOffset = {};
                    }
                }

                windowAction.store((wp == WM_LBUTTONDOWN) ? 1 : 2, std::memory_order_release);
                overlayCv.notify_one();
            }
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            short action = windowAction.load(std::memory_order_acquire);
            if (targetWindow && ((action == 1 && wp == WM_LBUTTONUP) || (action == 2 && wp == WM_RBUTTONUP))) {
                RECT r = overlayBounds.load(std::memory_order_relaxed);

                SetWindowPos(targetWindow, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);

                windowAction.store(0, std::memory_order_release);
                targetWindow = nullptr;
            }
            break;
        }

        default:
            break;
    }
}
} // namespace mm