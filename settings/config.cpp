// config.cpp
#include <pch.hpp>

#include "config.hpp"
#include "action_registry.hpp" // ParseActionFromParts, DispatchAction, ActionRow lookup
#include "dispatcher.hpp"
#include "parser.hpp" // parse::VK, VKToString, SplitAndTrimParts, Trim, Bool, Int, Res, Color

#include "../utils/utils.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

// Default config
static constexpr const char* default_config = R"(
#	Warning: Non standard ini file, just for syntax highlighting
#   All binds require the super key to be held
#
#	Dispatchers:
#
#	KillWindow
#	FullScreen
#	FullScreenPadded
#	MsgBox "MSG", "TITLE"   (DEBUG)
#	SendWinCombo
#	Run
#	SetResolution
#	CycleAudioDevice
#
#	MoveWindowLeftHalf
#	MoveWindowRightHalf
#	MoveWindowToLeftMon
#	MoveWindowToRightMon
#
#
#   SendWinCombo		<key> [,shift(1/0)]
#		- Sends Windows Key + <key> event
#
#   Run					<path> [,admin(1/0), args]
#		- Runs exe <path> with as admin or as user with [args]
#
#   SetResolution		<width>x<height>@<RefreshRateHz>    # 1920x1080@360
#		- Sets display resolution of main monitor
#
#   CycleAudioDevice
#		- Cycles enabled playback devices
#
#	Modifiers:
#	SHIFT LSHIFT RSHIFT
#	CONTROL LCONTROL RCONTROL
#	MENU LMENU RMENU | ALT -> MENU
#
#	Format:
#	[Modifier+] <Key> = <Dispatcher> [,arg1, arg2...]
#	HEXCOLOR = 00FF00 -> RED 0: GREEN: 255 BLUE: 0
#
#	[settings]
#	SUPER = VK_KEY required
#	COLOR = <HEXCOLOR> [, HEXCOLOR Gradient, GradientAngle:float(ignored if rotating), isRotating:bool, rotationSpeed deg/s:float]

[settings]
SUPER = LWIN # REQUIRED
COLOR = 00a2ff, ff00f7, 45, true, 120
BORDER = 3
RESIZE_CORNER = BOTTOMRIGHT # CLOSEST TOPLEFT TOPRIGHT BOTTOMLEFT BOTTOMRIGH
PADDING = 16

[binds]
Q = KillWindow
SHIFT+Q = ForceKillWindow

SHIFT+F = FullScreen
F = FullScreenPadded
V = SendWinCombo, V
E = SendWinCombo, E
R = SendWinCombo, R
D = SendWinCombo, D
X = SendWinCombo, X
PERIOD = SendWinCombo, PERIOD

SPACE = SendWinCombo, SPACE

# Half screen moves
LEFT = MoveWindowLeftHalf
RIGHT = MoveWindowRightHalf

# Move monitors
LSHIFT+LEFT = MoveWindowToLeftMon
LSHIFT+RIGHT = MoveWindowToRightMon
LSHIFT+LEFT = FullScreenPadded
LSHIFT+RIGHT = FullScreenPadded

F7 = CycleAudioDevice
F1 = MsgBox, Hello World,Wow

RETURN = Run, wt.exe
LSHIFT+RETURN = Run, wt.exe, 1
SHIFT+F6 = SetResolution, 1440x1080@360
SHIFT+F7 = SetResolution, 1920x1080@240
)";

// Utilities

static std::vector<KeyEvent> ExpandLeftRightModifiers(const KeyEvent& base) {
    std::vector<KeyEvent> out;
    const uint8_t baseMods = base.modMask & ~(SHIFT | CTRL | ALT);

    auto choose = [](uint8_t mask, uint8_t L, uint8_t R) -> std::vector<uint8_t> {
        if ((mask & (L | R)) == (L | R))
            return {L, R}; // generic token -> expand
        if (mask & L)
            return {L}; // specific L only
        if (mask & R)
            return {R}; // specific R only
        return {0};     // none
    };

    const auto sVar = choose(base.modMask, LSHIFT, RSHIFT);
    const auto cVar = choose(base.modMask, LCTRL, RCTRL);
    const auto aVar = choose(base.modMask, LALT, RALT);

    for (auto s : sVar)
        for (auto c : cVar)
            for (auto a : aVar)
                out.push_back(KeyEvent{base.vk, static_cast<uint8_t>(baseMods | s | c | a)});

    return out;
}

// Settings parsers
using SettingParser = std::function<void(Settings&, const std::string&)>;
static const std::unordered_map<std::string, SettingParser> g_settingParsers = {
  {"COLOR",
    [](Settings& s, const std::string& val) {
        auto parts = parse::SplitAndTrimParts(val);

        size_t i = 0;

        if (parts.size() > i)
            s.color = parse::Color(parts[i++]);
        if (parts.size() > i)
            s.color2 = parse::Color(parts[i++]);
        if (parts.size() > i)
            s.gradientAngleDeg = parse::Float(parts[i++]);
        if (parts.size() > i)
            s.rotating = parse::Bool(parts[i++]);
        if (parts.size() > i)
            s.rotationSpeed = parse::Float(parts[i++]);

        s.gradient = parts.size() >= 2;
    }},
  {"SUPER", [](Settings& s, const std::string& val) { s.SUPER = parse::VK(val); }},
  {"PADDING", [](Settings& s, const std::string& val) { s.padding = parse::Int(val); }},
  {"BORDER", [](Settings& s, const std::string& val) { s.borderThickness = parse::Float(val); }},
  {"RESIZE_CORNER",
    [](Settings& s, const std::string& val) {
        std::string v = val;
        parse::ToUpper(v);
        static const std::unordered_map<std::string, ResizeCorner> map{{"CLOSEST", ResizeCorner::None},
          {"TOPLEFT", ResizeCorner::TopLeft},
          {"TOPRIGHT", ResizeCorner::TopRight},
          {"BOTTOMLEFT", ResizeCorner::BottomLeft},
          {"BOTTOMRIGHT", ResizeCorner::BottomRight}};
        if (auto it = map.find(v); it != map.end())
            s.resize_corner = it->second;
    }},
};

