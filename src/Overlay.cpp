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

void Overlay::Init(const OverlayConfig& cfg, const ScreenConfig& screen, const std::wstring& dataDir) {
    cfg_ = cfg; scfg_ = screen; start_ = GetTickCount64();
    logoDir_ = dataDir + L"logos\\";
    CreateDirectoryW(logoDir_.c_str(), nullptr);
    radioStyle_ = cfg.style == "radio";
    anchor_ = AnchorFromName(radioStyle_ ? cfg.radio.position : cfg.position);
    offX_ = (float)(radioStyle_ ? cfg.radio.offsetX : cfg.offsetX);
    offY_ = (float)(radioStyle_ ? cfg.radio.offsetY : cfg.offsetY);
    scale_ = radioStyle_ ? cfg.radio.scale : cfg.scale;
    {
        std::string c = cfg.radio.color; for (auto& ch : c) ch = (char)tolower((unsigned char)ch);
        static const std::map<std::string, uint32_t> named = {{"amber", 0xFFB000}, {"green", 0x5CFF6A}, {"blue", 0x4FC3FF},
                                                              {"white", 0xF2F4FF}, {"red", 0xFF4A3D}, {"orange", 0xFF8A1E}};
        auto it = named.find(c);
        if (it != named.end()) lcdColor_ = it->second;
        else if (c.size() == 7 && c[0] == '#') lcdColor_ = (uint32_t)strtoul(c.c_str() + 1, nullptr, 16);
    }
    if (radioStyle_) Log::Write("Overlay style: car radio display");
    off_ = cfg.mode == "off";
    if (scfg_.enabled) {
        D3D11Overlay::EnableScreenHook(true);
        D3D11Overlay::SetScreenArea((float)scfg_.areaX, (float)scfg_.areaY, (float)scfg_.areaW, (float)scfg_.areaH, scfg_.flipY, scfg_.flipX);
        collecting_ = scfg_.probe || scfg_.width <= 0 || scfg_.height <= 0;
        D3D11Overlay::SetCollectTargets(collecting_);
        nextStatAt_ = start_ + 15000; nextProbeAt_ = start_ + 20000;
        Log::Write("Screen mode (experimental): %s", scfg_.probe ? "PROBE - test patterns will be drawn into candidate screens"
                   : (collecting_ ? "no target size set - logging render targets, set screen.probe = true to find the screen"
                                  : "drawing into the configured render target"));
        if (!scfg_.probe && !collecting_) D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
    }
    if (off_) Log::Write("Overlay panel disabled");
    if (off_ && !scfg_.enabled) return;
    D3D11Overlay::SetPlacement(anchor_, offX_, offY_);
    if (cfg.mode != "window" || scfg_.enabled) d3d_ = D3D11Overlay::Install();
    screenOn_ = scfg_.enabled && D3D11Overlay::ScreenHookActive();
    if (off_) return;
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
    if (screenOn_ && !scfg_.probe && !scfg_.grid && scfg_.width > 0 && scfg_.height > 0) {
        ULONGLONG t = GetTickCount64();
        if (t >= scrUntil_) scrShownAt_ = t;
        scrUntil_ = t + (ULONGLONG)(scfg_.durationSeconds * 1000);
        scrInfo_ = info;
        RenderScreen();
    }
    if (off_) return;
    ULONGLONG now = GetTickCount64();
    if (!Visible()) shownAt_ = now;
    info_ = info;
    double secs = cfg_.durationSeconds;
    if (radioStyle_) {   // restart the marquee and keep the display on for at least one full pass
        scroll_ = 0; nextScrollAt_ = now + 1200;
        int len = (int)LcdText().size();
        if (len > cfg_.radio.columns) secs = std::max(secs, 1.2 + (len + 5) * cfg_.radio.scrollSeconds + 1.0);
    }
    visibleUntil_ = now + (ULONGLONG)(secs * 1000);
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
    if (radioStyle_) image_ = RenderRadioLcd(LcdText(), scroll_, cfg_.radio.columns, info_.bars, info_.favourite, h, scale_, lcdColor_);
    else image_ = RenderOverlay(info_, h, cfg_.scale, logo_.get());
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
    float w = (float)image_->width, h = (float)image_->height, ox = offX_ * sh, oy = offY_ * sh;
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
    if (screenOn_) TickScreen(now);
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
            if (logo_) { logoCache_[logoKey_] = logo_; if (Visible()) Rerender(); if (screenOn_ && now < scrUntil_) RenderScreen(); }
        }
    }
    int h = D3D11Overlay::ScreenHeight();
    if (image_ && h >= 200 && std::abs(h - renderedH_) > 2) Rerender();   // resolution changed -> crisp text again
    if (radioStyle_ && Visible() && (int)LcdText().size() > cfg_.radio.columns && now >= nextScrollAt_) {   // marquee step
        ++scroll_;
        nextScrollAt_ = now + (ULONGLONG)(cfg_.radio.scrollSeconds * 1000);
        Rerender();
    }

    float alpha = 1.f;
    if (!cfg_.alwaysVisible) {
        if (now >= visibleUntil_) alpha = 0.f;
        else alpha = std::min({1.f, (now - shownAt_) / 250.f, (visibleUntil_ - now) / 400.f});
    }
    D3D11Overlay::SetAlpha(d3d_ && !window_ ? alpha : 0.f);
    if (window_) UpdateWindow(alpha);
}

