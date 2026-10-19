// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "InputManager.h"
#include <mmsystem.h>

void InputManager::Init(const Config& cfg) { cfg_ = cfg; }

bool InputManager::GameFocused() const {
    HWND fg = GetForegroundWindow(); if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();   // the DLL lives inside eurotrucks2.exe
}

static bool ChatMenuInFront() {   // the Trucker Chat menu (separate plugin) is a window of the game process too
    HWND fg = GetForegroundWindow(); if (!fg) return false;
    wchar_t cls[64] = {0}; GetClassNameW(fg, cls, 64);
    return !wcscmp(cls, L"TruckerChatMenu");   // typing in the chat menu must not change the radio
}

static bool Down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

void InputManager::Poll(const std::function<void(Action, bool)>& fire) {
    if ((cfg_.onlyWhenGameFocused && !GameFocused()) || ChatMenuInFront()) { for (auto& k : keyDown_) k = false; for (auto& h : holdDone_) h = true; return; }
    bool pressed[(int)Action::Count] = {};
    for (int i = 0; i < (int)Action::Count; ++i) {
        const KeyBinding& b = cfg_.keys[i];
        pressed[i] = b.vk && Down(b.vk) && Down(VK_CONTROL) == b.ctrl && Down(VK_SHIFT) == b.shift && Down(VK_MENU) == b.alt;
    }
    bool anyButton = false;
    for (int b : cfg_.buttons) if (b >= 0) anyButton = true;
    ULONGLONG now = GetTickCount64();
    if (anyButton && (joyOk_ || now >= nextJoyRetry_)) {
        JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) != JOYERR_NOERROR) { joyOk_ = false; nextJoyRetry_ = now + 3000; }
        else {
            joyOk_ = true;
            for (int i = 0; i < (int)Action::Count; ++i) {
                int b = cfg_.buttons[i];
                if (b >= 0 && b <= 31 && (ji.dwButtons & (1u << b))) pressed[i] = true;
            }
            lastButtons_ = ji.dwButtons;
        }
    }
    const ULONGLONG holdMs = (ULONGLONG)(cfg_.memoryHoldSeconds * 1000);
    for (int i = 0; i < (int)Action::Count; ++i) {
        bool d = pressed[i], was = keyDown_[i];
        keyDown_[i] = d;
        if (!IsMemoryAction((Action)i)) { if (d && !was) fire((Action)i, false); continue; }
        // station button like on a car radio: tap = select, hold = save
        if (d && !was) { pressAt_[i] = now; holdDone_[i] = false; }
        else if (d && !holdDone_[i] && now - pressAt_[i] >= holdMs) { holdDone_[i] = true; fire((Action)i, true); }
        else if (!d && was && !holdDone_[i]) { holdDone_[i] = true; fire((Action)i, false); }
    }
}

void InputManager::Resync() {
    for (int i = 0; i < (int)Action::Count; ++i) {
        const KeyBinding& b = cfg_.keys[i];
        keyDown_[i] = b.vk && Down(b.vk);
        holdDone_[i] = true;   // a key still held from the menu does nothing when released
    }
    JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) == JOYERR_NOERROR) lastButtons_ = ji.dwButtons;
}

int InputManager::PollNewButton() {
    JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) != JOYERR_NOERROR) return -1;
    DWORD fresh = ji.dwButtons & ~lastButtons_;
    lastButtons_ = ji.dwButtons;
    for (int b = 0; b < 32; ++b) if (fresh & (1u << b)) return b;
    return -1;
}
