// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Keys other plugins already use (1.7.0): SPF Framework plugins such as MotionCab (Dynamic Head-Motion) keep their
// key bindings in plugins\spfPlugins\<plugin>\config\settings.json, the framework in plugins\spfAssets\config\
// framework_settings.json. A radio key on the same key would fire both (MotionCab glances left on NumPad 7 while the
// radio switches to - or, held, overwrites - station button 7), so the radio leaves those keys to the other plugin.
#include "Common.h"

namespace ForeignKeys {
struct Key { int vk = 0; std::string owner; };
// pluginsDir: the game's bin\win_x64\plugins\ folder (with backslash at the end)
std::vector<Key> Scan(const std::wstring& pluginsDir);
}
