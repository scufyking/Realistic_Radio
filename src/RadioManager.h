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
#include "TunnelMap.h"
#include <set>
#include <unordered_map>

// Replaces main.js (refresh, setRadioStation, setWhitenoise, nextStation, favourites) + the server's command handling.
class RadioManager {
public:
    bool Init(const std::wstring& dataDir, const std::string& game);
    void Shutdown();
    void Tick(double x, double z, bool positionValid, bool electric, bool engine, float speed = 0.f);   // speed in m/s   // called ~4x per second from the radio thread
    void OnAction(Action a, bool hold = false);   // hold: station button held long enough (= save)
    // key menu (Ctrl+0)
    bool MenuOpen() const { return menu_.IsOpen(); }
    bool MenuTick(InputManager& input);   // true = menu closed and new bindings applied (re-init the input)
    const Config& Settings() const { return cfg_; }
    void SetTruck(const std::string& id, const std::string& name, const std::string& brandId = "", const std::string& brand = "",
                  const std::string& model = "");   // truck changed: load its preset (presetAuto)
private:
    void Refresh(double x, double z);
    // variant: the same programme in a neighbouring region (e.g. SWR1 BW -> SWR1 RP): changes without tuning noise
    void SetRadioStation(const std::string& country, size_t index, double whitenoise, bool variant = false);
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
    std::wstring song_;                // live song title of the station (shown on the screen card)
    ULONGLONG songAt_ = 0;
    bool powered_ = true;
    void SetPower(bool on);
    int lastBars_ = -1;
    ReceptionResult last_;
    std::string currentCountry_, lastNearestCountry_;
    int currentIndex_ = -1;
    bool playing_ = false, initialReceived_ = false;
    ULONGLONG lastRefresh_ = 0, switchUntil_ = 0, swapAt_ = 0, flutterAt_ = 0;
    ULONGLONG menuOpenAt_ = 0; long long menuFrames_ = 0;   // the menu must really be drawn, else it closes again
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
    std::string truckId_, truckName_, brandId_, brand_, model_;
    bool lastElectric_ = false;
    ULONGLONG startLogoAt_ = 0;   // ignition on: start logo as soon as the truck is known
    // radio memory per truck (truckMemory): realistic-radio\trucks-<game>.json
    struct TruckMemory {
        std::string name, source = "radio", mediaTrack;
        double volume = -1; bool muted = false;
        std::string band;
        int freq = 0;
        std::map<std::string, std::string> mediaResume, stations, favourites;   // stations / favourites: country -> station name
        std::map<std::string, std::string> buttons;                             // station buttons "1".. -> "<region>|<name>"
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
    // 1.4.0: FM / DAB+, same programme in several regions, station buttons
    bool Dab() const { return cfg_.band == "dab"; }
    std::string FamilyKey(const std::string& region, const std::string& name) const;   // "germany|swr1" for SWR1 BW and SWR1 RP
    std::string RawFamilyKey(const std::string& region, const std::string& name) const;
    void BuildFamilies();                                     // place names + keys that are not unique within a region
    std::set<std::string> places_;
    std::map<std::string, std::set<std::string>> famAmbiguous_;
    mutable std::unordered_map<std::string, std::string> famCache_;
    ULONGLONG lostSince_ = 0;                                 // current station out of range since (switch after a few seconds)
    std::string CurrentFamily() const;
    bool FindVariant(const std::string& family, std::string& region, int& index) const;   // best received region with it
    void PickStation(const std::string& region, double whitenoise);   // truck memory, favourite or the first station
    void StationButton(int n, bool hold);
    std::string ButtonGet(int n) const;
    void ButtonSet(int n, const std::string& value);
    int CurrentButton() const;   // station button of the current station, 0 = none
    void ToggleBand();
    // frequency radio (FM): the dial like an old car radio
    bool FreqMode() const { return cfg_.tuning == "frequency" && !Dab(); }
    struct OnFreq { std::string region; int index; double w; };
    std::vector<OnFreq> StationsOn(int freq) const;     // receivable stations on this frequency, best first
    void RefreshFreq(const ReceptionResult& r);
    void SetTuned(int freq);                            // tune to a frequency (station or only static)
    void GoSilent();                                    // nothing on the frequency: static only
    void SeekFreq(int dir);                             // Next / Previous: next frequency with a station
    std::wstring FreqText(int freq) const;              // "94,7 MHz"
    int tuned_ = 0;                                     // 0.1 MHz
    ULONGLONG flipUntil_ = 0;                           // two stations on one frequency: the weaker one for a moment
    bool silent_ = false;
    int AutoStore(bool overwrite);   // station buttons from the best received stations here; returns how many were set
    bool autoStoreChecked_ = false;  // empty station buttons are filled once after the first reception
    ULONGLONG switchHoldMs_ = 2200;
    // tunnels: no reception inside (TunnelMap)
    TunnelMap tunnels_;
    void UpdateTunnel(double x, double z, bool valid, ULONGLONG now);
    bool inTunnel_ = false;
    float tunnelDepth_ = 0.f, tunMul_ = 1.f, tunAdd_ = 0.f;
    ULONGLONG tunEnterAt_ = 0, tunExitAt_ = 0, tunStepAt_ = 0, tunCheckAt_ = 0;
    double lastX_ = 0, lastZ_ = 0;
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
