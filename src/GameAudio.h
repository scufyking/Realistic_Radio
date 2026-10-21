// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// The game's own sound system (FMOD Studio, fmodstudio.dll / fmod.dll next to the game), beta:
//  * "cabin" output: the radio is played inside the game's sound mix on its radio bus (bus:/radio), like the game's own
//    radio: cab acoustics, open windows, the game's volume settings, quieter outside the truck.
//  * the game's own radio is muted while Realistic Radio plays (two radios at once otherwise).
// The game's FMOD Studio system is taken from its per-frame update call (code hook); all FMOD calls are made inside that
// call, i.e. on the game's own thread. FMOD then pulls our audio through a user-created stream.
#include <string>

namespace GameAudio {
using PullFn = void (*)(float* out, unsigned frames, void* user);   // 48 kHz stereo float, called on FMOD's stream thread
// cabin: play through the game; muteGameRadio: silence the game's radio while ours plays; bus: e.g. "bus:/radio".
// pull = nullptr stops our sound (call it before the audio engine goes away).
void Configure(bool cabin, bool muteGameRadio, const std::string& bus, PullFn pull, void* user);
void SetRadioAudible(bool on);   // Realistic Radio is playing something right now (for muting the game's radio)
bool Playing();                  // our sound is playing inside the game's mix (the audio device must stay silent)
// 0 = off, 1 = waiting for the game's sound system, 2 = playing in the cab, -1 = not possible (see the log)
int State();
// "Quieter outside the truck": the camera position as the game gives it to its sound system (FMOD listener) and how
// the game mixes the cab buses (final volumes, -1 = unknown). WatchListener(true) also installs the hook when needed
// (call Configure afterwards) and logs the game's mix changes for diagnosis.
void WatchListener(bool on);
bool Listener(float pos[3], unsigned maxAgeMs = 500);
// the game's own cab state: cabin_out (0 = camera in the cab, 1 = outside) and the windows (wnd_left / wnd_right);
// false while the game has not reported it
bool CabState(float& cabinOut, float& windowLeft, float& windowRight);
void CabMix(float& windowLeft, float& windowRight, float& windowOut, float& interior);
}
