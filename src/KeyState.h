// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Key state straight from Windows (win32u.dll), past hooks other plugins put on user32's GetAsyncKeyState /
// GetKeyState. SPF Framework (MotionCab and other SPF plugins) hooks these functions and runs its key binding logic
// inside every call: the radio thread polling its keys would feed SPF key events from a second thread and could get
// keys reported as "up" that SPF keeps from the game. Falls back to user32 where win32u.dll does not exist.
#include <windows.h>

inline SHORT RealAsyncKeyState(int vk) {
    using Fn = SHORT(WINAPI*)(int);
    static Fn fn = [] {
        HMODULE m = GetModuleHandleW(L"win32u.dll");
        if (!m) m = LoadLibraryW(L"win32u.dll");
        Fn p = m ? (Fn)GetProcAddress(m, "NtUserGetAsyncKeyState") : nullptr;
        return p ? p : (Fn)&GetAsyncKeyState;
    }();
    return fn(vk);
}
inline SHORT RealKeyState(int vk) {
    using Fn = SHORT(WINAPI*)(int);
    static Fn fn = [] {
        HMODULE m = GetModuleHandleW(L"win32u.dll");
        if (!m) m = LoadLibraryW(L"win32u.dll");
        Fn p = m ? (Fn)GetProcAddress(m, "NtUserGetKeyState") : nullptr;
        return p ? p : (Fn)&GetKeyState;
    }();
    return fn(vk);
}
