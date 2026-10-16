// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "OverlayImage.h"
#include <memory>

struct DisplayConfig {
    bool enabled = false;   // off by default: only useful together with PrismTextureStreamer
    int width = 1280, height = 720;
    bool offscreen = true;                    // keep the window outside the visible desktop
    std::string title = "Local Radio Display";
};

// A normal (non-tool) top-level window inside eurotrucks2.exe that shows the radio as an infotainment screen.
// PrismTextureStreamer (Ctrl+F8 -> pick "Local Radio Display") captures it onto the truck's GPS/dashboard screen.
// Runs on its own thread with a real message loop so captures (WGC or PrintWindow) are answered immediately.
class DisplayWindow {
public:
    void Start(const DisplayConfig& cfg);
    void Stop();
    void Update(const DisplayState& st, std::shared_ptr<const OverlayImage> logo);   // cheap, any thread
private:
    static DWORD WINAPI ThreadProc(LPVOID p);
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void Render();
    DisplayConfig cfg_;
    HANDLE thread_ = nullptr;
    std::atomic<HWND> hwnd_{nullptr};
    std::mutex mtx_;
    DisplayState state_;
    std::shared_ptr<const OverlayImage> logo_, image_;
    bool hasState_ = false;
};
