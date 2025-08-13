#pragma once
#include <cstdint>
#include <algorithm>

template<typename T, uint8_t N = 4>
struct FixedActions {
    T items[N];
    uint8_t count = 0;

    bool push_back(const T& v) {
        if (count >= N) return false;
        items[count++] = v;
        return true;
    }
    bool push_back(T&& v) {
        if (count >= N) return false;
        items[count++] = std::move(v);
        return true;
    }
    void clear() { count = 0; }
    bool empty() const { return count == 0; }
    bool full()  const { return count >= N; }

    T* begin() { return items; }
    T* end() { return items + count; }
    const T* begin() const { return items; }
    const T* end()   const { return items + count; }

    T& operator[](uint8_t i) { return items[i]; }
    const T& operator[](uint8_t i) const { return items[i]; }

    static constexpr uint8_t capacity() { return N; }
};
static_assert(FixedActions<int, 4>::capacity() == 4, "capacity check");