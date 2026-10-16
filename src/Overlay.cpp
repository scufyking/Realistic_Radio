// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Overlay.h"
#include "D3D11Overlay.h"
#include "Logger.h"
#include <urlmon.h>
#include <algorithm>
#include <cmath>

namespace {
int AnchorFromName(const std::string& p) {
    static const char* names[] = {"top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right"};
    for (int i = 0; i < 6; ++i) if (p == names[i]) return i;
    return 1;
}
std::string SafeNameUtf8(const std::string& logo) {
    std::string n = logo;
    for (char& c : n) if (strchr("/\\:*?\"<>|", c)) c = '_';
    return n;
}
std::wstring SafeFileName(const std::string& logo) { return Utf8ToWide(SafeNameUtf8(logo)); }
std::string UrlEncodePath(const std::string& s) {   // percent-encode, keep RFC 3986 unreserved and '/'
    static const char* hex = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || strchr("-_.~/", c)) o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
}

void Overlay::Init(const OverlayConfig& cfg, const std::wstring& dataDir) {
    cfg_ = cfg; start_ = GetTickCount64();
    logoDir_ = dataDir + L"logos\\";
    CreateDirectoryW(logoDir_.c_str(), nullptr);
    anchor_ = AnchorFromName(cfg.position);
    off_ = cfg.mode == "off";
    if (off_) { Log::Write("Overlay disabled"); return; }
    D3D11Overlay::SetPlacement(anchor_, (float)cfg.offsetX, (float)cfg.offsetY);
    if (cfg.mode != "window") d3d_ = D3D11Overlay::Install();
    if (cfg.mode == "window" || (cfg.mode == "auto" && !d3d_)) UseWindow(true);
}

void Overlay::Shutdown() {
    D3D11Overlay::Uninstall();
    UseWindow(false);
    if (job_) { for (int i = 0; i < 30 && !job_->done; ++i) Sleep(100); }
}

void Overlay::Show(const OverlayInfo& info, const std::string& logoPath) {
    if (cfg_.showLogos && !logoPath.empty()) { if (logoPath != logoKey_) RequestLogo(logoPath); }
    else if (!logoPath.empty() || !cfg_.showLogos) { logoKey_.clear(); logo_.reset(); }
    if (off_) return;
    ULONGLONG now = GetTickCount64();
    if (!Visible()) shownAt_ = now;
    visibleUntil_ = now + (ULONGLONG)(cfg_.durationSeconds * 1000);
    info_ = info;
    Rerender();
}

void Overlay::Update(const OverlayInfo& info) {
    if (off_ || info == info_) return;
    info_ = info;
    if (Visible()) Rerender();
}

void Overlay::RequestLogo(const std::string& logoPath) {
    logoKey_ = logoPath; logo_.reset();
    auto it = logoCache_.find(logoPath);
    if (it != logoCache_.end()) { logo_ = it->second; return; }
    std::wstring file = logoDir_ + SafeFileName(logoPath);
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logo_ = LoadImageFile(file);
        if (logo_) { logoCache_[logoPath] = logo_; return; }
    }
    if (logoPath.rfind("http", 0) != 0 && cfg_.urlPrefix.empty()) return;   // bundled logos only, no network
    { auto f = logoFailedAt_.find(logoPath); if (f != logoFailedAt_.end() && GetTickCount64() - f->second < 600000) return; }
    if (job_ && !job_->done) return;   // a download is running; this logo is fetched next time it is shown
    std::string lower = logoPath; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".svg") == 0) return;   // SVG logos are not supported
    job_ = std::make_shared<LogoJob>();
    // server layout: <urlPrefix><logo path>, e.g. https://radio.scufystudio.de/logo/stations/images-europe/germany/80s80s.png
    std::string rel = logoPath; while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    job_->url = (logoPath.rfind("http", 0) == 0 ? logoPath : cfg_.urlPrefix + UrlEncodePath(rel));
    job_->file = file; job_->key = logoPath;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&Overlay::LogoThread, &self);
    auto* holder = new std::shared_ptr<LogoJob>(job_);
    HANDLE t = CreateThread(nullptr, 0, LogoThread, holder, 0, nullptr);
    if (t) CloseHandle(t); else { delete holder; FreeLibrary(self); job_->done = true; }
}

DWORD WINAPI Overlay::LogoThread(LPVOID p) {
    {
        auto job = *(std::shared_ptr<LogoJob>*)p; delete (std::shared_ptr<LogoJob>*)p;
        std::wstring tmp = job->file + L".part";
        HRESULT hr = URLDownloadToFileW(nullptr, Utf8ToWide(job->url).c_str(), tmp.c_str(), 0, nullptr);
        job->ok = SUCCEEDED(hr) && MoveFileExW(tmp.c_str(), job->file.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!job->ok) { DeleteFileW(tmp.c_str()); Log::Write("Logo download failed (hr=0x%08lx): %s", (unsigned long)hr, job->url.c_str()); }
        job->done = true;
    }
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Overlay::LogoThread, &self);
    FreeLibraryAndExitThread(self, 0);
}

