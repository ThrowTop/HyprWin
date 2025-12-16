#include "pch.hpp"
#include "overlayController.hpp"

#include "tinylog.hpp"

OverlayController::OverlayController(HINSTANCE hi, Config* cfg, std::atomic<POINT>* mousePos)
  : hInstance(hi), config(cfg), latestMousePos(mousePos) {
    overlayThread = std::jthread([this](std::stop_token st) { OverlayLoop(st); });
}

OverlayController::~OverlayController() {
    if (overlayThread.joinable()) {
        overlayThread.request_stop();
        overlayCv.notify_one();
    }
}

void OverlayController::UpdateState(const OverlayState& state) {
    {
        std::scoped_lock lock(overlayCvMutex);
        overlayState = state;
        currentAction.store(state.action, std::memory_order_release);
        overlayBounds.store(state.windowBounds, std::memory_order_relaxed);
    }
    overlayCv.notify_one();
}

void OverlayController::ClearState() {
    currentAction.store(OverlayAction::None, std::memory_order_release);
    overlayCv.notify_one();
}

void OverlayController::OverlayLoop(std::stop_token st) {
    OverlayWindow overlay;
    overlay.Init(hInstance);
    SET_THREAD_NAME("Overlay");

    std::unique_lock lock(overlayCvMutex);
    while (!st.stop_requested()) {
        MSG msg;
        while (PeekMessageW(&msg, overlay.GetHwnd(), 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        overlayCv.wait(lock, [&] { return st.stop_requested() || currentAction.load(std::memory_order_acquire) != OverlayAction::None; });

        if (st.stop_requested())
            break;

        OverlayState state = overlayState;
        lock.unlock();

        static HCURSOR g_curSizeAll = LoadCursor(nullptr, IDC_SIZEALL);
        static HCURSOR g_curNWSE = LoadCursor(nullptr, IDC_SIZENWSE);
        static HCURSOR g_curNESW = LoadCursor(nullptr, IDC_SIZENESW);
        static HCURSOR g_curArrow = LoadCursor(nullptr, IDC_ARROW);

        if (state.action == OverlayAction::Move) {
            SetCursor(g_curSizeAll);
        } else if (state.action == OverlayAction::Resize) {
            switch (state.resizeCorner) {
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

        overlay.PreRender([&] { return !st.stop_requested() && currentAction.load(std::memory_order_acquire) != OverlayAction::None; },
          [&] {
              if (!latestMousePos)
                  return;

              POINT pt = latestMousePos->load(std::memory_order_relaxed);

              RECT newBounds{};
              if (state.action == OverlayAction::Move) {
                  RECT r = state.windowBounds;
                  int w = r.right - r.left;
                  int h = r.bottom - r.top;
                  newBounds = {pt.x - state.dragOffset.x, pt.y - state.dragOffset.y, pt.x - state.dragOffset.x + w, pt.y - state.dragOffset.y + h};
              } else if (state.action == OverlayAction::Resize) {
                  int dx = pt.x - state.resizeStartCursor.x;
                  int dy = pt.y - state.resizeStartCursor.y;

                  RECT r = state.resizeStartRect;
                  RECT nb = r;

                  switch (state.resizeCorner) {
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

                  if (newW < state.minSize.cx) {
                      if (state.resizeCorner == ResizeCorner::TopLeft || state.resizeCorner == ResizeCorner::BottomLeft)
                          nb.left = nb.right - state.minSize.cx;
                      else
                          nb.right = nb.left + state.minSize.cx;
                  }
                  if (newH < state.minSize.cy) {
                      if (state.resizeCorner == ResizeCorner::TopLeft || state.resizeCorner == ResizeCorner::TopRight)
                          nb.top = nb.bottom - state.minSize.cy;
                      else
                          nb.bottom = nb.top + state.minSize.cy;
                  }

                  if (state.maxSize.cx > 0 && (nb.right - nb.left) > state.maxSize.cx) {
                      if (state.resizeCorner == ResizeCorner::TopLeft || state.resizeCorner == ResizeCorner::BottomLeft)
                          nb.left = nb.right - state.maxSize.cx;
                      else
                          nb.right = nb.left + state.maxSize.cx;
                  }

                  if (state.maxSize.cy > 0 && (nb.bottom - nb.top) > state.maxSize.cy) {
                      if (state.resizeCorner == ResizeCorner::TopLeft || state.resizeCorner == ResizeCorner::TopRight)
                          nb.top = nb.bottom - state.maxSize.cy;
                      else
                          nb.bottom = nb.top + state.maxSize.cy;
                  }

                  newBounds = nb;
              }

              overlayBounds.store(newBounds, std::memory_order_relaxed);
              RECT renderRect = {newBounds.left + state.visualOffset.left,
                newBounds.top + state.visualOffset.top,
                newBounds.right + state.visualOffset.right,
                newBounds.bottom + state.visualOffset.bottom};

              overlay.Move(renderRect.left, renderRect.top);
              overlay.Resize(renderRect.right - renderRect.left, renderRect.bottom - renderRect.top);
          });

        overlay.Hide();

        lock.lock();
    }
}

