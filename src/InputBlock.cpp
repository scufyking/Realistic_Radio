// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#define DIRECTINPUT_VERSION 0x0800
#include "InputBlock.h"
#include "Logger.h"
#include <dinput.h>
#include <psapi.h>
#include <cstring>

namespace {
std::atomic<bool> g_block{false};
std::atomic<bool> g_loggedRaw{false}, g_loggedDi{false};
bool g_installed = false;

// ---- raw input --------------------------------------------------------------------------------
typedef UINT(WINAPI* GetRawInputDataFn)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
typedef UINT(WINAPI* GetRawInputBufferFn)(PRAWINPUT, PUINT, UINT);
GetRawInputDataFn g_origData = nullptr;
GetRawInputBufferFn g_origBuffer = nullptr;

void Idle(RAWINPUT* ri) {
    if (ri->header.dwType != RIM_TYPEMOUSE) return;
    ri->data.mouse.lLastX = 0; ri->data.mouse.lLastY = 0;
    ri->data.mouse.usButtonFlags = 0; ri->data.mouse.usButtonData = 0; ri->data.mouse.ulRawButtons = 0;
    if (!g_loggedRaw.exchange(true)) Log::Write("Menu: mouse kept from the game (raw input)");
}
UINT WINAPI HookGetRawInputData(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT header) {
    UINT r = g_origData(h, cmd, data, size, header);
    if (g_block && cmd == RID_INPUT && data && r != (UINT)-1 && r >= sizeof(RAWINPUTHEADER)) Idle((RAWINPUT*)data);
    return r;
}
UINT WINAPI HookGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT header) {
    UINT r = g_origBuffer(data, size, header);
    if (g_block && data && r != (UINT)-1) {
        RAWINPUT* ri = data;
        for (UINT i = 0; i < r; ++i) {
            Idle(ri);
            ri = (RAWINPUT*)(((ULONG_PTR)((BYTE*)ri + ri->header.dwSize) + 7) & ~(ULONG_PTR)7);   // NEXTRAWINPUTBLOCK (8-byte aligned)
        }
    }
    return r;
}

bool PatchPtr(void** slot, void* fn) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, fn);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Redirects the module's imports of user32!GetRawInputData / GetRawInputBuffer. Returns the number of entries patched.
int PatchImports(HMODULE mod) {
    BYTE* base = (BYTE*)mod;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0;
    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((const char*)(base + imp->Name), "user32.dll") != 0) continue;
        auto* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            void** slot = (void**)&thunk->u1.Function;
            if (*slot == (void*)g_origData && PatchPtr(slot, (void*)&HookGetRawInputData)) ++n;
            else if (*slot == (void*)g_origBuffer && PatchPtr(slot, (void*)&HookGetRawInputBuffer)) ++n;
        }
    }
    return n;
}

// ---- window messages: WM_INPUT from the mouse never reaches the game's window procedure while blocked ----------
WNDPROC g_origProc = nullptr;
HWND g_wnd = nullptr;
LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT && g_block && g_origData) {
        RAWINPUTHEADER hdr; UINT sz = sizeof(hdr);
        if (g_origData((HRAWINPUT)lp, RID_HEADER, &hdr, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && hdr.dwType == RIM_TYPEMOUSE) {
            if (!g_loggedRaw.exchange(true)) Log::Write("Menu: mouse kept from the game (WM_INPUT)");
            return DefWindowProcW(h, msg, wp, lp);   // lets Windows clean up the input
        }
    }
    return CallWindowProcW(g_origProc, h, msg, wp, lp);
}

// ---- DirectInput 8 -----------------------------------------------------------------------------
typedef HRESULT(STDMETHODCALLTYPE* GetStateFn)(void*, DWORD, LPVOID);
typedef HRESULT(STDMETHODCALLTYPE* GetDataFn)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
GetStateFn g_stateW = nullptr, g_stateA = nullptr;
GetDataFn g_dataW = nullptr, g_dataA = nullptr;

bool IsMouseW(void* d) { DIDEVICEINSTANCEW i{}; i.dwSize = sizeof(i); return SUCCEEDED(((IDirectInputDevice8W*)d)->GetDeviceInfo(&i)) && GET_DIDEVICE_TYPE(i.dwDevType) == DI8DEVTYPE_MOUSE; }
bool IsMouseA(void* d) { DIDEVICEINSTANCEA i{}; i.dwSize = sizeof(i); return SUCCEEDED(((IDirectInputDevice8A*)d)->GetDeviceInfo(&i)) && GET_DIDEVICE_TYPE(i.dwDevType) == DI8DEVTYPE_MOUSE; }
void LogDi() { if (!g_loggedDi.exchange(true)) Log::Write("Menu: mouse kept from the game (DirectInput)"); }

