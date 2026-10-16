// ETS2 Local Radio Native
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
}
