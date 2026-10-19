// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"

// Tunnels of the map: radio reception is lost inside. The databases come from "ETS2 Tunnel Radio" by Tal
// (bohbotoly), MIT license (see NOTICE.md): built from the game's own covered zones ("no weather" zones, where the
// rain stops), only stretches of 200 m and more. Each tunnel is a line on the X/Z plane with a half width.
// ETS2 1.60 vanilla incl. map DLCs, and ProMods 2.83 + Middle-East (picked when game.log.txt shows ProMods).
// Own tunnels (map mods, missing ones): realistic-radio\tunnels-own.json, recorded with the "tunnel" key.
class TunnelMap {
public:
    void Init(const std::wstring& dataDir, const std::string& game);
    void Tick();                                   // every few seconds: map mods changed (profile loaded)?
    bool Inside(double x, double z) const;
    // "tunnel" key: first press = entry, second press = exit -> saved to tunnels-own.json. Returns a message.
    std::wstring Mark(double x, double z, bool english);
    size_t Count() const { return segs_.size() + own_.size(); }
private:
    struct Seg { float ax, az, bx, bz, hw; };
    void LoadDb(bool promods);
    void LoadOwn();
    bool DetectProMods();
    std::vector<Seg> segs_, own_;
    std::wstring dir_, gameLog_;
    std::string game_;
    int db_ = -1;                                  // 0 vanilla, 1 ProMods
    ULONGLONG nextCheck_ = 0;
    long long logSize_ = -1;
    bool haveEntry_ = false; double entX_ = 0, entZ_ = 0;
};
