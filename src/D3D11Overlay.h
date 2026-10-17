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
void SetScreenArea(float x, float y, float w, float h, bool flipY, bool flipX = false);   // part of the render target, 0..1
void SetCollectTargets(bool on);
struct RenderTargetStat { int w, h, format; long long binds; };
std::vector<RenderTargetStat> TakeTargetStats();   // colour render targets bound since the last call (not the back buffer)
bool ScreenHookActive();
long long RenderTargetCalls();   // diagnostics: render-target changes seen by the hook
}
