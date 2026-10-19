// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "MenuInput.h"
#include "D3D11Overlay.h"
#include "Logger.h"
#include <deque>
#include <set>
#include <algorithm>

namespace MenuInput {
namespace {
std::mutex g_mtx;
std::deque<Event> g_queue;
std::set<DWORD> g_swallowed;          // keys whose key-down was kept from the game (their key-up is kept too)
bool g_ctrl = false, g_shift = false, g_alt = false;
HHOOK g_kbd = nullptr, g_mouse = nullptr;
HANDLE g_thread = nullptr, g_ready = nullptr;
DWORD g_tid = 0;
bool g_poll = false;                  // no hooks: poll the keyboard (keys then also reach the game)
bool g_prev[256] = {};
std::atomic<float> g_scale{1.f};      // window pixels -> back-buffer pixels
std::atomic<float> g_cx{0}, g_cy{0};

bool GameInFront() {
    HWND fg = GetForegroundWindow(); if (!fg) return false;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return false;
    wchar_t cls[64] = {0}; GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"TruckerChatMenu")) return false;   // the Trucker Chat menu window keeps its keys
    return true;
}
void Push(const Event& e) { if (g_queue.size() < 128) g_queue.push_back(e); }

LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && GameInFront()) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        DWORD vk = k->vkCode;
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN, up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
        std::lock_guard<std::mutex> l(g_mtx);
        bool modifier = true;
        switch (vk) {
        case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: g_ctrl = down; break;
        case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT: g_shift = down; break;
        case VK_LMENU: case VK_RMENU: case VK_MENU: g_alt = down; break;
        case VK_LWIN: case VK_RWIN: return CallNextHookEx(nullptr, code, wp, lp);   // never block the Windows key
        default: modifier = false;
        }
        if (down) {
            if (!modifier) { Event e; e.type = Event::Key; e.vk = (int)vk; e.ctrl = g_ctrl; e.shift = g_shift; e.alt = g_alt; Push(e); }
            g_swallowed.insert(vk);
            return 1;
        }
        if (up && g_swallowed.erase(vk)) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && GameInFront()) {
        const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lp;
        Event e;
        switch (wp) {
        case WM_MOUSEMOVE: {   // move our own cursor by the movement the Windows cursor would have made
            POINT cur; GetCursorPos(&cur);
            float k = g_scale;
            float w = (float)std::max(1, D3D11Overlay::ScreenWidth()), h = (float)std::max(1, D3D11Overlay::ScreenHeight());
            float x = std::clamp(g_cx.load() + (m->pt.x - cur.x) * k, 0.f, w - 1), y = std::clamp(g_cy.load() + (m->pt.y - cur.y) * k, 0.f, h - 1);
            g_cx = x; g_cy = y;
            D3D11Overlay::SetCursorPos(x, y);
            return 1;
        }
        case WM_LBUTTONDOWN: e.type = Event::MouseDown; e.button = 0; break;
        case WM_LBUTTONUP: e.type = Event::MouseUp; e.button = 0; break;
        case WM_RBUTTONDOWN: e.type = Event::MouseDown; e.button = 1; break;
        case WM_RBUTTONUP: e.type = Event::MouseUp; e.button = 1; break;
        case WM_MBUTTONDOWN: e.type = Event::MouseDown; e.button = 2; break;
        case WM_MBUTTONUP: e.type = Event::MouseUp; e.button = 2; break;
        case WM_MOUSEWHEEL: e.type = Event::Wheel; e.wheel = (short)HIWORD(m->mouseData) > 0 ? 1 : -1; break;
        default: return 1;   // other buttons / horizontal wheel: keep from the game
        }
        std::lock_guard<std::mutex> l(g_mtx);
        Push(e);
        return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

DWORD WINAPI Thread(LPVOID) {
    MSG m;
    PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // create the message queue before signalling
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&KeyboardProc, &self);
    g_kbd = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, self, 0);
    if (!g_kbd) Log::Write("Menu: keyboard hook failed (error %lu)", GetLastError());
    g_mouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, self, 0);
    if (!g_mouse) Log::Write("Menu: mouse hook failed (error %lu)", GetLastError());
    SetEvent(g_ready);
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {}
    if (g_kbd) { UnhookWindowsHookEx(g_kbd); g_kbd = nullptr; }
    if (g_mouse) { UnhookWindowsHookEx(g_mouse); g_mouse = nullptr; }
    return 0;
}

void PollKeys() {   // g_mtx held
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    for (int vk = 0x08; vk < 0xFF; ++vk) {
        if ((vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN || vk == VK_RWIN) continue;
        bool d = down(vk);
        if (d && !g_prev[vk]) { Event e; e.type = Event::Key; e.vk = vk; e.ctrl = down(VK_CONTROL); e.shift = down(VK_SHIFT); e.alt = down(VK_MENU); Push(e); }
        g_prev[vk] = d;
    }
}
} // namespace

bool Start(float cx, float cy) {
    if (g_thread) return true;
    {
        std::lock_guard<std::mutex> l(g_mtx);
        g_queue.clear(); g_swallowed.clear();
        g_ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        g_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        g_alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }
    g_cx = cx; g_cy = cy;
    D3D11Overlay::SetCursorPos(cx, cy);
    {   // the back buffer can be larger or smaller than the window (render scaling, DPI)
        RECT r{}; HWND fg = GetForegroundWindow();
        float k = 1.f;
        if (fg && GetClientRect(fg, &r) && r.right > 0 && D3D11Overlay::ScreenWidth() > 0) k = (float)D3D11Overlay::ScreenWidth() / r.right;
        g_scale = std::clamp(k, 0.25f, 4.f);
    }
    g_poll = false;
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, Thread, nullptr, 0, &g_tid);
    if (g_thread) WaitForSingleObject(g_ready, 2000);
    CloseHandle(g_ready); g_ready = nullptr;
    if (g_kbd) return true;
    Stop();
    std::lock_guard<std::mutex> l(g_mtx);
    g_poll = true;
    for (int vk = 0; vk < 256; ++vk) g_prev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;   // keys held now do not count
    return false;
}

void Stop() {
    g_poll = false;
    if (!g_thread) return;
    PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread); g_thread = nullptr; g_tid = 0;
}

bool Pop(Event& e) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_poll && g_queue.empty() && GameInFront()) PollKeys();
    if (g_queue.empty()) return false;
    e = g_queue.front(); g_queue.pop_front();
    return true;
}

bool MouseActive() { return g_mouse != nullptr; }
} // namespace MenuInput
