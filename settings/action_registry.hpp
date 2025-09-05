// action_registry.hpp
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <array>
#include <algorithm>
#include "action_types.hpp"
#include "dispatcher.hpp"
#include "parser.hpp"

// Parsers

inline std::optional<std::monostate>
ParseNone(const std::vector<std::string>&, std::string& extra) {
    extra.clear();
    return std::monostate{};
}

inline std::optional<SendWinComboParams>
ParseWinCombo(const std::vector<std::string>& p, std::string& extra) {
    if (p.size() < 2) return std::nullopt;
    UINT vk = parse::VK(p[1]);
    bool shift = p.size() > 2 && parse::Bool(p[2]);
    extra = std::format(" vk={} shift={}", parse::VKToString(vk), shift);
    return SendWinComboParams{ vk, shift };
}

// parser for: IPCMessage, 0xHEX_CMD, MSG_NAME, WINDOW_CLASS
inline std::optional<IPCMessageParams>
ParseIPCMessage(const std::vector<std::string>& p, std::string& extra) {
    if (p.size() < 4) return std::nullopt;

    IPCMessageParams out{};
    out.cmd = parse::HexWPARAM(p[1], 0); // returns 0 on parse error
    if (!out.cmd) return std::nullopt;

    out.regMsgName.assign(p[2].begin(), p[2].end());
    out.targetClass.assign(p[3].begin(), p[3].end());
    if (out.regMsgName.empty() || out.targetClass.empty()) return std::nullopt;

    extra = std::format(R"( cmd=0x{:X} msg="{}" class="{}")",
        static_cast<unsigned long long>(out.cmd), p[2], p[3]);
    return out;
}

inline std::optional<RunProcessParams>
ParseRun(const std::vector<std::string>& p, std::string& extra) {
    if (p.size() < 2) return std::nullopt;

    std::wstring path(p[1].begin(), p[1].end());
    bool admin = false;
    size_t argStart = 2;
    if (p.size() > 2 && (p[2] == "0" || p[2] == "1")) {
        admin = (p[2] == "1");
        argStart = 3;
    }
    std::string joined;
    for (size_t i = argStart; i < p.size(); ++i) {
        if (!joined.empty()) joined.push_back(' ');
        joined += p[i];
    }
    std::wstring args;
    if (!joined.empty()) args.assign(joined.begin(), joined.end());

    extra = std::format(R"( path="{}" admin={} args="{}")",
        parse::ToUTF8(path), admin, parse::ToUTF8(args));
    return RunProcessParams{ std::move(path), admin, std::move(args) };
}

inline std::optional<SetResolutionParams>
ParseRes(const std::vector<std::string>& p, std::string& extra) {
    if (p.size() < 2) return std::nullopt;
    SetResolutionParams r = parse::Res(p[1]);
    extra = std::format(" res={}x{}@{}Hz", r.width, r.height, r.hz);
    return r;
}

// Action table: Name, ParamType, ParseFn
#define ACTIONS(X) \
X(KillWindow,            std::monostate,       ParseNone)       \
X(ForceKillWindow,       std::monostate,       ParseNone)       \
X(FullScreen,            std::monostate,       ParseNone)       \
X(FullScreenToggle,      std::monostate,       ParseNone)       \
X(FullScreenPadded,      std::monostate,       ParseNone)       \
X(IPCMessage,            IPCMessageParams,     ParseIPCMessage) \
X(MsgBox,                RunProcessParams,     ParseRun)        \
X(SendWinCombo,          SendWinComboParams,   ParseWinCombo)   \
X(Run,                   RunProcessParams,     ParseRun)        \
X(SetResolution,         SetResolutionParams,  ParseRes)        \
X(CycleAudioDevice,      std::monostate,       ParseNone)       \
X(MoveWindowLeftHalf,    std::monostate,       ParseNone)       \
X(MoveWindowRightHalf,   std::monostate,       ParseNone)       \
X(MoveWindowToLeftMon,   std::monostate,       ParseNone)       \
X(MoveWindowToRightMon,  std::monostate,       ParseNone)       \

// Row and wrappers

struct ActionRow {
    std::string_view name;
    std::optional<ActionParams>(*parse)(const std::vector<std::string>&, std::string&);
    void (*exec)(const ActionParams&, const Settings*);
};

template<class P>
inline std::optional<ActionParams>
WrapParse(std::optional<P>(*pf)(const std::vector<std::string>&, std::string&),
    const std::vector<std::string>& parts, std::string& extra) {
    if (auto r = pf(parts, extra)) return ActionParams{ *r };
    return std::nullopt;
}

template<class P, class F>
inline void WrapExec(const F& f, const ActionParams& ap, const Settings* s) {
    if constexpr (std::is_same_v<P, std::monostate>) {
        if constexpr (std::is_invocable_v<F>) {
            f();                                   // void()
        }
        else if constexpr (std::is_invocable_v<F, const Settings*>) {
            f(s);                                  // void(Settings)
        }
        else if constexpr (std::is_invocable_v<F, const std::monostate&>) {
            f(std::get<std::monostate>(ap));       // void(monostate)
        }
        else if constexpr (std::is_invocable_v<F, const std::monostate&, const Settings*>) {
            f(std::get<std::monostate>(ap), s);    // void(monostate, Settings)
        }
        else {
            static_assert(std::is_void_v<F>, "Unsupported executor for monostate");
        }
    }
    else {
        if constexpr (std::is_invocable_v<F, const P&>) {
            f(std::get<P>(ap));                    // void(P)
        }
        else if constexpr (std::is_invocable_v<F, const P&, const Settings*>) {
            f(std::get<P>(ap), s);                 // void(P, Settings)
        }
        else {
            static_assert(std::is_void_v<F>, "Executor must accept (P) or (P, Settings)");
        }
    }
}

#define MAKE_ROW(Name, P, PFn) \
    ActionRow{ \
        #Name, \
        +[](const std::vector<std::string>& parts, std::string& extra) -> std::optional<ActionParams> { \
            return WrapParse<P>(&PFn, parts, extra); \
        }, \
        +[](const ActionParams& ap, const Settings* s) { \
            WrapExec<P>(dispatcher::Name, ap, s); \
        } \
    },

// Build array (size deduced)
inline const auto kActionRows = std::to_array<ActionRow>({
    ACTIONS(MAKE_ROW)
    });
#undef MAKE_ROW

// Lookup and front-ends
inline uint16_t ActionIdByName(std::string_view s) {
    for (uint16_t i = 0; i < kActionRows.size(); ++i) {
        if (kActionRows[i].name == s) return i;
    }
    return UINT16_MAX;
}

inline const ActionRow* ActionRowById(uint16_t id) {
    return (id < kActionRows.size()) ? &kActionRows[id] : nullptr;
}

inline std::optional<Action> ParseActionFromParts(const std::vector<std::string>& parts,
    std::string& extra) {
    if (parts.empty()) return std::nullopt;
    uint16_t id = ActionIdByName(parts[0]);
    if (id == UINT16_MAX) return std::nullopt;
    if (auto p = kActionRows[id].parse(parts, extra)) {
        return Action{ id, std::move(*p) };
    }
    return std::nullopt;
}

inline void DispatchAction(const Action& a, const Settings* settings) {
    const ActionRow* row = ActionRowById(a.typeId);
    if (row) row->exec(a.params, settings);
}