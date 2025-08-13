﻿# HyprWin

HyprWin is a Windows utility for **resizing**, **moving**, and **managing windows** using customizable keyboard shortcuts.  
It supports **modifiers**, **color themes**, and various window control dispatchers.

> ⚠ **Warning:** This project uses a **non-standard INI format** for syntax highlighting purposes.  
> All binds require the **SUPER** key to be held.

## Recommend remapping the windows key to some unused key using your keyboard software. I use `PAUSE` as i have a 75% keyboard. 

---

## Features

- **Customizable Keybinds** with modifiers.
- **Window Control Dispatchers** for movement, resizing, fullscreen, and more.
- **Custom Colors** and gradients for overlays.
- **Resizable Borders** with padding configuration.
- **Multiple Actions** including message boxes, audio device cycling, and running commands.

---


## Dispatchers:
- `KillWindow`
- `ForceKillWindow`
- `FullScreen`
- `FullScreenToggle`
- `FullScreenPadded`
- `MsgBox`
- `SendWinCombo`
- `Run`
- `SetResolution`
- `CycleAudioDevice`
- `MoveWindowLeftHalf`
- `MoveWindowRightHalf`
- `MoveWindowToLeftMon`
- `MoveWindowToRightMon`

### `Modifiers:` 
- SHIFT LSHIFT RSHIFT
- CONTROL LCONTROL RCONTROL
- MENU LMENU RMENU

### Example
```ini
[binds]
Q = KillWindow
SHIFT+Q = ForceKillWindow
SHIFT+F6 = SetResolution 1024x768@360

[settings]
SUPER = PAUSE
COLOR = 00FF00, FF0000, 45.0, 1, 10.0
```
### Format:
```ini
[Modifier+] <Key> = <Dispatcher> [,arg1, arg2...]
HEXCOLOR = 00FF00 -> RED 0: GREEN: 255 BLUE: 0
```
```ini
[settings]
#   SUPER is the key that must be held to use the binds
SUPER = <VK_KEYCODE>
COLOR = <HEXCOLOR>, [<HEXCOLOR>, GradientAngle:float, isRotating:bool, rotationSpeed deg/s:float]
BORDER = 3
RESIZE_CORNER = CLOSEST # CLOSEST TOPLEFT TOPRIGHT BOTTOMLEFT BOTTOMRIGH
PADDING = 16
```
