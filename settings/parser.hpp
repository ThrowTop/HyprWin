#pragma once
#include <windows.h>
#include <d2d1.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include "action_types.hpp"
#include <format>

namespace parse {
    inline void ToUpper(std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    }

    inline std::string rectToStr(const RECT& r) {
        return std::format("({}, {}, {}, {})",
            static_cast<long>(r.left),
            static_cast<long>(r.top),
            static_cast<long>(r.right),
            static_cast<long>(r.bottom));
    }

    inline std::string Trim(const std::string& s) {
        size_t hash = s.find('#');
        size_t end = (hash != std::string::npos) ? hash : s.size();

        size_t first = s.find_first_not_of(" \t");
        if (first == std::string::npos || first >= end) return "";

        size_t last = s.find_last_not_of(" \t", end - 1);
        return s.substr(first, last - first + 1);
    }

    inline std::vector<std::string> SplitAndTrimParts(const std::string& s) {
        std::vector<std::string> out;
        size_t start = 0;

        while (start < s.size()) {
            size_t comma = s.find(',', start);
            size_t end = (comma == std::string::npos) ? s.size() : comma;

            std::string part = s.substr(start, end - start);
            size_t first = part.find_first_not_of(" \t");
            size_t last = part.find_last_not_of(" \t");

            if (first != std::string::npos)
                part = part.substr(first, last - first + 1);
            else
                part.clear();

            out.push_back(std::move(part));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        return out;
    }

    inline int Int(const std::string& s, int fallback = 0) {
        try { return std::stoi(s); }
        catch (...) { return fallback; }
    }

    inline int Hex(const std::string& s, int fallback = 0) {
        try { return std::stoi(s, nullptr, 16); }
        catch (...) { return fallback; }
    }

    inline float Float(const std::string& s, float fallback = 0.0f) {
        try { return std::stof(s); }
        catch (...) { return fallback; }
    }

    inline D2D1_COLOR_F Color(const std::string& hex, float alpha = 1.0f) {
        if (hex.length() != 6) return D2D1::ColorF(0, 0, 0, alpha);
        int r = Hex(hex.substr(0, 2));
        int g = Hex(hex.substr(2, 2));
        int b = Hex(hex.substr(4, 2));
        return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
    }

    inline bool Bool(const std::string& s) {
        std::string u = s;
        ToUpper(u);
        return u == "1" || u == "TRUE";
    }

    inline std::string ToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string str(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), str.data(), size, nullptr, nullptr);
        return str;
    }

    inline WPARAM HexWPARAM(const std::string& tok, WPARAM fallback = 0) {
        if (tok.empty()) return fallback;

        size_t i = 0;
        if (tok.size() >= 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) i = 2;
        if (i >= tok.size()) return fallback;

        unsigned long long v = 0;
        const char* first = tok.data() + i;
        const char* last = tok.data() + tok.size();
        auto [ptr, ec] = std::from_chars(first, last, v, 16);
        if (ec != std::errc() || ptr != last) return fallback;

        return static_cast<WPARAM>(v);
    }

    inline constexpr std::pair<std::string_view, UINT> kVkPairs[] = {
        {"UP",VK_UP},{"DOWN",VK_DOWN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},
        {"HOME",VK_HOME},{"END",VK_END},{"PGUP",VK_PRIOR},{"PGDN",VK_NEXT},
        {"TAB",VK_TAB},{"ESC",VK_ESCAPE},{"RETURN",VK_RETURN},{"BACKSPACE",VK_BACK},
        {"DELETE",VK_DELETE},{"INSERT",VK_INSERT},
        {"LSHIFT",VK_LSHIFT},{"RSHIFT",VK_RSHIFT},{"LCTRL",VK_LCONTROL},{"RCTRL",VK_RCONTROL},
        {"LALT",VK_LMENU},{"RALT",VK_RMENU},
        {"SPACE",VK_SPACE},{"CAPSLOCK",VK_CAPITAL},{"NUMLOCK",VK_NUMLOCK},{"SCROLLLOCK",VK_SCROLL},
        {"PAUSE",VK_PAUSE},{"PRINT",VK_SNAPSHOT},{"APPS",VK_APPS},{"LWIN",VK_LWIN},{"RWIN",VK_RWIN},
        {"PERIOD",VK_OEM_PERIOD},
    };

    // maps built once, O(1) thereafter
    inline const std::unordered_map<std::string_view, UINT>& StrToVk() {
        static const auto m = [] {
            std::unordered_map<std::string_view, UINT> t;
            t.reserve(std::size(kVkPairs));
            for (auto& p : kVkPairs) t.emplace(p.first, p.second);
            return t;
        }();
        return m;
    }
    inline const std::unordered_map<UINT, std::string_view>& VkToStr() {
        static const auto m = [] {
            std::unordered_map<UINT, std::string_view> t;
            t.reserve(std::size(kVkPairs));
            for (auto& p : kVkPairs) t.emplace(p.second, p.first);
            return t;
        }();
        return m;
    }

    inline UINT VK(std::string_view s) {
        if (s.size() == 1) {
            unsigned char c = static_cast<unsigned char>(s[0]);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
        }
        if (auto it = StrToVk().find(s); it != StrToVk().end()) return it->second;
        if (s.size() >= 2 && s[0] == 'F') {
            unsigned v = 0; for (size_t i = 1; i < s.size(); ++i) { char c = s[i]; if (c < '0' || c>'9') return 0; v = v * 10 + (c - '0'); }
            if (v >= 1 && v <= 24) return VK_F1 + (v - 1);
        }
        return 0;
    }

    inline std::string VKToString(UINT vk) {
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
        if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
        if (auto it = VkToStr().find(vk); it != VkToStr().end()) return std::string(it->second);
        char buf[8]; std::snprintf(buf, sizeof(buf), "0x%02X", vk); return buf;
    }

    inline SetResolutionParams Res(const std::string& s) {
        SetResolutionParams r{};
        size_t xPos = s.find('x'), atPos = s.find('@');
        if (xPos != std::string::npos && atPos != std::string::npos && xPos < atPos) {
            try {
                r.width = parse::Int(s.substr(0, xPos));
                r.height = parse::Int(s.substr(xPos + 1, atPos - xPos - 1));
                r.hz = parse::Int(s.substr(atPos + 1));
            }
            catch (...) { }
        }
        return r;
    }
}