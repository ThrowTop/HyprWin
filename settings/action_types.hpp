#pragma once

#include <variant>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <windows.h>
#include <d2d1.h>

// ---------- Common small types ----------

// Modifiers bitmask
enum ModMask : uint8_t {
    NONE = 0,
    LSHIFT = 1 << 0, RSHIFT = 1 << 1, SHIFT = LSHIFT | RSHIFT,
    LCTRL = 1 << 2, RCTRL = 1 << 3, CTRL = LCTRL | RCTRL,
    LALT = 1 << 4, RALT = 1 << 5, ALT = LALT | RALT,
};

// Physical key + modifiers
struct KeyEvent {
    UINT    vk{};
    uint8_t modMask{};

    bool operator==(const KeyEvent& o) const noexcept {
        return vk == o.vk && modMask == o.modMask;
    }
};

namespace std {
    template<> struct hash<KeyEvent> {
        size_t operator()(const KeyEvent& k) const noexcept {
            return hash<UINT>{}(k.vk) ^ (hash<uint8_t>{}(k.modMask) << 1);
        }
    };
} // namespace std

// Resize hint
enum class ResizeCorner { None, TopLeft, TopRight, BottomLeft, BottomRight };

// ---------- Action parameter structs ----------

struct SendWinComboParams { UINT vk{}; bool shift = false; };
struct RunProcessParams { std::wstring path; bool ADMIN = false; std::wstring args; };
struct SetResolutionParams { int width{}, height{}, hz{}; };
struct IPCMessageParams {
    WPARAM cmd{};
    std::wstring regMsgName;
    std::wstring targetClass;
};
struct OverlayMsgParams {
    std::string utf8Payload;
};

// Union of all parameter types
using ActionParams = std::variant<
    std::monostate,
    SendWinComboParams,
    RunProcessParams,
    SetResolutionParams,
    IPCMessageParams,
    OverlayMsgParams
>;

// Action = dispatcher type (registry id) + params
struct Action {
    uint16_t    typeId{};
    ActionParams params;
};

// ---------- App settings parsed from [settings] ----------

struct Settings {
    D2D1_COLOR_F color{};       // primary border color
    D2D1_COLOR_F color2{};      // secondary color (for gradient)
    bool  gradient = false;
    bool  rotating = false;
    float rotationSpeed = 120.f;
    float gradientAngleDeg = 0.f;
    float borderThickness = 5.f;

    int padding = 20;

    UINT SUPER = 0;             // required super key (VK)
    ResizeCorner resize_corner = ResizeCorner::None;
};