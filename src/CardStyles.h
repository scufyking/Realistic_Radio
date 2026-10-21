// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Own designs for the in-cab card: JSON files in realistic-radio\styles\ (one file = one style, chosen as design
// "style:<file name without .json>"). A style picks one of the layouts (standard, classic, smart, presets), starts from
// the colours of a built-in design and changes what it lists; optionally a picture as background. Files are read again
// when they change, so a style can be edited while the game runs.
#include <string>
#include <vector>

void InitCardStyles(const std::wstring& stylesDir);   // creates the folder with a template and a read-me
struct CardStyleEntry { std::string id; std::wstring name; };
std::vector<CardStyleEntry> ListCustomStyles();      // all usable files (not those starting with "_"), sorted by name
bool StyleUsesTiles(const std::string& design);      // the station-list layout (shows the station buttons)
