// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"

// Keyboard and mouse for the settings menu. While the menu is open, low-level hooks (WH_KEYBOARD_LL and
// WH_MOUSE_LL, on their own thread with a message loop) receive the input and keep it away from the game.
// Mouse movement moves the menu's own cursor (D3D11Overlay::SetCursorPos), the Windows cursor stays where it is.
namespace MenuInput {
struct Event {
    enum Type { Key, MouseDown, MouseUp, Wheel } type = Key;
    int vk = 0; bool ctrl = false, shift = false, alt = false;   // Key
    wchar_t ch = 0;                                              // Key: the character it types (keyboard layout of the game), 0 = none
    int button = 0;                                              // MouseDown/Up: 0 = left, 1 = right, 2 = middle
    int wheel = 0;                                               // Wheel: +1 up, -1 down
};
bool Start(float cursorX, float cursorY);   // false = hooks unavailable: keyboard is polled instead, no mouse
void Stop();
bool Pop(Event& e);
bool MouseActive();
}
