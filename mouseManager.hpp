#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "lockfreequeue.hpp"
#include "settings/config.hpp"
#include "overlayController.hpp"

namespace mm {

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
    void ProcessMouse(WPARAM wp);

    static inline MouseManager* instance = nullptr;
    Config* config = nullptr;

    HHOOK hookHandle = nullptr;
    DWORD hookThreadId = 0;

    std::jthread inputThread;
    std::jthread hookThread;

    std::condition_variable cv;
    std::mutex cvMutex;

    std::condition_variable hookCv;
    std::mutex hookCvMutex;

    std::atomic<POINT> latestMousePos = {POINT{0, 0}};
    std::atomic<POINT> lastDownPt{POINT{0, 0}};

    POINT dragOffset = {};
    POINT resizeStartCursor = {};
    RECT resizeStartRect = {};

    HWND targetWindow = nullptr;

    OverlayController overlayController;

    bool installHookRequested = false;
    bool uninstallHookRequested = false;

    bool allowLUpPassthrough = false;
    bool allowRUpPassthrough = false;

    LockFreeQueue<WPARAM, 16> mouseQueue;

    ResizeCorner resizeCorner = ResizeCorner::BottomRight; // default

    HINSTANCE hInstance;
};
} // namespace mm