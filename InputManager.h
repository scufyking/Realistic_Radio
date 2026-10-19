// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Config.h"
#include <functional>

// Polls keyboard (GetAsyncKeyState) and every game controller (DirectInput 8: all wheels, button boxes, pads with up to
// 128 buttons and 4 hat switches; WinMM only as a fallback) from the radio thread.
// The SCS SDK input API only lets plugins *provide* input devices to the game; it cannot report key presses,
// so polling is the dependable native replacement for the server's global keyboard hook / SharpDX joystick.
struct IDirectInput8W;
struct IDirectInputDevice8W;

class InputManager {
public:
    ~InputManager();
    void Init(const Config& cfg);
    // fire(action, hold): station buttons fire when released (hold = false) or once when held long enough (hold = true)
    void Poll(const std::function<void(Action, bool)>& fire);
    void Resync();          // take the current key state as "already pressed" (no action for keys still held)
    // key menu: a controller button pressed since the last call (button -1 = none); device = its product name
    struct NewButton { int button = -1; std::string device; };
    NewButton PollNewButton();
private:
    bool GameFocused() const;
    Config cfg_;
    bool keyDown_[(int)Action::Count] = {};
    bool holdDone_[(int)Action::Count] = {};
    ULONGLONG pressAt_[(int)Action::Count] = {};
    DWORD lastButtons_ = 0;
    bool joyOk_ = true;
    ULONGLONG nextJoyRetry_ = 0;
    // DirectInput controllers
    struct Pad { std::string name; IDirectInputDevice8W* dev = nullptr; bool down[144] = {}; bool prev[144] = {}; bool ok = false; };
    std::vector<Pad> pads_;
    IDirectInput8W* di_ = nullptr;
    bool diFailed_ = false;
    ULONGLONG enumAt_ = 0;
    void UpdatePads(ULONGLONG now);   // (re)enumerates now and then and reads all controllers
    void ReleasePads();
    bool PadDown(int binding) const;
};