std::wstring Overlay::LcdText() const {
    std::wstring t = info_.lcd.empty() ? info_.title : info_.lcd;
    if (!t.empty()) CharUpperBuffW(&t[0], (DWORD)t.size());   // car radios show capitals
    return t;
}

// ---------------------------------------------------------------------------------------------
// In-cab screen (experimental)
void Overlay::RenderScreen() {
    std::shared_ptr<OverlayImage> logo = (cfg_.showLogos && scrInfo_.volume < 0) ? logo_ : nullptr;
    // render at the pixel size of the visible area, so the card is not stretched
    int w = std::max(64, (int)std::lround(scfg_.width * std::clamp(scfg_.areaW, 0.01, 1.0)));
    int h = std::max(64, (int)std::lround(scfg_.height * std::clamp(scfg_.areaH, 0.01, 1.0)));
    D3D11Overlay::SetScreenImage(RenderScreenCard(scrInfo_, logo.get(), w, h));
    D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
}

void Overlay::TickScreen(ULONGLONG now) {
    if (collecting_ && now >= nextStatAt_) {   // merge and log the render targets the game used
        nextStatAt_ = now + 15000;
        for (auto& s : D3D11Overlay::TakeTargetStats()) {
            auto it = std::find_if(cands_.begin(), cands_.end(), [&](const Cand& c) { return c.w == s.w && c.h == s.h && c.fmt == s.format; });
            if (it == cands_.end()) cands_.push_back({s.w, s.h, s.format, s.binds}); else it->binds += s.binds;
        }
        std::string list;
        for (auto& c : cands_) { char b[64]; snprintf(b, sizeof(b), " %dx%d(fmt %d)", c.w, c.h, c.fmt); list += b; }
        Log::Write("Screen: %lld render-target changes so far; targets seen:%s", D3D11Overlay::RenderTargetCalls(), list.empty() ? " none yet" : list.c_str());
    }
    if (scfg_.grid && scfg_.width > 0 && scfg_.height > 0) {   // coordinate grid permanently on the configured target
        if (probeIdx_ != -2) {
            probeIdx_ = -2;
            D3D11Overlay::SetScreenImage(RenderScreenGrid(scfg_.width, scfg_.height, scfg_.flipX, scfg_.flipY));
            D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
            Log::Write("Screen grid: showing coordinates on %dx%d (format %d)", scfg_.width, scfg_.height, scfg_.format);
        }
        D3D11Overlay::SetScreenAlpha(1.f);
        return;
    }
    if (scfg_.probe) {
        if (now < nextProbeAt_ || cands_.empty()) return;
        nextProbeAt_ = now + (ULONGLONG)(scfg_.probeSeconds * 1000);
        probeIdx_ = (probeIdx_ + 1) % (int)cands_.size();
        const Cand& c = cands_[probeIdx_];
        wchar_t label[96];
        swprintf(label, 96, L"%d/%d  %dx%d  f%d", probeIdx_ + 1, (int)cands_.size(), c.w, c.h, c.fmt);
        D3D11Overlay::SetScreenImage(RenderScreenProbe(label, c.w, c.h));
        D3D11Overlay::SetScreenTarget(c.w, c.h, c.fmt);
        D3D11Overlay::SetScreenAlpha(1.f);
        Log::Write("Screen probe %d/%d: marking render target %dx%d (format %d) - is it visible on a truck screen?",
                   probeIdx_ + 1, (int)cands_.size(), c.w, c.h, c.fmt);
        return;
    }
    float a = 0.f;
    if (now < scrUntil_) a = std::min({1.f, (now - scrShownAt_) / 200.f, (scrUntil_ - now) / 400.f});
    D3D11Overlay::SetScreenAlpha(a);
}
