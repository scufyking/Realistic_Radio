// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#define DIRECTINPUT_VERSION 0x0800
#include "InputManager.h"
#include "Logger.h"
#include <mmsystem.h>
#include <dinput.h>

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
    if (anyButton && !diFailed_) {   // all controllers through DirectInput
        UpdatePads(now);
        for (int i = 0; i < (int)Action::Count; ++i) if (cfg_.buttons[i] >= 0 && PadDown(cfg_.buttons[i])) pressed[i] = true;
    } else if (anyButton && (joyOk_ || now >= nextJoyRetry_)) {   // fallback: WinMM, one controller, 32 buttons
        JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) != JOYERR_NOERROR) { joyOk_ = false; nextJoyRetry_ = now + 3000; }
        else {
            joyOk_ = true;
            for (int i = 0; i < (int)Action::Count; ++i) {
                int b = cfg_.buttons[i];
                if (b >= 0 && b < Config::kPadSlot && b <= 31 && (ji.dwButtons & (1u << b))) pressed[i] = true;
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
    if (!diFailed_) { UpdatePads(GetTickCount64()); for (auto& p : pads_) for (int b = 0; b < 144; ++b) p.prev[b] = p.down[b]; }
    JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) == JOYERR_NOERROR) lastButtons_ = ji.dwButtons;
}

InputManager::NewButton InputManager::PollNewButton() {
    NewButton r;
    if (!diFailed_) {
        UpdatePads(GetTickCount64());
        for (auto& p : pads_) {
            for (int b = 0; b < 144; ++b) {
                if (p.down[b] && !p.prev[b] && r.button < 0) { r.button = b; r.device = p.name; }
                p.prev[b] = p.down[b];
            }
        }
        if (r.button >= 0) Log::Write("Controller: %s button %d pressed", r.device.c_str(), r.button + 1);
        if (!diFailed_) return r;
    }
    JOYINFOEX ji{}; ji.dwSize = sizeof(ji); ji.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1 + cfg_.controllerIndex, &ji) != JOYERR_NOERROR) return r;
    DWORD fresh = ji.dwButtons & ~lastButtons_;
    lastButtons_ = ji.dwButtons;
    for (int b = 0; b < 32; ++b) if (fresh & (1u << b)) { r.button = b; break; }
    return r;
}

// --- DirectInput controllers ------------------------------------------------------------------------------------
namespace {
HWND GameWindow() {   // DirectInput needs a top-level window of this process (background mode: it never takes input away)
    struct F { DWORD pid; HWND best; long area; } f{GetCurrentProcessId(), nullptr, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        F* f = (F*)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
        RECT r; GetWindowRect(h, &r); long a = (r.right - r.left) * (r.bottom - r.top);
        if (a > f->area) { f->area = a; f->best = h; }
        return TRUE;
    }, (LPARAM)&f);
    return f.best;
}
std::string Narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
}

InputManager::~InputManager() { ReleasePads(); if (di_) { di_->Release(); di_ = nullptr; } }

void InputManager::ReleasePads() {
    for (auto& p : pads_) if (p.dev) { p.dev->Unacquire(); p.dev->Release(); }
    pads_.clear();
}

void InputManager::UpdatePads(ULONGLONG now) {
    if (diFailed_) return;
    if (!di_) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&GameWindow, &self);
        if (FAILED(DirectInput8Create(self, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di_, nullptr)) || !di_) {
            di_ = nullptr; diFailed_ = true;
            Log::Write("Controller: DirectInput not available - only the first controller with 32 buttons (WinMM)");
            return;
        }
    }
    if (now >= enumAt_) {   // (re)connect all controllers now and then: plugged in later, woken up, ...
        enumAt_ = now + 10000;
        HWND game = GameWindow();
        if (!game) { enumAt_ = now + 2000; return; }
        std::vector<DIDEVICEINSTANCEW> found;
        di_->EnumDevices(DI8DEVCLASS_GAMECTRL, [](LPCDIDEVICEINSTANCEW d, LPVOID p) -> BOOL {
            ((std::vector<DIDEVICEINSTANCEW>*)p)->push_back(*d); return DIENUM_CONTINUE; }, &found, DIEDFL_ATTACHEDONLY);
        bool same = found.size() == pads_.size();
        for (size_t i = 0; same && i < found.size(); ++i) same = pads_[i].ok && pads_[i].name == Narrow(found[i].tszProductName);
        if (!same) {
            std::vector<Pad> old; old.swap(pads_);
            std::string list;
            for (auto& inst : found) {
                Pad p; p.name = Narrow(inst.tszProductName);
                for (auto& o : old) if (o.name == p.name) { memcpy(p.prev, o.prev, sizeof(p.prev)); }
                if (SUCCEEDED(di_->CreateDevice(inst.guidInstance, &p.dev, nullptr)) && p.dev &&
                    SUCCEEDED(p.dev->SetDataFormat(&c_dfDIJoystick2)) &&
                    SUCCEEDED(p.dev->SetCooperativeLevel(game, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
                    p.dev->Acquire();
                    p.ok = true;
                } else if (p.dev) { p.dev->Release(); p.dev = nullptr; }
                list += (list.empty() ? "" : ", ") + p.name + (p.ok ? "" : " (not readable)");
                pads_.push_back(std::move(p));
            }
            for (auto& o : old) if (o.dev) { o.dev->Unacquire(); o.dev->Release(); }
            Log::Write("Controller: %d found%s%s", (int)pads_.size(), list.empty() ? "" : " - ", list.c_str());
        }
    }
    for (auto& p : pads_) {
        if (!p.dev) continue;
        DIJOYSTATE2 st{};
        p.dev->Poll();
        HRESULT hr = p.dev->GetDeviceState(sizeof(st), &st);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) { p.dev->Acquire(); p.dev->Poll(); hr = p.dev->GetDeviceState(sizeof(st), &st); }
        if (FAILED(hr)) { for (bool& d : p.down) d = false; continue; }
        for (int b = 0; b < 128; ++b) p.down[b] = (st.rgbButtons[b] & 0x80) != 0;
        for (int k = 0; k < 4; ++k) {   // hat switch: 0 = up, 9000 = right, ... (diagonals press both directions)
            DWORD pov = st.rgdwPOV[k];
            bool centred = LOWORD(pov) == 0xFFFF;
            int deg = centred ? -1 : (int)(pov / 100);
            auto hit = [&](int dir) { if (centred) return false; int d = std::abs(deg - dir) % 360; return std::min(d, 360 - d) <= 45; };
            p.down[128 + k * 4 + 0] = hit(0); p.down[128 + k * 4 + 1] = hit(90); p.down[128 + k * 4 + 2] = hit(180); p.down[128 + k * 4 + 3] = hit(270);
        }
    }
}

bool InputManager::PadDown(int binding) const {
    int slot = binding / Config::kPadSlot, btn = binding % Config::kPadSlot;
    if (btn < 0 || btn >= 144) return false;
    const std::string* want = (slot > 0 && slot <= (int)cfg_.controllers.size()) ? &cfg_.controllers[slot - 1] : nullptr;
    if (slot > 0 && !want) return false;
    for (auto& p : pads_) if ((!want || p.name == *want) && p.down[btn]) return true;
    return false;
}
