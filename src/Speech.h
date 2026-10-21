// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Voices for the virtual phone: a text is spoken by a Windows voice (SAPI, the voices installed in Windows) on a
// background thread and comes back as 48 kHz mono samples that already sound like a phone call (band limited).
// Plus the ringtone. Nothing here touches the game.
#include "Common.h"
#include <memory>

namespace Speech {
using Clip = std::shared_ptr<const std::vector<float>>;
// lang: "de" or "en" (a voice of that language is preferred when Windows has one). id identifies the result.
void Request(int id, const std::wstring& text, const std::string& lang);
bool Take(int& id, Clip& clip);   // a finished request (clip == nullptr: no voice available)
void Shutdown();
Clip Ringtone();                  // about 3 s: ring - ring - pause (meant to loop)
}
