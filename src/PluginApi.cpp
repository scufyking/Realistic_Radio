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
#include "ExternalMedia.h"
#include "Addons.h"
#include <string>
#include "Logger.h"
#include "Common.h"
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

RR_API int rr_api_version() { return 2; }   // 2: add-on apps (1.7.0); the functions of version 1 are unchanged
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

// ---- phone media (Realistic Radio 1.7.0): music of another app on the PC, from the add-on realistic-radio-media.dll.
// The add-on calls rr_media_register about every second (also while nothing plays), rr_media_set when something plays
// or is paused, rr_media_none when nothing plays, rr_media_cover with the cover of the current title (premultiplied BGRA,
// top-down; 0 x 0 = none) and asks rr_media_command for the buttons pressed on the phone.
// status: 1 stopped, 2 paused, 3 playing, 4 changing. caps: 1 play / pause, 2 next, 4 previous. Times in ms, -1 = unknown.
RR_API void rr_media_register(const wchar_t* addonName) { ExternalMedia::Register(addonName ? addonName : L""); }
RR_API void rr_media_set(const wchar_t* app, const wchar_t* title, const wchar_t* artist, const wchar_t* album, int status, long long posMs, long long lenMs, int caps) {
    ExternalMedia::Set(app ? app : L"", title ? title : L"", artist ? artist : L"", album ? album : L"", status, posMs, lenMs, caps);
}
RR_API void rr_media_cover(const uint32_t* px, int w, int h) {
    if (!px || w <= 0 || h <= 0 || w > 4096 || h > 4096) { ExternalMedia::SetCover(nullptr); return; }
    auto img = std::make_shared<OverlayImage>();
    img->width = w; img->height = h; img->renderedForScreenHeight = 1080;
    img->pixels.assign(px, px + (size_t)w * h);
    ExternalMedia::SetCover(img);
}
RR_API void rr_media_none() { ExternalMedia::Clear(); }
RR_API int rr_media_command() { return ExternalMedia::TakeCommand(); }   // 0 none, 1 play / pause, 2 next, 3 previous
// a line in realistic-radio.log (UTF-8)
RR_API void rr_log(const char* line) { if (line) Log::Write("%s", line); }

// ---- add-ons with their own apps on the phone (Realistic Radio 1.7.0, interface version 2). Documentation and a
// header for add-on authors: sdk/addon/ (rr_addon.h, README). Texts are UTF-8, data is JSON.
RR_API int rr_app_register(const char* appJson) { return appJson && Addons::Register(appJson) ? 1 : 0; }
RR_API void rr_app_ui(const char* id, const char* uiJson) { if (id && uiJson) Addons::SetUi(id, uiJson); }
RR_API int rr_app_area(const char* id, int* w, int* h) { int W = 0, H = 0; bool open = id && Addons::Area(id, W, H); if (w) *w = W; if (h) *h = H; return open ? 1 : 0; }
RR_API void rr_app_frame(const char* id, const uint32_t* px, int w, int h) {
    if (!id || !px || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    auto img = std::make_shared<OverlayImage>();
    img->width = w; img->height = h; img->renderedForScreenHeight = 1080;
    img->pixels.assign(px, px + (size_t)w * h);
    Addons::SetFrame(id, img);
}
RR_API int rr_app_events(const char* id, char* buf, int size) { return id ? Addons::TakeEvents(id, buf, size) : 0; }
static int CopyOut(const std::string& s, char* buf, int size) {
    if (!buf || (int)s.size() + 1 > size) return -(int)(s.size() + 1);
    memcpy(buf, s.c_str(), s.size() + 1);
    return (int)s.size();
}
RR_API int rr_state(char* buf, int size) { return CopyOut(Addons::State(), buf, size); }
RR_API int rr_city(const char* token, char* buf, int size) { std::string j; if (!token || !Addons::City(token, j)) return 0; return CopyOut(j, buf, size); }
RR_API void rr_notify(const char* noticeJson) { if (noticeJson) Addons::Notify(noticeJson); }
