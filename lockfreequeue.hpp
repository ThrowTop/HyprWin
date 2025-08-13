#pragma once
#include <atomic>

template <typename T, size_t Size>
class LockFreeQueue {
    static_assert((Size& (Size - 1)) == 0, "Size must be a power of 2");

    T buffer[Size]{};
    std::atomic<size_t> head = 0;
    std::atomic<size_t> tail = 0;

public:
    bool push(const T& item) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t next = (t + 1) & (Size - 1);
        if (next == head.load(std::memory_order_acquire))
            return false; // full
        buffer[t] = item;
        tail.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire))
            return false; // empty
        item = buffer[h];
        head.store((h + 1) & (Size - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }
};