// dispatcher.hpp
#pragma once

#include "action_types.hpp"

namespace dispatcher {
    void KillWindow();
    void ForceKillWindow();
    void FullScreen();
    void FullScreenToggle();
    void FullScreenPadded(const Settings* st);
    void SendWinCombo(const SendWinComboParams& p);

    bool RunAsAdmin(const RunProcessParams& p);
    bool RunAsUser(const RunProcessParams& p);
    bool Run(const RunProcessParams& p);

    void SetResolution(const SetResolutionParams& p);
    void CycleAudioDevice();

    void MsgBox(const RunProcessParams& p);

    enum class MoveDir : uint8_t { Left, Right };

    // core
    void MoveWindow(MoveDir dir, bool toMonitor, int padding = 0);

    // wrappers for binds
    inline void MoveWindowLeftHalf(const Settings* st) { MoveWindow(MoveDir::Left, false, st->padding); }
    inline void MoveWindowRightHalf(const Settings* st) { MoveWindow(MoveDir::Right, false, st->padding); }
    inline void MoveWindowToLeftMon(const Settings* st) { MoveWindow(MoveDir::Left, true, st->padding); }
    inline void MoveWindowToRightMon(const Settings* st) { MoveWindow(MoveDir::Right, true, st->padding); }
}