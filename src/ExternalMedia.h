// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Music from another app on the PC (1.7.0): the add-on "Realistic Radio Phone Media" (realistic-radio-media.dll) reads
// what Windows plays (Spotify, YouTube Music, a browser, ...) and hands it over through PluginApi.cpp. The phone shows
// it as an app with cover and buttons; the buttons go back to the add-on as commands. Thread-safe.
#include "OverlayImage.h"
#include <memory>
#include <string>

namespace ExternalMedia {
enum Status { None = 0, Stopped = 1, Paused = 2, Playing = 3, Changing = 4 };
enum Command { CmdNone = 0, CmdPlayPause = 1, CmdNext = 2, CmdPrevious = 3 };
enum Caps { CanPlayPause = 1, CanNext = 2, CanPrevious = 4 };
struct State {
    bool addon = false;              // the add-on is running (heard from in the last seconds)
    bool session = false;            // an app plays or paused something
    std::wstring addonName, app;     // "Spotify"
    std::wstring title, artist, album;
    int status = None;
    int caps = 0;
    double pos = -1, len = -1;       // seconds; pos runs on while playing
    std::shared_ptr<OverlayImage> cover;
    unsigned serial = 0;             // changes with every new state (not with the running position)
    unsigned trackSerial = 0;        // changes when the title changes
    unsigned coverSerial = 0;
};
State Get();
// from the add-on (PluginApi.cpp)
void Register(const std::wstring& name);
void Set(const std::wstring& app, const std::wstring& title, const std::wstring& artist, const std::wstring& album, int status, long long posMs, long long lenMs, int caps);
void SetCover(std::shared_ptr<OverlayImage> cover);
void Clear();                        // nothing plays
int TakeCommand();
// from the radio
void Command(int cmd);
}
