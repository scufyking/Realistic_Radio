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

void Overlay::Reconfigure(const OverlayConfig& cfg, const ScreenConfig& screen) {
    std::string mode = cfg_.mode;   // the mode needs a restart (hooks / window are set up once)
    cfg_ = cfg; cfg_.mode = mode;
    scfg_.durationSeconds = screen.durationSeconds;
    bool areaChanged = scfg_.areaX != screen.areaX || scfg_.areaY != screen.areaY || scfg_.areaW != screen.areaW || scfg_.areaH != screen.areaH ||
                       scfg_.flipX != screen.flipX || scfg_.flipY != screen.flipY;
    bool designChanged = scfg_.design != screen.design || scfg_.volumeStyle != screen.volumeStyle || scfg_.scale != screen.scale || scfg_.logoSize != screen.logoSize, testChanged = scfg_.grid != screen.grid;
    if (scfg_.target != screen.target) { D3D11Overlay::SetScreenTargetIndex(screen.target); Log::Write("Screen: drawing into target %d (0 = all)", screen.target); }
    scfg_.volumeStyle = screen.volumeStyle;
    scfg_.target = screen.target; scfg_.scale = screen.scale; scfg_.startLogoScale = screen.startLogoScale; scfg_.logoSize = screen.logoSize;
    // truck preset with another screen target (render target size / format of the cab screen)
    if (screen.width > 0 && screen.height > 0 &&
        (scfg_.width != screen.width || scfg_.height != screen.height || scfg_.format != screen.format)) {
        scfg_.width = screen.width; scfg_.height = screen.height; scfg_.format = screen.format;
        if (!scfg_.probe) { collecting_ = false; D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format); }
        areaChanged = true;
        Log::Write("Screen target: %dx%d format %d", scfg_.width, scfg_.height, scfg_.format);
    }
    scfg_.areaX = screen.areaX; scfg_.areaY = screen.areaY; scfg_.areaW = screen.areaW; scfg_.areaH = screen.areaH;
    scfg_.flipX = screen.flipX; scfg_.flipY = screen.flipY; scfg_.design = screen.design; scfg_.grid = screen.grid;
    scfg_.startLogo = screen.startLogo; scfg_.startLogoSeconds = screen.startLogoSeconds; scfg_.startLogoFile = screen.startLogoFile;
    if (screenOn_ && (areaChanged || designChanged || testChanged)) {
        if (scfg_.grid) probeIdx_ = -1;   // test mode: draw the grid again with the new area
        else {
            if (testChanged) probeIdx_ = -1;
            D3D11Overlay::SetScreenArea((float)scfg_.areaX, (float)scfg_.areaY, (float)scfg_.areaW, (float)scfg_.areaH, scfg_.flipY, scfg_.flipX);
            if (ScreenUsable()) {   // show the card right away in its new place / design
                ULONGLONG t = GetTickCount64();
                if (!pinned_) { if (t >= scrUntil_) scrShownAt_ = t; scrUntil_ = t + (ULONGLONG)(scfg_.durationSeconds * 1000); }
                if (scrInfo_.volume >= 0 || scrInfo_.title.empty()) scrInfo_ = pageInfo_;
                RenderScreen();
            }
        }
    }
    ApplyStyle();
    PruneLogoCache(true);
    if (!menu_) D3D11Overlay::SetPlacement(anchor_, offX_, offY_);
    if (!off_ && Visible()) Rerender();
}

void Overlay::ApplyStyle() {
    const OverlayConfig& cfg = cfg_;
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
}

