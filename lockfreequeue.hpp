#pragma once
#include <atomic>
#include <type_traits>
#include <cstddef>

template <typename T, size_t Size>
class LockFreeQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static_assert(Size >= 2, "Size too small");

    T buffer[Size]{};

    struct alignas(64) AlignedAtomic {
        std::atomic<size_t> v{0};
    };
    AlignedAtomic head_;
    AlignedAtomic tail_;

    static constexpr bool NoThrowCopy = std::is_trivially_copyable_v<T> || std::is_nothrow_copy_assignable_v<T>;

  public:
    bool push(const T& item) noexcept(NoThrowCopy) {
        size_t t = tail_.v.load(std::memory_order_relaxed);
        size_t next = (t + 1) & (Size - 1);
        if (next == head_.v.load(std::memory_order_acquire))
            return false; // full
        if constexpr (std::is_trivially_copyable_v<T>) {
            buffer[t] = item; // trivial
        } else {
            buffer[t] = item; // may throw; guarded by noexcept(NoThrowCopy)
        }
        tail_.v.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept(NoThrowCopy) {
        size_t h = head_.v.load(std::memory_order_relaxed);
        if (h == tail_.v.load(std::memory_order_acquire))
            return false; // empty
        if constexpr (std::is_trivially_copyable_v<T>) {
            item = buffer[h];
        } else {
            item = buffer[h]; // may throw; guarded by noexcept(NoThrowCopy)
        }
        head_.v.store((h + 1) & (Size - 1), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return head_.v.load(std::memory_order_acquire) == tail_.v.load(std::memory_order_acquire);
    }
};
