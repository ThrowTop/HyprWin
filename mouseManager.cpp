#include "pch.hpp"
#include "mouseManager.hpp"
#include "utils/utils.hpp"
#include "overlay.hpp"
#include "overlayController.hpp"
#include "settings/config.hpp"
#include "utils/dwm.hpp"
#include "utils/mon.hpp"

#include "tinylog.hpp"

namespace mm {
MouseManager::MouseManager(HINSTANCE hi, Config* cfg) : hInstance(hi), config(cfg), overlayController(hi, cfg, &latestMousePos) {
    instance = this;

    inputThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();

        InputLoop(st);
    });

    hookThread = std::jthread([this](std::stop_token st) {
        utils::BoostThread();

        HookLoop(st);
    });
}

MouseManager::~MouseManager() {
    inputThread.request_stop();
    hookThread.request_stop();

    UninstallHook();

    cv.notify_all();
    hookCv.notify_all();
}

void MouseManager::InstallHook() {
    {
        std::scoped_lock lock(hookCvMutex);
        installHookRequested = true;
    }

    hookCv.notify_one();
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
            if (!overlayController.IsActive()) {
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

#ifdef _DEBUG
                utils::logWindowData(targetWindow);
#endif
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

                OverlayState state{};
                state.windowBounds = windowRect;
                state.action = (wp == WM_LBUTTONDOWN) ? OverlayAction::Move : OverlayAction::Resize;

                if (wp == WM_RBUTTONDOWN) {
                    LONG_PTR style = GetWindowLongPtrW(targetWindow, GWL_STYLE);
                    if ((style & WS_THICKFRAME) == 0) {
                        LOG_I("NOT RESIZABLE");
                        break;
                    }

                    RECT mm{};
                    if (utils::dwm::GetMinMax(targetWindow, mm)) {
                        state.minSize.cx = mm.left;
                        state.minSize.cy = mm.top;
                        state.maxSize.cx = mm.right;
                        state.maxSize.cy = mm.bottom;
                        LOG_T("MinMAX: {}", parse::rectToStr(mm));
                    }
                }

                if (IsZoomed(targetWindow)) {
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

                        state.dragOffset.x = static_cast<int>(width * anchorXPercent);
                        state.dragOffset.y = static_cast<int>(height * anchorYPercent);

                        const int newLeft = pt.x - state.dragOffset.x;
                        const int newTop = pt.y - state.dragOffset.y;

                        SetWindowPos(targetWindow, nullptr, newLeft, newTop, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

                        if (!utils::dwm::GetWindowRectSafe(targetWindow, windowRect))
                            break;

                        state.windowBounds = windowRect;
                    }
                }

                if (wp == WM_LBUTTONDOWN) {
                    state.dragOffset = {pt.x - windowRect.left, pt.y - windowRect.top};
                } else {
                    state.resizeStartCursor = pt;
                    state.resizeStartRect = windowRect;

                    if (config->m_settings.resize_corner == ResizeCorner::None) {
                        const int xRel = pt.x - windowRect.left;
                        const int yRel = pt.y - windowRect.top;
                        const int w = windowRect.right - windowRect.left;
                        const int h = windowRect.bottom - windowRect.top;

                        if (w > 0 && h > 0) {
                            const float xRatio = static_cast<float>(xRel) / w;
                            const float yRatio = static_cast<float>(yRel) / h;

                            if (xRatio < 0.5f) {
                                state.resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopLeft : ResizeCorner::BottomLeft;
                            } else {
                                state.resizeCorner = (yRatio < 0.5f) ? ResizeCorner::TopRight : ResizeCorner::BottomRight;
                            }
                        } else {
                            state.resizeCorner = ResizeCorner::BottomRight;
                        }
                    } else {
                        state.resizeCorner = config->m_settings.resize_corner;
                    }

                    RECT mm{};
                    if (utils::dwm::GetMinMax(targetWindow, mm)) {
                        state.minSize.cx = mm.left;
                        state.minSize.cy = mm.top;
                        state.maxSize.cx = mm.right;
                        state.maxSize.cy = mm.bottom;
                    }
                }

                RECT offs{};
                if (utils::dwm::GetDwmVisualOffsets(targetWindow, offs)) {
                    state.visualOffset = offs;
                }

                overlayController.UpdateState(state);
            }
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            if (targetWindow && overlayController.IsActive()) {
                RECT r = overlayController.GetLatestBounds();

                SetWindowPos(targetWindow, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);

                overlayController.ClearState();
                targetWindow = nullptr;
            }
            break;
        }

        default:
            break;
    }
}
} // namespace mm