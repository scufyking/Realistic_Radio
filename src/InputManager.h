// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Config.h"
#include <functional>

// Polls keyboard (GetAsyncKeyState) and a DirectInput/WinMM joystick (joyGetPosEx) from the radio thread.
// The SCS SDK input API only lets plugins *provide* input devices to the game; it cannot report key presses,
// so polling is the dependable native replacement for the server's global keyboard hook / SharpDX joystick.
class InputManager {
public:
    void Init(const Config& cfg);
    void Poll(const std::function<void(Action)>& fire);
    void Resync();          // take the current key state as "already pressed" (no action for keys still held)
    int PollNewButton();    // key menu: number of a controller button pressed since the last call, -1 = none
private:
    bool GameFocused() const;
    Config cfg_;
    bool keyDown_[(int)Action::Count] = {};
    DWORD lastButtons_ = 0;
    bool joyOk_ = true;
    ULONGLONG nextJoyRetry_ = 0;
};
