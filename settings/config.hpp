#pragma once
#include "action_types.hpp"
#include "action_vec.hpp"
#include <unordered_map>
#include <string>

using Actions4 = FixedActions<Action, 4>;

class Config {
public:
    std::unordered_map<KeyEvent, Actions4> m_keybinds;
    Settings m_settings;

    bool LoadConfig(const std::string& filename = "config.ini");

private:
    static bool ParseKeyWithModifiers(const std::string& s, KeyEvent& out);

    inline std::string ModMaskToString(uint8_t m) {
        std::string out;
        auto add = [&](std::string_view s) {
            if (!out.empty()) out.push_back('+');
            out.append(s);
        };
        if (m & LSHIFT) add("LSHIFT");
        if (m & RSHIFT) add("RSHIFT");
        if (m & LCTRL)  add("LCTRL");
        if (m & RCTRL)  add("RCTRL");
        if (m & LALT)   add("LALT");
        if (m & RALT)   add("RALT");
        if (!out.empty()) out.push_back('+');
        return out;
    }
};