HWND Overlay::FindGameWindow() {
    struct Ctx { DWORD pid; HWND best; long area; } c{GetCurrentProcessId(), nullptr, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
        wchar_t cls[64]; GetClassNameW(h, cls, 64);
        if (wcscmp(cls, L"RealisticRadioOverlay") == 0) return TRUE;
        RECT r; GetClientRect(h, &r); long a = (r.right - r.left) * (r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = h; }
        return TRUE;
    }, (LPARAM)&c);
    return c.best;
}

void Overlay::Rerender() {
    int h = D3D11Overlay::ScreenHeight();
    if (!h && window_) { if (!game_ || !IsWindow(game_)) game_ = FindGameWindow(); RECT r{}; if (game_ && GetClientRect(game_, &r)) h = r.bottom - r.top; }
    if (h < 200) h = 1080;
    renderedH_ = h;
    image_ = RenderOverlay(info_, h, cfg_.scale, logo_.get());
    D3D11Overlay::SetImage(image_);
}

void Overlay::UseWindow(bool on) {
    if (on == window_) return;
    window_ = on;
    if (on) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"RealisticRadioOverlay";
        RegisterClassExW(&wc);
        wnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               wc.lpszClassName, L"Realistic Radio", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
        Log::Write(wnd_ ? "Overlay: window mode active" : "Overlay: window creation failed");
        if (image_) Rerender();
    } else if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
}

void Overlay::UpdateWindow(float alpha) {
    if (!wnd_) return;
    if (!game_ || !IsWindow(game_)) game_ = FindGameWindow();
    HWND fg = GetForegroundWindow();
    bool gameActive = game_ && (fg == game_ || fg == wnd_);
    if (alpha <= 0.003f || !image_ || !gameActive || IsIconic(game_)) { if (IsWindowVisible(wnd_)) ShowWindow(wnd_, SW_HIDE); lastAlpha_ = -1; return; }
    RECT cr; GetClientRect(game_, &cr);
    POINT tl{0, 0}; ClientToScreen(game_, &tl);
    float sh = (float)(cr.bottom - cr.top), sw = (float)(cr.right - cr.left);
    float k = sh / std::max(1, image_->renderedForScreenHeight);
    if (std::abs(k - 1.f) > 0.02f) { Rerender(); k = 1.f; }
    float w = (float)image_->width, h = (float)image_->height, ox = (float)cfg_.offsetX * sh, oy = (float)cfg_.offsetY * sh;
    int a = anchor_;
    int x = tl.x + (int)((a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox);
    int y = tl.y + (int)((a < 3) ? oy : sh - h - oy);
    if (std::abs(alpha - lastAlpha_) < 0.01f && lastImg_ == image_.get() && lastPos_.x == x && lastPos_.y == y && IsWindowVisible(wnd_)) return;
    lastAlpha_ = alpha; lastImg_ = image_.get(); lastPos_ = POINT{x, y};
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = image_->width; bi.bmiHeader.biHeight = -image_->height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp) {
        memcpy(bits, image_->pixels.data(), image_->pixels.size() * 4);
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{x, y}, src{0, 0}; SIZE size{image_->width, image_->height};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)std::clamp((int)(alpha * 255), 0, 255), AC_SRC_ALPHA};
        UpdateLayeredWindow(wnd_, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old); DeleteObject(bmp);
        if (!IsWindowVisible(wnd_)) ShowWindow(wnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(wnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    DeleteDC(mem); ReleaseDC(nullptr, screen);
}

void Overlay::Tick() {
    ULONGLONG now = GetTickCount64();
    // pump messages for the overlay window (it lives on this thread)
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (off_) {   // overlay off: still finish logo downloads for the infotainment display
        if (job_ && job_->done) {
            auto j = job_; job_.reset();
            if (!j->ok) logoFailedAt_[j->key] = GetTickCount64();
            if (j->ok && SafeFileName(logoKey_) == j->file.substr(logoDir_.size())) { logo_ = LoadImageFile(j->file); if (logo_) logoCache_[logoKey_] = logo_; }
        }
        return;
    }

    if (cfg_.mode == "auto" && d3d_) {
        bool noD3D11 = D3D11Overlay::D3D11Frames() == 0 && (D3D11Overlay::OtherFrames() > 30 || now - start_ > 30000);
        if (noD3D11 && !window_) { Log::Write("Overlay: game does not render with D3D11 here, using window overlay"); UseWindow(true); }
        if (!noD3D11 && window_ && D3D11Overlay::D3D11Frames() > 0) UseWindow(false);
    }
    if (job_ && job_->done) {
        auto j = job_; job_.reset();
        if (!j->ok) logoFailedAt_[j->key] = GetTickCount64();
        if (j->ok && SafeFileName(logoKey_) == j->file.substr(logoDir_.size())) {
            logo_ = LoadImageFile(j->file);
            if (logo_) { logoCache_[logoKey_] = logo_; if (Visible()) Rerender(); }
        }
    }
    int h = D3D11Overlay::ScreenHeight();
    if (image_ && h >= 200 && std::abs(h - renderedH_) > 2) Rerender();   // resolution changed -> crisp text again

    float alpha = 1.f;
    if (!cfg_.alwaysVisible) {
        if (now >= visibleUntil_) alpha = 0.f;
        else alpha = std::min({1.f, (now - shownAt_) / 250.f, (visibleUntil_ - now) / 400.f});
    }
    D3D11Overlay::SetAlpha(d3d_ && !window_ ? alpha : 0.f);
    if (window_) UpdateWindow(alpha);
}
