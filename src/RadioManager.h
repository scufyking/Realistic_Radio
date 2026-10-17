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
#include "MediaLibrary.h"
#include "SettingsMenu.h"

// Replaces main.js (refresh, setRadioStation, setWhitenoise, nextStation, favourites) + the server's command handling.
class RadioManager {
public:
    bool Init(const std::wstring& dataDir, const std::string& game);
    void Shutdown();
    void Tick(double x, double z, bool positionValid, bool electric, bool engine, float speed = 0.f);   // speed in m/s   // called ~4x per second from the radio thread
    void OnAction(Action a);
    // key menu (Ctrl+0)
    bool MenuOpen() const { return menu_.IsOpen(); }
    bool MenuTick(InputManager& input);   // true = menu closed and new bindings applied (re-init the input)
    const Config& Settings() const { return cfg_; }
    void SetTruck(const std::string& id, const std::string& name);   // truck changed: load its preset (presetAuto)
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

    // USB / CD mode
    enum class Source { Radio, Media };
    Source source_ = Source::Radio;
    SettingsMenu menu_;
    float speedBoost_ = 0.f;     // speed-dependent volume, smoothed (0..1 of the configured boost)
    ULONGLONG lastSpeedAt_ = 0;
    void UpdateSpeedVolume(float speed, ULONGLONG now);
    void ApplySettings(const Config& e);
    std::string truckId_, truckName_;
    // radio memory per truck (truckMemory): realistic-radio\trucks-<game>.json
    struct TruckMemory {
        std::string name, source = "radio", mediaTrack;
        double volume = -1; bool muted = false;
        std::map<std::string, std::string> mediaResume, stations, favourites;   // stations / favourites: country -> station name
    };
    std::map<std::string, TruckMemory> mem_;
    std::wstring memPath_;
    std::string memSaved_;
    ULONGLONG memStoreAt_ = 0;
    void LoadMemory();
    void SaveMemory();
    void StoreMemory();                    // current state -> memory of truckId_
    void RestoreMemory();                  // memory of truckId_ -> current state (station, source, volume, ...)
    bool UseMemory() const { return cfg_.truckMemory && !truckId_.empty(); }
    std::string FavGet(const std::string& country) const;   // favourite of the truck (truckMemory), else the general one
    bool FavHas(const std::string& country) const;
    MediaLibrary lib_;
    std::vector<int> order_;    // play order (shuffled or folder order), indices into lib_.Tracks()
    int orderPos_ = -1, track_ = -1, disc_ = -1;
    bool mediaPaused_ = false;
    int mediaFailStreak_ = 0;
    ULONGLONG lastMediaInfoAt_ = 0;
    std::string mediaLogo_;
    std::wstring MediaFolder() const;
    void ToggleSource();
    bool EnterMedia(int disc, bool rescan);
    void StepDisc(int dir);
    std::string DiscKey() const;
    void EnterRadio();
    void BuildOrder();
    void PlayTrack(int orderPos, bool show);
    void StepTrack(int dir);
    void TickMedia(ULONGLONG now);
    OverlayInfo MediaInfo(const std::wstring& sub);
    std::string DiscType(int disc) const;
    std::wstring DiscChip(int disc, std::string* typeOut = nullptr) const;
};
