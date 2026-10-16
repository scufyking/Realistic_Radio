// ETS2 Local Radio Native
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
    void Init(const OverlayConfig& cfg, const std::wstring& dataDir);
    void Shutdown();
    void Show(const OverlayInfo& info, const std::string& logoPath);   // shows for durationSeconds
    void Update(const OverlayInfo& info);                               // changes content without extending display time
    void Tick();                                                        // ~20x per second
    bool Visible() const { return visibleUntil_ > GetTickCount64() || cfg_.alwaysVisible; }
    const OverlayConfig& Cfg() const { return cfg_; }
    std::shared_ptr<const OverlayImage> CurrentLogo() const { return logo_; }
private:
    void Rerender();
    void RequestLogo(const std::string& logoPath);
    void UseWindow(bool on);
    void UpdateWindow(float alpha);
    HWND FindGameWindow();
    static DWORD WINAPI LogoThread(LPVOID);

    OverlayConfig cfg_;
    std::wstring logoDir_;
    bool d3d_ = false, window_ = false, off_ = true;
    int anchor_ = 1;
    ULONGLONG start_ = 0, shownAt_ = 0, visibleUntil_ = 0;
    OverlayInfo info_;
    std::string logoKey_;
    std::shared_ptr<OverlayImage> logo_, image_;
    int renderedH_ = 0;
    HWND wnd_ = nullptr, game_ = nullptr;
    float lastAlpha_ = -1; const OverlayImage* lastImg_ = nullptr; POINT lastPos_{-99999, -99999};
    // logo download (one background thread at a time)
    struct LogoJob { std::string url; std::wstring file; std::atomic<bool> done{false}; bool ok = false; };
    std::shared_ptr<LogoJob> job_;
    std::map<std::string, std::shared_ptr<OverlayImage>> logoCache_;
};
