// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "DisplayWindow.h"
#include "Logger.h"

static const UINT WM_APP_REDRAW = WM_APP + 1;

void DisplayWindow::Start(const DisplayConfig& cfg) {
    cfg_ = cfg;
    if (!cfg.enabled || thread_) return;
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
}

void DisplayWindow::Stop() {
    if (!thread_) return;
    HWND h = hwnd_.load();
    if (h) PostMessageW(h, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(thread_, 3000) != WAIT_OBJECT_0) Log::Write("Display thread did not stop in time");
    CloseHandle(thread_); thread_ = nullptr;
}

void DisplayWindow::Update(const DisplayState& st, std::shared_ptr<const OverlayImage> logo) {
    {
        std::lock_guard<std::mutex> l(mtx_);
        if (hasState_ && st == state_ && logo == logo_) return;
        state_ = st; logo_ = std::move(logo); hasState_ = true;
    }
    HWND h = hwnd_.load();
    if (h) PostMessageW(h, WM_APP_REDRAW, 0, 0);
}

void DisplayWindow::Render() {
    DisplayState st; std::shared_ptr<const OverlayImage> logo;
    { std::lock_guard<std::mutex> l(mtx_); st = state_; logo = logo_; }
    auto img = RenderDisplay(st, cfg_.width, cfg_.height, logo.get());
    std::lock_guard<std::mutex> l(mtx_);
    image_ = img;
}

LRESULT CALLBACK DisplayWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = (DisplayWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_APP_REDRAW:
        if (self) { self->Render(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        std::shared_ptr<const OverlayImage> img;
        if (self) { std::lock_guard<std::mutex> lk(self->mtx_); img = self->image_; }
        if (img) {
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = img->width; bi.bmiHeader.biHeight = -img->height;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc, 0, 0, img->width, img->height, 0, 0, 0, img->height, img->pixels.data(), &bi, DIB_RGB_COLORS);
        } else { RECT r; GetClientRect(h, &r); FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); }
        EndPaint(h, &ps);
        return 0; }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

DWORD WINAPI DisplayWindow::ThreadProc(LPVOID p) {
    auto* self = (DisplayWindow*)p;
    HINSTANCE inst = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&DisplayWindow::WndProc, &inst);
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"LocalRadioDisplay";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    // Owner window: an owned window does not get a taskbar button, but (unlike WS_EX_TOOLWINDOW) it still
    // shows up in PrismTextureStreamer's window list.
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    RECT r{0, 0, self->cfg_.width, self->cfg_.height};
    AdjustWindowRectEx(&r, WS_POPUP, FALSE, WS_EX_NOACTIVATE);
    int x = 40, y = 40;
    if (self->cfg_.offscreen) {   // to the left of the whole virtual desktop, so it is never visible
        x = GetSystemMetrics(SM_XVIRTUALSCREEN) - (r.right - r.left) - 200;
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    }
    HWND h = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, Utf8ToWide(self->cfg_.title).c_str(), WS_POPUP,
                             x, y, r.right - r.left, r.bottom - r.top, owner, nullptr, inst, nullptr);
    if (!h) { Log::Write("Display window creation failed (%lu)", GetLastError()); if (owner) DestroyWindow(owner); UnregisterClassW(wc.lpszClassName, inst); return 1; }
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
    self->Render();
    ShowWindow(h, SW_SHOWNOACTIVATE);
    Log::Write("Display window '%s' ready (%dx%d, %s) - select it in PrismTextureStreamer (Ctrl+F8)",
               self->cfg_.title.c_str(), self->cfg_.width, self->cfg_.height, self->cfg_.offscreen ? "off-screen" : "on-screen");
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    self->hwnd_ = nullptr;
    if (owner) DestroyWindow(owner);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
