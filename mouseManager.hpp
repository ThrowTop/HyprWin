#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "lockfreequeue.hpp"
#include "settings/config.hpp"

namespace mm {
inline std::atomic<short> windowAction = 0;

class MouseManager {
  public:
    MouseManager(HINSTANCE hi, Config* cfg);
    ~MouseManager();

    void InstallHook();
    void UninstallHook();

  private:
    static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
    void InputLoop(std::stop_token st);
    void HookLoop(std::stop_token st);
    void OverlayLoop(std::stop_token st);
    void ProcessMouse(WPARAM wp);

    static inline MouseManager* instance = nullptr;
    Config* config = nullptr;

    HHOOK hookHandle = nullptr;
    DWORD hookThreadId = 0;

    std::jthread inputThread;
    std::jthread hookThread;
    std::jthread overlayThread;

    std::stop_token inputToken;
    std::stop_token hookToken;
    std::stop_token overlayToken;

    std::condition_variable overlayCv;
    std::mutex overlayCvMutex;

    std::condition_variable cv;
    std::mutex cvMutex;

    std::condition_variable hookCv;
    std::mutex hookCvMutex;

    RECT overlayVisualOffset = {}; // left/top/right/bottom deltas relative to real rect

    SIZE minSize = {1, 1};
    SIZE maxSize = {INT_MAX, INT_MAX};

    std::atomic<bool> overlayInitialized{false};
    std::atomic<bool> overlayShouldInit{false};
    std::atomic<RECT> overlayBounds = {};

    std::atomic<POINT> latestMousePos = {POINT{0, 0}};
    std::atomic<POINT> lastDownPt{POINT{0, 0}};

    POINT dragOffset = {};
    POINT resizeStartCursor = {};
    RECT resizeStartRect = {};

    HWND targetWindow = nullptr;

    bool installHookRequested = false;
    bool uninstallHookRequested = false;

    LockFreeQueue<WPARAM, 16> mouseQueue;

    ResizeCorner resizeCorner = ResizeCorner::BottomRight; // default

    HINSTANCE hInstance;
};
} // namespace mm