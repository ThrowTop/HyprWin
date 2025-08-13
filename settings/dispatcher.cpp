// dispatcher.cpp
#include <pch.hpp>

#include "action_types.hpp"
#include "dispatcher.hpp"
#include "../utils.hpp"
#include "../audioDeviceManager.hpp"

#include "../helpers/dwm.hpp"
#include "../helpers/mon.hpp"

#include <thread>
#include <Windows.h>
#include <userenv.h>
#pragma comment(lib, "Userenv.lib")

namespace dispatcher {
    void KillWindow() {
        HWND hwnd = utils::GetFilteredWindow();

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return;

        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
            const wchar_t* fname = wcsrchr(path, L'\\');
            fname = fname ? fname + 1 : path;
            if (_wcsicmp(fname, L"obs64.exe") == 0 || _wcsicmp(fname, L"obs32.exe") == 0) {
                CloseHandle(hProc);
                return;
            }
        }
        CloseHandle(hProc);

        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    void ForceKillWindow() {
        HWND hwnd = utils::GetWindow();

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return;

        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 1); // exit code 1
            CloseHandle(hProc);
        }
    }

    void FullScreen() {
        HWND hwnd = utils::GetFilteredWindow();
        if (!hwnd) return;

        ShowWindow(hwnd, SW_MAXIMIZE);
    }

    void FullScreenToggle() {
        HWND hwnd = utils::GetFilteredWindow();
        if (!hwnd) return;

        WINDOWPLACEMENT wp{ sizeof(WINDOWPLACEMENT) };
        if (GetWindowPlacement(hwnd, &wp)) {
            ShowWindow(hwnd, wp.showCmd == SW_SHOWMAXIMIZED ? SW_RESTORE : SW_MAXIMIZE);
        }
    }

    void FullScreenPadded(const Settings* st) {
        HWND hwnd = utils::GetFilteredWindow();
        if (!hwnd) return;

        utils::SetBorderedWindow(hwnd, st->padding);
        return;
    }

    void SendWinCombo(const SendWinComboParams& p) {
        INPUT in[6] = {};
        int i = 0;

        in[i].type = INPUT_KEYBOARD;
        in[i++].ki.wVk = VK_LWIN;

        if (p.shift) {
            in[i].type = INPUT_KEYBOARD;
            in[i++].ki.wVk = VK_SHIFT;
        }

        in[i].type = INPUT_KEYBOARD;
        in[i++].ki.wVk = p.vk;

        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = p.vk;
        in[i++].ki.dwFlags = KEYEVENTF_KEYUP;

        if (p.shift) {
            in[i].type = INPUT_KEYBOARD;
            in[i].ki.wVk = VK_SHIFT;
            in[i++].ki.dwFlags = KEYEVENTF_KEYUP;
        }

        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = VK_LWIN;
        in[i++].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(i, in, sizeof(INPUT));
    }

    bool RunAsAdmin(const RunProcessParams& p) {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.lpVerb = L"runas";
        info.lpFile = p.path.c_str();
        info.lpParameters = p.args.empty() ? nullptr : p.args.c_str();
        info.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&info) != FALSE;
    }

    bool RunAsUser(const RunProcessParams& p) {
        DWORD pid = 0;
        HWND shell = GetShellWindow();
        GetWindowThreadProcessId(shell, &pid);
        if (!pid) return false;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return false;

        HANDLE hToken = nullptr;
        if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
            CloseHandle(hProc);
            return false;
        }

        HANDLE hDup = nullptr;
        if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &hDup)) {
            CloseHandle(hToken);
            CloseHandle(hProc);
            return false;
        }

        std::wstring cmd = L"\"" + p.path + L"\"";
        if (!p.args.empty()) {
            cmd += L" ";
            cmd += p.args;
        }
        LPWSTR cmdLine = cmd.data(); // string lives during the call

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        LPVOID env = nullptr;
        CreateEnvironmentBlock(&env, hDup, FALSE);

        BOOL ok = CreateProcessWithTokenW(
            hDup,
            LOGON_WITH_PROFILE,
            nullptr,
            cmdLine,
            CREATE_UNICODE_ENVIRONMENT,
            env,
            nullptr,
            &si,
            &pi
        );

        if (ok) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }

        if (env) DestroyEnvironmentBlock(env);
        CloseHandle(hDup);
        CloseHandle(hToken);
        CloseHandle(hProc);

        return ok;
    }

    bool Run(const RunProcessParams& p) {
        WLOG(L"Run: path='%s' | admin=%d | args='%s'",
            p.path.c_str(),
            p.ADMIN,
            p.args.c_str());
        return p.ADMIN ? RunAsAdmin(p) : RunAsUser(p);
    }

    void SetResolution(const SetResolutionParams& p) {
        DEVMODE dm = {};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
            LOG("WARNING SHIT IS FUCKED SET DISPLAY");
        }
        dm.dmPelsWidth = p.width;
        dm.dmPelsHeight = p.height;
        dm.dmBitsPerPel = 32;
        dm.dmDisplayFrequency = p.hz;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

        LONG result = ChangeDisplaySettingsExW(NULL, &dm, NULL, CDS_UPDATEREGISTRY | CDS_GLOBAL, NULL);
    }

    void MsgBox(const RunProcessParams& p) {
        std::thread([&] {
            MessageBoxW(nullptr, p.args.c_str(), p.path.c_str(), MB_OK);
        }).detach();
    }

    void CycleAudioDevice() {
        AudioDeviceManager::Instance().cycleToNextDevice();
    }

    void MoveWindow(MoveDir dir, bool toMonitor, int padding) {
        HWND hwnd = utils::GetFilteredWindow();
        if (!hwnd) return;

        RECT wr{}, vrCur{};
        if (!helpers::dwm::GetVisual(hwnd, wr, vrCur)) return;

        const RECT curWork = helpers::mon::GetWorkAreaFromWindow(hwnd);

        WINDOWPLACEMENT wp{ sizeof(wp) };
        GetWindowPlacement(hwnd, &wp);
        const bool wasMax = (wp.showCmd == SW_SHOWMAXIMIZED);

        // Explicit monitor move
        if (toMonitor) {
            const HMONITOR dest = helpers::mon::FindAdjacentMonitorX(hwnd, dir == MoveDir::Right);
            if (!dest) return; // no monitor further in that direction

            const RECT dstWork = helpers::mon::GetWorkArea(dest);

            const LONG dx = vrCur.left - curWork.left;
            const LONG dy = vrCur.top - curWork.top;
            const LONG vw = vrCur.right - vrCur.left;
            const LONG vh = vrCur.bottom - vrCur.top;

            RECT vrNew{
                dstWork.left + dx,
                dstWork.top + dy,
                dstWork.left + dx + vw,
                dstWork.top + dy + vh
            };
            vrNew = utils::ClampRectToWork(vrNew, dstWork);

            if (wasMax) ShowWindow(hwnd, SW_RESTORE);
            helpers::dwm::SetWindowVisualRect(hwnd, vrNew);
            if (wasMax) ShowWindow(hwnd, SW_MAXIMIZE);

            helpers::dwm::CenterCursorInVisual(hwnd);
            return;
        }

        // Half-snap on current monitor (with padding)
        const LONG mid = (curWork.left + curWork.right) / 2;
        const LONG edgePad = padding;
        const LONG centerPad = padding / 2;

        RECT leftHalf{
            curWork.left + edgePad,
            curWork.top + edgePad,
            mid - centerPad,
            curWork.bottom - edgePad
        };
        RECT rightHalf{
            mid + centerPad,
            curWork.top + edgePad,
            curWork.right - edgePad,
            curWork.bottom - edgePad
        };

        // Already snapped to requested side -> try adjacent monitor on closest half, else do nothing
        if ((dir == MoveDir::Left && helpers::mon::RectApproxEq(vrCur, leftHalf)) ||
            (dir == MoveDir::Right && helpers::mon::RectApproxEq(vrCur, rightHalf))) {
            const HMONITOR dest = helpers::mon::FindAdjacentMonitorX(hwnd, dir == MoveDir::Right);
            if (!dest) return; // no monitor in that direction

            const RECT dstWork = helpers::mon::GetWorkArea(dest);
            const LONG dstMid = (dstWork.left + dstWork.right) / 2;

            // Closest half after crossing: move to opposite side on destination monitor
            RECT target{};
            target.top = dstWork.top + edgePad;
            target.bottom = dstWork.bottom - edgePad;

            if (dir == MoveDir::Left) {
                // from left half -> right half of left monitor
                target.left = dstMid + centerPad;
                target.right = dstWork.right - edgePad;
            }
            else {
                // from right half -> left half of right monitor
                target.left = dstWork.left + edgePad;
                target.right = dstMid - centerPad;
            }

            helpers::dwm::SetWindowVisualRect(hwnd, utils::ClampRectToWork(target, dstWork));
            helpers::dwm::CenterCursorInVisual(hwnd);
            return;
        }

        // Snap to requested side on current monitor
        RECT vrTarget = (dir == MoveDir::Left) ? leftHalf : rightHalf;

        if (vrTarget.right < vrTarget.left)  vrTarget.right = vrTarget.left;
        if (vrTarget.bottom < vrTarget.top)   vrTarget.bottom = vrTarget.top;

        if (wasMax) ShowWindow(hwnd, SW_RESTORE);
        helpers::dwm::SetWindowVisualRect(hwnd, vrTarget);
        // do not re-maximize after half-snap
        helpers::dwm::CenterCursorInVisual(hwnd);
    }
} // namespace dispatcher