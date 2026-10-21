// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "OverlayImage.h"

// Draws the overlay into ETS2's own Direct3D 11 frames.
// The plugin already runs inside eurotrucks2.exe, so no injection is needed: we create a throw-away D3D11
// device + swap chain once, read the IDXGISwapChain vtable (shared by every swap chain in dxgi.dll) and
// redirect Present/Present1 to our function, which renders a textured quad and then calls the original.
namespace D3D11Overlay {
bool Install();                  // call from the radio thread (never from DllMain / game callbacks)
void Watchdog();                 // radio thread, ~20x per second: fallback code hooks for graphics tools (see D3D11Overlay.cpp)
void Uninstall();
void SetImage(std::shared_ptr<const OverlayImage> img);
void SetAlpha(float a);
void SetPlacement(int anchor, float offsetX, float offsetY);   // anchor 0..5 = TL, TC, TR, BL, BC, BR; offsets in screen heights
long long D3D11Frames();         // frames presented by a D3D11 device (overlay drawable)
long long OtherFrames();         // frames from D3D12/other devices (overlay not drawable here)
int ScreenHeight();              // back-buffer height of the last frame, 0 if unknown
int ScreenWidth();
// auto-detection (beta): the next time the game finishes the screen target, find the area it drew into
void RequestScreenCapture();
int ScreenCaptureState(float rect[4]);   // 0 idle, 1 waiting, 2 done (rect = x, y, w, h in 0..1), 3-5 failed
void ResetScreenCapture();
// settings menu: mouse cursor drawn above the overlay image; positions in back-buffer pixels
void SetCursorImage(std::shared_ptr<const OverlayImage> img);
void SetCursorVisible(bool on);
void SetCursorPos(float x, float y);
void CursorPos(float& x, float& y);
bool ImageRect(float& x, float& y, float& w, float& h);   // where the overlay image was drawn last frame

// --- experimental: draw into the game's in-cab screen render target (e.g. the infotainment / GPS screen) ---
// The game renders the cab screens into their own render targets. When the game switches away from a render
// target whose size matches (targetW x targetH), the current screen image is drawn on top of what the game
// rendered (navigation etc.), so the navigation stays and reappears as soon as alpha goes back to 0.
void EnableScreenHook(bool on);   // call before Install()
void SetScreenImage(std::shared_ptr<const OverlayImage> img);
void SetScreenTarget(int w, int h, int format);   // 0,0 = none; format 0 = any
void SetScreenAlpha(float a);
void SetScreenDim(float dim);           // 0 = full brightness .. 0.9 = darkest (lights on at night: dimmed like a real display)
void SetScreenTargetIndex(int index);   // which of several textures of that size per frame: 1 = first, 2 = second, 0 = all
int ScreenTargetsSeen();                // textures of that size in the last frame the screen was drawn
void SetScreenArea(float x, float y, float w, float h, bool flipY, bool flipX = false);   // part of the render target, 0..1
void SetCollectTargets(bool on);
struct RenderTargetStat { int w, h, format; long long binds; };
std::vector<RenderTargetStat> TakeTargetStats();   // colour render targets bound since the last call (not the back buffer)
bool ScreenHookActive();
// Trucker Chat (separate plugin, through PluginApi.cpp): layer 1 of the in-cab screen and a second back-buffer picture for
// the chat. Layer 0 is the radio (the functions above). Both screen layers are drawn into a matching target, 0 first.
void SetLayerScreenImage(int layer, std::shared_ptr<const OverlayImage> img);
void SetLayerScreenTarget(int layer, int w, int h, int format);
void SetLayerScreenAlpha(int layer, float a);
void SetLayerScreenArea(int layer, float x, float y, float w, float h, bool flipY, bool flipX);
void SetChatImage(std::shared_ptr<const OverlayImage> img);   // drawn below the radio overlay and the settings menu
void SetChatAlpha(float a);
void SetChatPlacement(int anchor, float offsetX, float offsetY);
long long RenderTargetCalls();   // diagnostics: render-target changes seen by the hook
// Trucker Chat: the chat follows the radio's screen (target size / format, area, flip; changes with the truck preset)
// and waits while the radio shows a card on it (not while the radio display is pinned on the screen).
bool RadioScreenGeometry(int& w, int& h, int& format, float& x, float& y, float& aw, float& ah, bool& flipY, bool& flipX);
bool RadioCardShowing();
void SetRadioPinned(bool pinned);
}
