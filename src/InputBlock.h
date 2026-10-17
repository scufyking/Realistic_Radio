// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"

// Keeps mouse movement and clicks away from the game while the settings menu is open.
// A low-level mouse hook does not stop raw input / DirectInput, which the game uses for the camera, so the
// game's own reads are filtered instead:
//  - GetRawInputData / GetRawInputBuffer: import-table entries of the game's modules are redirected; mouse
//    records are returned with no movement, no buttons and no wheel.
//  - DirectInput 8 (A and W): GetDeviceState / GetDeviceData of mouse devices return an idle mouse.
// Nothing is changed while the menu is closed (the filters pass everything through).
namespace InputBlock {
void Install();              // once, from the radio thread (not from DllMain / game callbacks)
void SetMouseBlocked(bool on);
void AttachWindow(HWND gameWindow);   // also filter WM_INPUT in the game window's message handler (stays installed)
}
