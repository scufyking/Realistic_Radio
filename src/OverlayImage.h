// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include <cstdint>
#include <memory>

// Premultiplied BGRA image (top-down). Used for the D3D11 texture and for UpdateLayeredWindow.
struct OverlayImage {
    int width = 0, height = 0;
    int renderedForScreenHeight = 1080;
    std::vector<uint32_t> pixels;
};

struct OverlayInfo {
    std::wstring headline;   // "Jetzt läuft:" or a status text
    std::wstring title;      // station name (orange, like the original)
    std::wstring subtitle;   // country / volume / state
    int bars = -1;           // 0..5 reception, -1 = hidden
    bool favourite = false;
    bool operator==(const OverlayInfo& o) const { return headline == o.headline && title == o.title && subtitle == o.subtitle && bars == o.bars && favourite == o.favourite; }
};

// Draws the panel with GDI (text, full Unicode incl. Cyrillic/Greek/Arabic via font fallback) + optional logo.
std::shared_ptr<OverlayImage> RenderOverlay(const OverlayInfo& info, int screenHeight, double scale, const OverlayImage* logo);
// State shown on the truck's infotainment screen (via PrismTextureStreamer window capture)
struct DisplayState {
    bool powered = true, playing = true, muted = false, favourite = false;
    std::wstring headline, title, country, status, prevName, nextName, footer;
    int bars = -1, volume = 0;
    bool operator==(const DisplayState& o) const {
        return powered == o.powered && playing == o.playing && muted == o.muted && favourite == o.favourite && headline == o.headline &&
               title == o.title && country == o.country && status == o.status && prevName == o.prevName && nextName == o.nextName &&
               footer == o.footer && bars == o.bars && volume == o.volume;
    }
};
// Full-screen "infotainment" picture (opaque BGRA) for the radio display window.
std::shared_ptr<OverlayImage> RenderDisplay(const DisplayState& st, int width, int height, const OverlayImage* logo);

// Decodes PNG/JPG/BMP via WIC to premultiplied BGRA (nullptr on failure). COM must be initialised.
std::shared_ptr<OverlayImage> LoadImageFile(const std::wstring& path);