// Config::LoadConfig
bool Config::LoadConfig(const std::string& filename) {
    LOG_I("Loading config file: {}", filename);
    std::ifstream file(filename);
    std::istringstream fallback(default_config);
    std::istream* in = nullptr;

    m_keybinds.clear();
    m_settings = {};

    if (file.is_open()) {
        in = &file;
    } else {
        LOG_I("Config not found. Creating default: {}", filename);
        if (std::ofstream out(filename); out.is_open()) {
            out << default_config;
            LOG_I("Default config written.");
        } else {
            LOG_C("Failed to write default config: {}", filename);
        }
        MessageBoxA(nullptr, "Default config created.", "Config Initialized", MB_OK | MB_ICONINFORMATION);
        in = &fallback;
    }

    enum class Section { None, Binds, Settings } current = Section::None;
    std::string raw;
    while (std::getline(*in, raw)) {
        std::string line = parse::Trim(raw);
        if (line.empty())
            continue;

        if (line == "[binds]") {
            current = Section::Binds;
            continue;
        }
        if (line == "[settings]") {
            current = Section::Settings;
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string keyStr = parse::Trim(line.substr(0, eq));
        std::string valueStr = parse::Trim(line.substr(eq + 1));

        switch (current) {
            case Section::Binds: {
                KeyEvent keyEvent{};
                if (!ParseKeyWithModifiers(keyStr, keyEvent))
                    break;

                auto parts = parse::SplitAndTrimParts(valueStr);
                if (parts.empty())
                    break;

                // Do NOT uppercase action name; table is case-sensitive: "FullScreen", "Run", etc.
                // parse::ToUpper(parts[0]); // removed

                std::string info;
                auto act = ParseActionFromParts(parts, info);
                if (!act)
                    break;

                for (const KeyEvent& k : ExpandLeftRightModifiers(keyEvent)) {
                    auto& vec = m_keybinds[k];
                    const std::string keyName = parse::VKToString(k.vk);
                    const std::string modMaskStr = ModMaskToString(k.modMask);
                    if (vec.full()) {
                        LOG_W("Key Combo: {}{} Already Has 4 Dispatchers", modMaskStr, keyName);
                    } else {
                        vec.push_back(*act); // returns false if >4;

                        LOG_CONFIG("Bind: {}{} -> {} {}", modMaskStr, keyName, parts[0], (info.empty() ? "" : info));
                    }
                }
            } break;

            case Section::Settings: {
                parse::ToUpper(keyStr);
                if (auto it = g_settingParsers.find(keyStr); it != g_settingParsers.end()) {
                    it->second(m_settings, valueStr);
                }
            } break;

            default:
                break;
        }
    }
    // replace: return m_settings.SUPER != 0;

    if (m_settings.SUPER == 0) {
        LOG_E("SUPER not set (0).");
        return false;
    }

    // Disallow Ctrl/Alt/Shift (generic or L/R variants)
    switch (m_settings.SUPER) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            LOG_E("Invalid SUPER: modifiers (Ctrl/Alt/Shift, any side) are not allowed. VK={}", (unsigned)m_settings.SUPER);
            return false;
        default:
            break;
    }

    return true;
}

// Helpers
bool Config::ParseKeyWithModifiers(const std::string& s, KeyEvent& out) {
    out.modMask = 0;
    std::string keyStr = s;
    size_t pos;
    while ((pos = keyStr.find('+')) != std::string::npos) {
        std::string mod = keyStr.substr(0, pos);
        parse::ToUpper(mod);
        if (mod == "SHIFT")
            out.modMask |= SHIFT;
        else if (mod == "LSHIFT")
            out.modMask |= LSHIFT;
        else if (mod == "RSHIFT")
            out.modMask |= RSHIFT;
        else if (mod == "CTRL" || mod == "CONTROL")
            out.modMask |= CTRL;
        else if (mod == "LCTRL" || mod == "LCONTROL")
            out.modMask |= LCTRL;
        else if (mod == "RCTRL" || mod == "RCONTROL")
            out.modMask |= RCTRL;
        else if (mod == "ALT" || mod == "MENU")
            out.modMask |= ALT;
        else if (mod == "LALT" || mod == "LMENU")
            out.modMask |= LALT;
        else if (mod == "RALT" || mod == "RMENU")
            out.modMask |= RALT;
        else
            return false;
        keyStr.erase(0, pos + 1);
    }
    parse::ToUpper(keyStr);
    out.vk = parse::VK(keyStr);
    return out.vk != 0;
}