#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "overlay.hpp"
#include "settings/config.hpp"
#include "settings/action_types.hpp"

enum class OverlayAction { None, Move, Resize };

struct OverlayState {
    OverlayAction action = OverlayAction::None;
    RECT windowBounds{};
    RECT visualOffset{};
    POINT dragOffset{};
    POINT resizeStartCursor{};
    RECT resizeStartRect{};
    ResizeCorner resizeCorner = ResizeCorner::BottomRight;
    SIZE minSize = {1, 1};
    SIZE maxSize = {INT_MAX, INT_MAX};
};

class OverlayController {
  public:
    OverlayController(HINSTANCE hi, Config* cfg, std::atomic<POINT>* mousePos);
    ~OverlayController();

    void UpdateState(const OverlayState& state);
    void ClearState();

    RECT GetLatestBounds() const { return overlayBounds.load(std::memory_order_relaxed); }
    bool IsActive() const { return currentAction.load(std::memory_order_acquire) != OverlayAction::None; }

  private:
    void OverlayLoop(std::stop_token st);

    HINSTANCE hInstance;
    Config* config = nullptr;
    std::atomic<POINT>* latestMousePos = nullptr;

    std::jthread overlayThread;

    std::condition_variable overlayCv;
    std::mutex overlayCvMutex;

    OverlayState overlayState{};
    std::atomic<OverlayAction> currentAction{OverlayAction::None};
    std::atomic<RECT> overlayBounds{};
};

