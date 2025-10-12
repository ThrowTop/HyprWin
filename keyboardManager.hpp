#pragma once
#include <windows.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "lockfreequeue.hpp"

#include "settings/config.hpp"

namespace km {
class KeyboardManager {
  public:
    KeyboardManager(Config* cfg);
    ~KeyboardManager();

    void SetSuperReleasedCallback(std::function<void()> cb);
    void SetSuperPressedCallback(std::function<void()> cb);

  private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) noexcept;
    void InputLoop(std::stop_token st);
    void HookLoop(std::stop_token st);
    void ProcessKey(UINT wp);
    void SeedModifierStates() noexcept;

    inline bool IsKeySet(int vk) const noexcept {
        return (keyBits[vk >> 6] >> (vk & 63)) & 1;
    }

    inline void SetKey(int vk) noexcept {
        keyBits[vk >> 6] |= (1ull << (vk & 63));
    }

    inline void ClearKey(int vk) noexcept {
        keyBits[vk >> 6] &= ~(1ull << (vk & 63));
    }

    inline void ClearAllKeys() noexcept {
        for (auto& bits : keyBits)
            bits = 0;
    }

    static inline KeyboardManager* instance = nullptr;
    Config* config = nullptr;

    std::function<void()> superPressedCallback;
    std::function<void()> superReleasedCallback;

    HHOOK hookHandle = nullptr;
    DWORD hookThreadId = 0;

    std::jthread inputThread;
    std::jthread hookThread;

    std::condition_variable cv;
    std::mutex cvMutex;

    std::condition_variable hookCv;
    std::mutex hookCvMutex;

    bool installHookRequested = false;
    bool uninstallHookRequested = false;

    LockFreeQueue<UINT, 32> keyQueue;
    uint64_t keyBits[4]{};
};
} // namespace km