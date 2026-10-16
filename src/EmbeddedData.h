// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <string>

// Writes the data files embedded in the DLL (station lists, sounds, example file) into `dir`.
// Runs on every start but only rewrites files when the embedded data differs from what is on disk
// (tracked in data-version.txt). User files (settings, favourites, own stations, logos) are never touched.
bool ExtractEmbeddedData(const std::wstring& dir);
