// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#pragma once
#include "Config.h"
#include "StationDatabase.h"
#include "ReceptionCalculator.h"
#include "AudioEngine.h"
#include "Overlay.h"

// Replaces main.js (refresh, setRadioStation, setWhitenoise, nextStation, favourites) + the server's command handling.
class RadioManager {
public:
    bool Init(const std::wstring& dataDir, const std::string& game);
    void Shutdown();
    void Tick(double x, double z, bool positionValid, bool electric, bool engine);   // called ~4x per second from the radio thread
    void OnAction(Action a);
private:
    void Refresh(double x, double z);
    void SetRadioStation(const std::string& country, size_t index, double whitenoise);
    void SetWhitenoise(double whitenoise);
    void NextStation(int amount);
    std::string ResolveUrl(const std::string& url) const;
    OverlayInfo CurrentInfo(const std::wstring& subtitleOverride = L"");
    void ShowCurrent(const std::wstring& subtitleOverride = L"", bool volumeMessage = false);
    int StationIndexByName(const std::string& country, const std::string& name) const;

    Config cfg_;
    std::wstring dir_, cfgPath_;
    StationDatabase db_;
    Favourites fav_;
    AudioEngine audio_;
    Overlay overlay_;
    DisplayWindow display_;
    void UpdateDisplay();
    bool lastBuffering_ = false;
    bool lastFailed_ = false;
    bool powered_ = true;
    void SetPower(bool on);
    int lastBars_ = -1;
    ReceptionResult last_;
    std::string currentCountry_, lastNearestCountry_;
    int currentIndex_ = -1;
    bool playing_ = false, initialReceived_ = false;
    ULONGLONG lastRefresh_ = 0, switchUntil_ = 0, swapAt_ = 0, flutterAt_ = 0;
    std::string pendingUrl_;
    bool noiseAllowed_ = true;
    struct Entry { std::string country; size_t index; double whitenoise; };
    std::vector<Entry> list_;   // g_stations
};
