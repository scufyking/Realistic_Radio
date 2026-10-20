// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

#define REALISTIC_RADIO_VERSION "1.4.7"
#define REALISTIC_RADIO_VERSION_W L"1.4.7"

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
std::wstring PluginDataDir();   // <folder of DLL>\realistic-radio\  (with trailing backslash)
