// super_watcher.hpp
// ASCII only

#pragma once
#include <windows.h>
#include <atomic>

namespace super_watcher {
static HHOOK g_hook = nullptr;
static std::atomic<UINT> g_superVk{0};
static bool g_isDown = false;

using hook_fn = void (*)(void*) noexcept;

static hook_fn g_km_install = nullptr;
static hook_fn g_km_uninstall = nullptr;
static hook_fn g_mm_install = nullptr;
static hook_fn g_mm_uninstall = nullptr;
static void* g_km_ctx = nullptr;
static void* g_mm_ctx = nullptr;

static __forceinline bool is_injected(const KBDLLHOOKSTRUCT* k) {
    return (k->flags & LLKHF_INJECTED) != 0;
}

static LRESULT CALLBACK Proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const UINT target = g_superVk.load(std::memory_order_relaxed);

        if (k->vkCode == target && !is_injected(k)) {
            const bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            if (keyDown && !g_isDown) {
                g_isDown = true;
                if (g_km_install)
                    g_km_install(g_km_ctx);
                if (g_mm_install)
                    g_mm_install(g_mm_ctx);
            } else if (keyUp && g_isDown) {
                g_isDown = false;
                if (g_km_uninstall)
                    g_km_uninstall(g_km_ctx);
                if (g_mm_uninstall)
                    g_mm_uninstall(g_mm_ctx);
            }

            return 1;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static bool Install(UINT superVk) {
    if (g_hook)
        return true;
    g_superVk.store(superVk, std::memory_order_relaxed);
    g_isDown = false;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, Proc, GetModuleHandleW(nullptr), 0);
    return g_hook != nullptr;
}

static void SetCallbacks(hook_fn km_i, hook_fn km_u, hook_fn mm_i, hook_fn mm_u, void* km_ctx, void* mm_ctx) noexcept {
    g_km_install = km_i;
    g_km_uninstall = km_u;
    g_mm_install = mm_i;
    g_mm_uninstall = mm_u;
    g_km_ctx = km_ctx;
    g_mm_ctx = mm_ctx;
}

static void SetSuperVk(UINT newVk, bool resetEdge = true) noexcept {
    g_superVk.store(newVk, std::memory_order_relaxed);
    if (resetEdge)
        g_isDown = false;
}

static void ClearCallbacks() noexcept {
    g_km_install = g_km_uninstall = nullptr;
    g_mm_install = g_mm_uninstall = nullptr;
    g_km_ctx = g_mm_ctx = nullptr;
}

static void Uninstall() noexcept {
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    ClearCallbacks();
    g_superVk.store(0, std::memory_order_relaxed);
    g_isDown = false;
}
} // namespace super_watcher