HRESULT STDMETHODCALLTYPE StateW(void* self, DWORD cb, LPVOID data) {
    HRESULT r = g_stateW(self, cb, data);
    if (g_block && SUCCEEDED(r) && data && (cb == sizeof(DIMOUSESTATE) || cb == sizeof(DIMOUSESTATE2)) && IsMouseW(self)) { memset(data, 0, cb); LogDi(); }
    return r;
}
HRESULT STDMETHODCALLTYPE StateA(void* self, DWORD cb, LPVOID data) {
    HRESULT r = g_stateA(self, cb, data);
    if (g_block && SUCCEEDED(r) && data && (cb == sizeof(DIMOUSESTATE) || cb == sizeof(DIMOUSESTATE2)) && IsMouseA(self)) { memset(data, 0, cb); LogDi(); }
    return r;
}
HRESULT STDMETHODCALLTYPE DataW(void* self, DWORD cb, LPDIDEVICEOBJECTDATA rg, LPDWORD n, DWORD flags) {
    HRESULT r = g_dataW(self, cb, rg, n, flags);
    if (g_block && SUCCEEDED(r) && n && *n && IsMouseW(self)) { *n = 0; LogDi(); }   // events are read and dropped
    return r;
}
HRESULT STDMETHODCALLTYPE DataA(void* self, DWORD cb, LPDIDEVICEOBJECTDATA rg, LPDWORD n, DWORD flags) {
    HRESULT r = g_dataA(self, cb, rg, n, flags);
    if (g_block && SUCCEEDED(r) && n && *n && IsMouseA(self)) { *n = 0; LogDi(); }
    return r;
}

bool PatchSlot(void** vt, int idx, void* fn, void** orig) {
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    if (orig) *orig = vt[idx];
    InterlockedExchangePointer(&vt[idx], fn);
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    return true;
}

// Every IDirectInputDevice8 object of one character set shares one function table, so patching the table of a
// device we create also covers the devices the game created (also those created earlier).
void HookDirectInput() {
    HMODULE di8 = LoadLibraryW(L"dinput8.dll");
    if (!di8) return;
    typedef HRESULT(WINAPI* CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto create = (CreateFn)GetProcAddress(di8, "DirectInput8Create");
    if (!create) return;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&HookDirectInput, &self);
    int ok = 0;
    IDirectInput8W* w = nullptr;
    if (SUCCEEDED(create(self, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&w, nullptr)) && w) {
        IDirectInputDevice8W* d = nullptr;
        if (SUCCEEDED(w->CreateDevice(GUID_SysMouse, &d, nullptr)) && d) {
            void** vt = *(void***)d;
            if (vt[9] != (void*)&StateW && PatchSlot(vt, 9, (void*)&StateW, (void**)&g_stateW)) ++ok;
            if (vt[10] != (void*)&DataW && PatchSlot(vt, 10, (void*)&DataW, (void**)&g_dataW)) ++ok;
            d->Release();
        }
        w->Release();
    }
    IDirectInput8A* a = nullptr;
    if (SUCCEEDED(create(self, DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&a, nullptr)) && a) {
        IDirectInputDevice8A* d = nullptr;
        if (SUCCEEDED(a->CreateDevice(GUID_SysMouse, &d, nullptr)) && d) {
            void** vt = *(void***)d;
            if (vt[9] != (void*)&StateA && vt[9] != (void*)&StateW && PatchSlot(vt, 9, (void*)&StateA, (void**)&g_stateA)) ++ok;
            if (vt[10] != (void*)&DataA && vt[10] != (void*)&DataW && PatchSlot(vt, 10, (void*)&DataA, (void**)&g_dataA)) ++ok;
            d->Release();
        }
        a->Release();
    }
    Log::Write("Menu: DirectInput mouse filter %s (%d entries)", ok ? "ready" : "not available", ok);
    // dinput8.dll stays loaded: the patched function tables point into this DLL, which is pinned by the D3D hook
}
} // namespace

namespace InputBlock {
void Install() {
    if (g_installed) return;
    g_installed = true;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    g_origData = (GetRawInputDataFn)GetProcAddress(u32, "GetRawInputData");
    g_origBuffer = (GetRawInputBufferFn)GetProcAddress(u32, "GetRawInputBuffer");
    int patched = 0;
    if (g_origData && g_origBuffer) {
        wchar_t winDir[MAX_PATH]; GetWindowsDirectoryW(winDir, MAX_PATH);
        size_t winLen = wcslen(winDir);
        HMODULE mods[1024]; DWORD need = 0;
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Install, &self);
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) {
            for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 1024); ++i) {
                if (mods[i] == self) continue;
                wchar_t path[MAX_PATH]; GetModuleFileNameW(mods[i], path, MAX_PATH);
                if (_wcsnicmp(path, winDir, winLen) == 0) continue;   // Windows' own DLLs: leave alone
                patched += PatchImports(mods[i]);
            }
        }
    }
    Log::Write("Menu: raw input mouse filter on %d import(s)", patched);
    HookDirectInput();
}
void SetMouseBlocked(bool on) { g_block = on; }
void AttachWindow(HWND w) {
    if (!w || g_wnd) return;   // once; the filter stays in place and passes everything through while the menu is closed
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return;
    WNDPROC prev = (WNDPROC)GetWindowLongPtrW(w, GWLP_WNDPROC);
    if (!prev) return;
    HMODULE self = nullptr;   // the window keeps calling into this DLL: keep it loaded for the process lifetime
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&WndProc, &self);
    g_origProc = prev;
    if (SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)&WndProc)) { g_wnd = w; Log::Write("Menu: window message filter on the game window"); }
    else g_origProc = nullptr;
}
}
