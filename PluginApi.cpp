// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Drawing interface for other plugins in the same game process (Trucker Chat, since Realistic Radio 1.4.3).
// Two plugins that both hook Direct3D 11 (Present and OMSetRenderTargets) can crash the game: each one draws from
// inside the other's hook and saves / restores the pipeline state in the middle of the other's drawing. With this
// interface only Realistic Radio hooks Direct3D; the other plugin hands over its pictures and Realistic Radio draws
// them: in-cab screen layer 1 (after the radio's layer 0) and a second back-buffer picture (below the radio overlay
// and the settings menu).
//
// The other plugin finds the functions with GetModuleHandle("realistic-radio.dll") + GetProcAddress. Plain C types
// only; pictures are copied (premultiplied BGRA, top-down, width * height values). Thread-safe, call from a worker
// thread (never from DllMain or a game callback). rr_api_version() is raised only for incompatible changes; new
// functions may be added - look each one up and treat a missing one as "not supported".
#include "D3D11Overlay.h"
#include <cstdint>
#include <cstring>

#define RR_API extern "C" __declspec(dllexport)

namespace {
std::shared_ptr<const OverlayImage> Copy(const uint32_t* px, int w, int h, int forScreenHeight) {
    if (!px || w <= 0 || h <= 0 || w > 8192 || h > 8192) return nullptr;
    auto img = std::make_shared<OverlayImage>();
    img->width = w; img->height = h; img->renderedForScreenHeight = forScreenHeight > 0 ? forScreenHeight : 1080;
    img->pixels.assign(px, px + (size_t)w * h);
    return img;
}
}

RR_API int rr_api_version() { return 1; }
RR_API int rr_attach() { return D3D11Overlay::Install() ? 1 : 0; }   // makes sure the hook is in place (idempotent)
RR_API void rr_detach() { D3D11Overlay::SetChatAlpha(0.f); D3D11Overlay::SetLayerScreenAlpha(1, 0.f); }   // the hook stays (the radio's)
RR_API void rr_enable_screen_hook(int on) { if (on) D3D11Overlay::EnableScreenHook(true); }   // never switches the radio's screen off

// in-cab screen, layer 1
RR_API void rr_chat_screen_image(const uint32_t* px, int w, int h, int forScreenHeight) { D3D11Overlay::SetLayerScreenImage(1, Copy(px, w, h, forScreenHeight)); }
RR_API void rr_chat_screen_target(int w, int h, int format) { D3D11Overlay::SetLayerScreenTarget(1, w, h, format); }
RR_API void rr_chat_screen_alpha(float a) { D3D11Overlay::SetLayerScreenAlpha(1, a); }
RR_API void rr_chat_screen_area(float x, float y, float w, float h, int flipY, int flipX) { D3D11Overlay::SetLayerScreenArea(1, x, y, w, h, flipY != 0, flipX != 0); }

// back-buffer picture (side chat / overlay)
RR_API void rr_chat_image(const uint32_t* px, int w, int h, int forScreenHeight) { D3D11Overlay::SetChatImage(Copy(px, w, h, forScreenHeight)); }
RR_API void rr_chat_alpha(float a) { D3D11Overlay::SetChatAlpha(a); }
RR_API void rr_chat_placement(int anchor, float offsetX, float offsetY) { D3D11Overlay::SetChatPlacement(anchor, offsetX, offsetY); }

// state
RR_API long long rr_d3d11_frames() { return D3D11Overlay::D3D11Frames(); }
RR_API long long rr_other_frames() { return D3D11Overlay::OtherFrames(); }
RR_API int rr_screen_height() { return D3D11Overlay::ScreenHeight(); }
RR_API int rr_screen_hook_active() { return D3D11Overlay::ScreenHookActive() ? 1 : 0; }
RR_API long long rr_render_target_calls() { return D3D11Overlay::RenderTargetCalls(); }
RR_API void rr_collect_targets(int on) { if (on) D3D11Overlay::SetCollectTargets(true); }
// colour render targets bound since the last call: 4 values per entry (width, height, DXGI format, binds)
RR_API int rr_take_target_stats(long long* out, int maxEntries) {
    auto v = D3D11Overlay::TakeTargetStats();
    int n = 0;
    for (auto& s : v) { if (n >= maxEntries || !out) break; out[n * 4] = s.w; out[n * 4 + 1] = s.h; out[n * 4 + 2] = s.format; out[n * 4 + 3] = s.binds; ++n; }
    return n;
}

// the radio's own screen: target size / format, area and flip of the active truck preset (0 = radio screen off)
RR_API int rr_radio_screen(int* w, int* h, int* format, float* x, float* y, float* aw, float* ah, int* flipY, int* flipX) {
    int W, H, F; float X, Y, AW, AH; bool FY, FX;
    bool ok = D3D11Overlay::RadioScreenGeometry(W, H, F, X, Y, AW, AH, FY, FX);
    if (w) *w = W; if (h) *h = H; if (format) *format = F; if (x) *x = X; if (y) *y = Y; if (aw) *aw = AW; if (ah) *ah = AH;
    if (flipY) *flipY = FY; if (flipX) *flipX = FX;
    return ok ? 1 : 0;
}
RR_API int rr_radio_card_showing() { return D3D11Overlay::RadioCardShowing() ? 1 : 0; }   // a radio card is on the screen now (not pinned)
