// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Config.h"
#include "OverlayImage.h"

// Overlay facade, owned by the radio thread.
//  mode "d3d11":  drawn inside the game's frames (works in fullscreen) – default renderer of ETS2/ATS is DirectX 11
//  mode "window": transparent, click-through, topmost window over the game window (works with OpenGL/borderless)
//  mode "auto":   d3d11; switches to window if the game does not present D3D11 frames (e.g. OpenGL renderer)
class Overlay {
public:
    void Init(const OverlayConfig& cfg, const ScreenConfig& screen, const std::wstring& dataDir);
    void Shutdown();
    void Show(const OverlayInfo& info, const std::string& logoPath);   // shows for durationSeconds
    void Update(const OverlayInfo& info);                               // changes content without extending display time
    void Tick();                                                        // ~20x per second
    // Radio page: keep the card on the in-cab screen permanently (instead of the navigation) until switched off
    void SetScreenPinned(bool pinned);
    // start screen when the ignition is switched on: brands\<brandId>.png|.jpg, else the brand name, fading in on black
    void ShowStartLogo(const std::string& truckId, const std::string& brandId, const std::wstring& brand, const std::wstring& model);
    // pictures for this truck in truck-logos\: sub folders <truck id>\ and <brand id>\, files <brand id>.png / <brand id>-*.png
    // (paths relative to truck-logos\, sorted)
    std::vector<std::wstring> StartLogoFiles(const std::string& truckId, const std::string& brandId) const;
    std::wstring TruckLogoDir() const { return dataDir_ + L"truck-logos\\"; }
    // Key menu: shown in the middle of the upper half of the game picture while open
    void PrepareMenu();                                   // makes sure the D3D11 hook is installed
    void ShowMenu(std::shared_ptr<OverlayImage> img);
    void SetMenuPosition(float x, float y);
    // auto-detection of the visible screen area (beta)
    void StartScreenDetect();
    int PollScreenDetect(float rect[4]);      // 0 running/idle, 1 done (rect = x, y, w, h), < 0 failed   // top-left corner in back-buffer pixels (menu dragged by its title)
    void Reconfigure(const OverlayConfig& cfg, const ScreenConfig& screen);
    void LogoCacheUsage(uint64_t& bytes, int& files) const;
    void ClearLogoCache();
    const std::wstring& LogoDir() const { return logoDir_; }   // settings menu: style, position, size, times
    void HideMenu();
    bool ScreenPinned() const { return pinned_; }
    bool Visible() const { return visibleUntil_ > GetTickCount64() || cfg_.alwaysVisible; }
    const OverlayConfig& Cfg() const { return cfg_; }
    std::shared_ptr<const OverlayImage> CurrentLogo() const { return logo_; }
private:
    void Rerender();
    void ApplyStyle();
    void PruneLogoCache(bool force);
    void RememberLogo(const std::string& key, const std::shared_ptr<OverlayImage>& img);
    static void TouchFile(const std::wstring& file);
    ULONGLONG lastPruneAt_ = 0;
    void RequestLogo(const std::string& logoPath);
    void UseWindow(bool on);
    void UpdateWindow(float alpha);
    HWND FindGameWindow();
    static DWORD WINAPI LogoThread(LPVOID);

    OverlayConfig cfg_;
    std::wstring logoDir_;
    bool d3d_ = false, window_ = false, off_ = true;
    int anchor_ = 1;
    bool radioStyle_ = false;
    float offX_ = 0, offY_ = 0;
    double scale_ = 1.0;
    uint32_t lcdColor_ = 0xFFB000;
    int scroll_ = 0;
    ULONGLONG nextScrollAt_ = 0;
    std::wstring LcdText() const;
    // in-cab screen (experimental)
    void TickScreen(ULONGLONG now);
    void RenderScreen();
    ScreenConfig scfg_;
    bool screenOn_ = false, collecting_ = false;
    OverlayInfo scrInfo_;
    OverlayInfo pageInfo_;              // latest station/track info: content of the radio page
    bool pinned_ = false;
    ULONGLONG splashAt_ = 0, splashUntil_ = 0;
    std::shared_ptr<OverlayImage> splash_;
    std::wstring dataDir_;
    bool Splashing(ULONGLONG now) const { return splash_ && now < splashUntil_; }
    bool menu_ = false;
    float menuX_ = -1, menuY_ = -1;           // -1 = centred
    int detectPhase_ = 0, detectResult_ = 0;
    ULONGLONG detectAt_ = 0;
    float detectRect_[4] = {};
    std::shared_ptr<OverlayImage> menuImg_;
    ULONGLONG pinShownAt_ = 0;
    bool ScreenUsable() const { return screenOn_ && !scfg_.probe && !scfg_.grid && scfg_.width > 0 && scfg_.height > 0; }
    ULONGLONG scrShownAt_ = 0, scrUntil_ = 0, nextStatAt_ = 0, nextProbeAt_ = 0;
    struct Cand { int w, h, fmt; long long binds; };
    std::vector<Cand> cands_;
    int probeIdx_ = -1;
    ULONGLONG start_ = 0, shownAt_ = 0, visibleUntil_ = 0;
    OverlayInfo info_;
    std::string logoKey_;
    std::shared_ptr<OverlayImage> logo_, image_;
    int renderedH_ = 0;
    HWND wnd_ = nullptr, game_ = nullptr;
    float lastAlpha_ = -1; const OverlayImage* lastImg_ = nullptr; POINT lastPos_{-99999, -99999};
    // logo download (one background thread at a time)
    struct LogoJob { std::string key, url; std::wstring file; std::atomic<bool> done{false}; bool ok = false; };
    std::shared_ptr<LogoJob> job_;
    std::map<std::string, std::shared_ptr<OverlayImage>> logoCache_;
    std::map<std::string, ULONGLONG> logoFailedAt_;
};