void Overlay::Init(const OverlayConfig& cfg, const ScreenConfig& screen, const std::wstring& dataDir) {
    cfg_ = cfg; scfg_ = screen; start_ = GetTickCount64();
    logoDir_ = dataDir + L"logos\\";
    dataDir_ = dataDir;
    CreateDirectoryW(logoDir_.c_str(), nullptr);
    ApplyStyle();
    PruneLogoCache(true);
    if (radioStyle_) Log::Write("Overlay style: car radio display");
    off_ = cfg.mode == "off";
    if (scfg_.enabled) {
        D3D11Overlay::EnableScreenHook(true);
        D3D11Overlay::SetScreenArea((float)scfg_.areaX, (float)scfg_.areaY, (float)scfg_.areaW, (float)scfg_.areaH, scfg_.flipY, scfg_.flipX);
        D3D11Overlay::SetScreenTargetIndex(scfg_.target);
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
    else { logoKey_.clear(); logo_.reset(); }   // no logo (USB track without cover, message without station)
    if (info.volume < 0) pageInfo_ = info;
    if (ScreenUsable()) {
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
    // in-cab card: refresh while it is visible (status, track time) - but never replace a volume card
    if (info.volume < 0) pageInfo_ = info;
    if (ScreenUsable() && (pinned_ || GetTickCount64() < scrUntil_) && scrInfo_.volume < 0 && !(info == scrInfo_)) {
        scrInfo_ = info;
        RenderScreen();
    }
    if (off_ || info == info_) return;
    info_ = info;
    if (Visible()) Rerender();
}

// ---- logo cache on disk: realistic-radio\logos\ (station logos and USB covers) ------------------------------
// Logos are only downloaded when a station is shown, but over months of driving that can add up. The folder is kept
// below overlay.logoCacheMB: the files used longest ago are deleted first (every use updates a file's time).
namespace {
struct CacheFile { std::wstring path; ULONGLONG time; uint64_t size; };
std::vector<CacheFile> ListCache(const std::wstring& dir) {
    std::vector<CacheFile> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t n = wcslen(fd.cFileName);
        if (n > 5 && _wcsicmp(fd.cFileName + n - 5, L".part") == 0) continue;   // download in progress
        v.push_back({dir + fd.cFileName, ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime,
                     ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return v;
}
}
void Overlay::TouchFile(const std::wstring& file) {
    HANDLE h = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
}
void Overlay::RememberLogo(const std::string& key, const std::shared_ptr<OverlayImage>& img) {
    if (logoCache_.size() >= 48) logoCache_.clear();   // decoded logos in memory: a few MB at most
    logoCache_[key] = img;
}
void Overlay::PruneLogoCache(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - lastPruneAt_ < 60000) return;
    lastPruneAt_ = now;
    auto files = ListCache(logoDir_);
    uint64_t total = 0; for (auto& f : files) total += f.size;
    const uint64_t limit = (uint64_t)std::max(5, cfg_.logoCacheMB) * 1024 * 1024;
    if (total <= limit) return;
    std::sort(files.begin(), files.end(), [](const CacheFile& a, const CacheFile& b) { return a.time < b.time; });
    int removed = 0;
    for (auto& f : files) {
        if (total <= limit * 8 / 10) break;   // some room, so this does not run after every download
        if (DeleteFileW(f.path.c_str())) { total -= f.size; ++removed; }
    }
    Log::Write("Logo cache: removed %d old file(s), %.1f MB left (limit %d MB)", removed, total / 1048576.0, cfg_.logoCacheMB);
}
void Overlay::LogoCacheUsage(uint64_t& bytes, int& files) const {
    auto v = ListCache(logoDir_);
    bytes = 0; files = (int)v.size();
    for (auto& f : v) bytes += f.size;
}
void Overlay::ClearLogoCache() {
    int n = 0;
    for (auto& f : ListCache(logoDir_)) if (DeleteFileW(f.path.c_str())) ++n;
    logoCache_.clear();
    Log::Write("Logo cache cleared (%d file(s))", n);
}

void Overlay::RequestLogo(const std::string& logoPath) {
    logoKey_ = logoPath; logo_.reset();
    auto it = logoCache_.find(logoPath);
    if (it != logoCache_.end()) { logo_ = it->second; return; }
    if (logoPath.rfind("file:", 0) == 0) {   // USB/CD cover: local image file
        logo_ = ShrinkImage(LoadImageFile(Utf8ToWide(logoPath.substr(5))), 512);
        if (logo_) RememberLogo(logoPath, logo_);
        return;
    }
    std::wstring file = logoDir_ + SafeFileName(logoPath);
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logo_ = LoadImageFile(file);
        if (logo_) { RememberLogo(logoPath, logo_); TouchFile(file); return; }
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
    if (!menu_) D3D11Overlay::SetImage(image_);   // while the settings menu is open it owns the picture
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
    if (menu_) {   // key menu covers the pop-up panel while it is open
        D3D11Overlay::SetAlpha(d3d_ && !window_ ? 1.f : 0.f);
        if (window_) UpdateWindow(1.f);
        return;
    }
    if (off_) {   // overlay off: still finish logo downloads for the infotainment display
        if (job_ && job_->done) {
            auto j = job_; job_.reset();
            if (!j->ok) logoFailedAt_[j->key] = GetTickCount64();
            if (j->ok && SafeFileName(logoKey_) == j->file.substr(logoDir_.size())) {
                logo_ = LoadImageFile(j->file);
                if (logo_) { RememberLogo(logoKey_, logo_); if (ScreenUsable() && (pinned_ || now < scrUntil_)) RenderScreen(); }   // logo arrived: show it now
            }
            if (j->ok) PruneLogoCache(false);
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
            if (logo_) { RememberLogo(logoKey_, logo_); if (Visible()) Rerender(); if (ScreenUsable() && (pinned_ || now < scrUntil_)) RenderScreen(); }
        }
        if (j->ok) PruneLogoCache(false);
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
    if (Splashing(GetTickCount64())) return;   // the start logo is on the screen: the card follows afterwards
    std::shared_ptr<OverlayImage> logo = (cfg_.showLogos && scrInfo_.volume < 0) ? logo_ : nullptr;
    // render at the pixel size of the visible area, so the card is not stretched
    int w = std::max(64, (int)std::lround(scfg_.width * std::clamp(scfg_.areaW, 0.01, 1.0)));
    int h = std::max(64, (int)std::lround(scfg_.height * std::clamp(scfg_.areaH, 0.01, 1.0)));
    if (scrInfo_.volume >= 0 && scfg_.volumeStyle == "bar") {   // slim bar at the top: over the navigation, or over the pinned radio page
        std::shared_ptr<OverlayImage> under;
        if (pinned_ && pageInfo_.volume < 0) {
            SongLine song;
            auto page = RenderScreenCard(pageInfo_, cfg_.showLogos ? logo_.get() : nullptr, w, h, scfg_.design, (float)scfg_.scale, (float)scfg_.logoSize, &song);
            under = song.strip ? ComposeSongLine(*page, song, 0) : page;
        }
        D3D11Overlay::SetScreenImage(RenderScreenVolumeBar(scrInfo_, w, h, scfg_.design, (float)scfg_.scale, under.get()));
        D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
        return;
    }
    SongLine song;
    scrCard_ = RenderScreenCard(scrInfo_, logo.get(), w, h, scfg_.design, (float)scfg_.scale, (float)scfg_.logoSize, &song);
    bool sameSong = scrSong_.strip && song.strip && scrSong_.strip->width == song.strip->width && scrSong_.x == song.x && scrSong_.y == song.y &&
                    scrSong_.strip->pixels == song.strip->pixels;
    scrSong_ = song;
    if (!sameSong) { songScrollAt_ = GetTickCount64(); songOff_ = 0; }   // a new title starts at the beginning
    D3D11Overlay::SetScreenImage(scrSong_.strip ? ComposeSongLine(*scrCard_, scrSong_, songOff_) : scrCard_);
    D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
}

// Auto-detection (beta) of the visible area: hide our own picture for a moment (so the game's next frames do not
// contain it), then let the D3D hook copy the game's own screen picture once and find where it drew.
void Overlay::StartScreenDetect() {
    detectResult_ = 0;
    if (!screenOn_ || scfg_.width <= 0 || scfg_.height <= 0) { detectResult_ = -1; return; }
    D3D11Overlay::ResetScreenCapture();
    detectPhase_ = 1; detectAt_ = GetTickCount64() + 700;
    Log::Write("Screen detect: started (target %dx%d)", scfg_.width, scfg_.height);
}
int Overlay::PollScreenDetect(float rect[4]) {
    int r = detectResult_;
    if (r == 1) for (int i = 0; i < 4; ++i) rect[i] = detectRect_[i];
    if (r != 0) detectResult_ = 0;
    return r;
}

void Overlay::TickScreen(ULONGLONG now) {
    if (detectPhase_) {
        D3D11Overlay::SetScreenAlpha(0.f);
        D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
        if (detectPhase_ == 1 && now >= detectAt_) { D3D11Overlay::RequestScreenCapture(); detectPhase_ = 2; detectAt_ = now + 4000; }
        else if (detectPhase_ == 2) {
            float r[4];
            int s = D3D11Overlay::ScreenCaptureState(r);
            if (s >= 2 || now >= detectAt_) {
                detectPhase_ = 0;
                D3D11Overlay::ResetScreenCapture();
                if (s == 2) { for (int i = 0; i < 4; ++i) detectRect_[i] = r[i]; detectResult_ = 1; }
                else detectResult_ = s >= 3 ? -s : -2;   // -2 = the screen was not drawn (not visible / other truck)
                Log::Write("Screen detect: %s", s == 2 ? "done" : "failed");
                probeIdx_ = -1;   // test grid: draw again
            }
        }
        return;
    }
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
    if (scfg_.grid && scfg_.width > 0 && scfg_.height > 0) {   // test mode: grid over the whole target + the card's area
        if (probeIdx_ != -2) {
            probeIdx_ = -2;
            D3D11Overlay::SetScreenArea(0.f, 0.f, 1.f, 1.f, scfg_.flipY, scfg_.flipX);
            D3D11Overlay::SetScreenImage(RenderScreenTest(scfg_.width, scfg_.height, scfg_.flipX, scfg_.flipY,
                                                          (float)scfg_.areaX, (float)scfg_.areaY, (float)scfg_.areaW, (float)scfg_.areaH));
            D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
            Log::Write("Screen test mode: grid on %dx%d (format %d), area %.2f %.2f %.2f %.2f", scfg_.width, scfg_.height, scfg_.format,
                       scfg_.areaX, scfg_.areaY, scfg_.areaW, scfg_.areaH);
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
    if (splash_) {   // start logo: fade in, hold, fade out - then the card (if one is due) or the navigation
        if (now < splashUntil_) {
            float fade = 700.f;
            D3D11Overlay::SetScreenAlpha(std::min({1.f, (now - splashAt_) / fade, (splashUntil_ - now) / fade}));
            return;
        }
        splash_.reset();
        if (pinned_ || now < scrUntil_) {
            scrShownAt_ = now; pinShownAt_ = now;
            scrUntil_ = std::max(scrUntil_, now + (ULONGLONG)(scfg_.durationSeconds * 1000));
            if (pinned_ && scrInfo_.volume >= 0) scrInfo_ = pageInfo_;
            RenderScreen();
        }
    }
    if (pinned_ && ScreenUsable()) {
        if (now >= scrUntil_ && scrInfo_.volume >= 0) { scrInfo_ = pageInfo_; RenderScreen(); }   // volume card over: back to the page
        a = std::min(1.f, (now - pinShownAt_) / 250.f);
    } else if (now < scrUntil_) a = std::min({1.f, (now - scrShownAt_) / 200.f, (scrUntil_ - now) / 400.f});
    D3D11Overlay::SetScreenAlpha(a);
    // live song title wider than its line: let it scroll (only the strip moves, the card is not rendered again)
    if (a > 0.f && scrCard_ && scrSong_.strip && scrSong_.strip->width > scrSong_.w && scrInfo_.volume < 0 && !Splashing(now)) {
        const ULONGLONG pause = 2000;
        int off = now < songScrollAt_ + pause ? 0 : (int)((now - songScrollAt_ - pause) * (scrSong_.h * 1.6) / 1000.0);
        if (off != songOff_) { songOff_ = off; D3D11Overlay::SetScreenImage(ComposeSongLine(*scrCard_, scrSong_, off)); }
    }
}

void Overlay::PrepareMenu() {
    if (!d3d_ && !window_) d3d_ = D3D11Overlay::Install();   // overlay and screen both off: hook only for the menu
}

void Overlay::ShowMenu(std::shared_ptr<OverlayImage> img) {
    if (!img) return;
    menuImg_ = img;
    menu_ = true;
    int sh = D3D11Overlay::ScreenHeight() >= 200 ? D3D11Overlay::ScreenHeight() : img->renderedForScreenHeight;
    if (menuX_ >= 0) D3D11Overlay::SetPlacement(0, menuX_ / sh, menuY_ / sh);   // where it was dragged to
    else D3D11Overlay::SetPlacement(1, 0.f, std::max(0.f, (sh - img->height) / 2.f / sh));   // centred
    D3D11Overlay::SetImage(menuImg_);
    if (window_) { image_ = menuImg_; lastImg_ = nullptr; }
    D3D11Overlay::SetAlpha(d3d_ && !window_ ? 1.f : 0.f);
}

void Overlay::SetMenuPosition(float x, float y) {
    menuX_ = x; menuY_ = y;
    int sh = std::max(1, D3D11Overlay::ScreenHeight());
    if (menu_) D3D11Overlay::SetPlacement(0, x / sh, y / sh);
}

void Overlay::HideMenu() {
    if (!menu_) return;
    menu_ = false; menuImg_.reset();
    D3D11Overlay::SetAlpha(0.f);
    D3D11Overlay::SetPlacement(anchor_, offX_, offY_);
    if (!off_ && info_.title.size()) Rerender(); else { image_.reset(); D3D11Overlay::SetImage(nullptr); }
    if (window_) { lastImg_ = nullptr; UpdateWindow(0.f); }
}

std::vector<std::wstring> Overlay::StartLogoFiles(const std::string& truckId, const std::string& brandId) const {
    std::vector<std::wstring> out;
    auto isImage = [](const std::wstring& n) {
        size_t d = n.rfind(L'.'); if (d == std::wstring::npos) return false;
        std::wstring e = n.substr(d); for (auto& ch : e) ch = towlower(ch);
        return e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".bmp";
    };
    auto lower = [](std::wstring s) { for (auto& ch : s) ch = towlower(ch); return s; };
    auto list = [&](const std::wstring& dir, const std::wstring& prefix, const std::wstring& stem) {
        std::vector<std::wstring> found;
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((TruckLogoDir() + dir + L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring n = fd.cFileName;
            if (!isImage(n)) continue;
            if (!stem.empty()) {   // top level: <stem>.png or <stem>-something.png
                std::wstring l = lower(n.substr(0, n.rfind(L'.')));
                if (l != stem && l.rfind(stem + L"-", 0) != 0) continue;
            }
            found.push_back(prefix + n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(found.begin(), found.end(), [&](const std::wstring& a, const std::wstring& b) { return lower(a) < lower(b); });
        for (auto& f : found) if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    };
    for (const std::string& key : {truckId, brandId}) {   // a mod truck can have its own folder (truck id) before its brand
        if (key.empty()) continue;
        std::wstring k = Utf8ToWide(key);
        list(k + L"\\", k + L"\\", L"");
        list(L"", L"", lower(k));
    }
    return out;
}

void Overlay::ShowStartLogo(const std::string& truckId, const std::string& brandId, const std::wstring& brand, const std::wstring& model) {
    if (!ScreenUsable() || !scfg_.startLogo || detectPhase_) return;
    std::shared_ptr<OverlayImage> logo;
    std::wstring file;
    if (scfg_.startLogoFile != "text") {
        auto files = StartLogoFiles(truckId, brandId);
        std::wstring want = Utf8ToWide(scfg_.startLogoFile);
        for (auto& f : files) if (_wcsicmp(f.c_str(), want.c_str()) == 0) file = f;
        if (file.empty() && !files.empty()) file = files.front();   // automatic: the first picture for this truck
        if (!file.empty()) if (auto raw = LoadImageFile(TruckLogoDir() + file)) logo = PrepareLogoImage(*raw);
    }
    if (!logo && brand.empty()) return;
    int w = std::max(64, (int)std::lround(scfg_.width * std::clamp(scfg_.areaW, 0.01, 1.0)));
    int h = std::max(64, (int)std::lround(scfg_.height * std::clamp(scfg_.areaH, 0.01, 1.0)));
    splash_ = RenderScreenSplash(logo.get(), brand, logo ? L"" : model, w, h, (float)scfg_.startLogoScale, (float)scfg_.scale);
    ULONGLONG now = GetTickCount64();
    splashAt_ = now; splashUntil_ = now + (ULONGLONG)(scfg_.startLogoSeconds * 1000);
    D3D11Overlay::SetScreenArea((float)scfg_.areaX, (float)scfg_.areaY, (float)scfg_.areaW, (float)scfg_.areaH, scfg_.flipY, scfg_.flipX);
    D3D11Overlay::SetScreenImage(splash_);
    D3D11Overlay::SetScreenTarget(scfg_.width, scfg_.height, scfg_.format);
    D3D11Overlay::SetScreenAlpha(0.f);
    Log::Write("Start logo: %s, truck '%s', brand id '%s' (%s)", WideToUtf8(brand).c_str(), truckId.c_str(), brandId.c_str(),
               logo ? ("truck-logos\\" + WideToUtf8(file)).c_str() : "name");
}

void Overlay::SetScreenPinned(bool p) {
    if (p == pinned_) return;
    pinned_ = p;
    D3D11Overlay::SetRadioPinned(p);
    ULONGLONG now = GetTickCount64();
    if (!ScreenUsable()) return;
    if (p) {
        pinShownAt_ = now < scrUntil_ ? now - 1000 : now;   // already visible: no fade
        if (now >= scrUntil_ || scrInfo_.volume < 0) scrInfo_ = pageInfo_;
        RenderScreen();
    } else {   // fade out, the navigation comes back
        scrShownAt_ = now - 1000;
        scrUntil_ = now + 400;
    }
}
