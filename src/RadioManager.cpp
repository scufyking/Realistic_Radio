// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "RadioManager.h"
#include "Logger.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

bool RadioManager::Init(const std::wstring& dir, const std::string& game) {
    dir_ = dir; cfgPath_ = dir + L"local-radio.json";
    cfg_.Load(cfgPath_);
    std::string file = cfg_.dataFile.empty() ? game + ".json" : cfg_.dataFile;
    if (!db_.Load(dir + Utf8ToWide(file))) return false;
    db_.LoadCustom(dir + L"custom-stations-" + Utf8ToWide(game) + L".json");
    fav_.Load(dir + L"favourites-" + Utf8ToWide(game) + L".json");
    if (!audio_.Init(dir, cfg_.reconnectMaxSeconds)) return false;
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    playing_ = cfg_.autoPlay;
    overlay_.Init(cfg_.overlay, dir);
    display_.Start(cfg_.display);
    return true;
}
void RadioManager::Shutdown() { display_.Stop(); overlay_.Shutdown(); audio_.Shutdown(); cfg_.SaveVolume(cfgPath_); }


std::string RadioManager::ResolveUrl(const std::string& url) const {
    if (url.empty() || url.find("://") != std::string::npos || (url.size() > 1 && url[1] == ':')) return url;
    return WideToUtf8(dir_) + url;   // relative local file -> local-radio folder
}
OverlayInfo RadioManager::CurrentInfo(const std::wstring& sub) {
    OverlayInfo i;
    auto* st = db_.StationsOf(currentCountry_);
    if (!st || currentIndex_ < 0 || currentIndex_ >= (int)st->size()) { i.title = L"Local Radio"; i.subtitle = sub; return i; }
    const Station& s = (*st)[currentIndex_];
    i.headline = playing_ ? Utf8ToWide(cfg_.overlay.textNowPlaying) : Utf8ToWide(cfg_.overlay.textStopped);
    i.title = Utf8ToWide(s.name);
    auto it = last_.available.find(currentCountry_);
    i.bars = playing_ ? (it == last_.available.end() ? 0 : ReceptionBars(it->second.whitenoise)) : -1;
    i.favourite = fav_.Get(currentCountry_) == s.name;
    if (!sub.empty()) i.subtitle = sub;
    else {
        i.subtitle = Utf8ToWide(db_.CountryDisplayName(currentCountry_));
        if (playing_ && audio_.StreamFailures() > 0) i.subtitle += L"  \u2022  " + Utf8ToWide(cfg_.overlay.textUnavailable);
        else if (playing_ && audio_.IsBuffering()) i.subtitle += L"  \u2022  " + Utf8ToWide(cfg_.overlay.textConnecting);
    }
    return i;
}
void RadioManager::ShowCurrent(const std::wstring& sub) {
    auto* st = db_.StationsOf(currentCountry_);
    std::string logo = (st && currentIndex_ >= 0 && currentIndex_ < (int)st->size()) ? (*st)[currentIndex_].logo : "";
    overlay_.Show(CurrentInfo(sub), logo);
}

int RadioManager::StationIndexByName(const std::string& country, const std::string& name) const {
    auto* st = db_.StationsOf(country); if (!st || name.empty()) return -1;
    for (size_t i = 0; i < st->size(); ++i) if ((*st)[i].name == name) return (int)i;
    return -1;
}

void RadioManager::SetPower(bool on) {
    if (on == powered_) return;
    powered_ = on;
    Log::Write(on ? "Power on (%s) - radio on" : "Power off (%s) - radio off", cfg_.powerSource.c_str());
    if (!on) {
        swapAt_ = 0; switchUntil_ = 0;
        audio_.StopStream(); audio_.SetStaticGain(0, 0.3f); audio_.SetSwitchGain(0, 0.1f); audio_.SetStreamGain(0, 0.3f);
    } else if (playing_ && currentIndex_ >= 0) {
        auto it = last_.available.find(currentCountry_);
        if (it != last_.available.end()) SetRadioStation(currentCountry_, currentIndex_, it->second.whitenoise);
        else { currentCountry_.clear(); currentIndex_ = -1; }   // out of range meanwhile -> next refresh picks a station
    }
}

void RadioManager::Tick(double x, double z, bool valid, bool electric, bool engine) {
    ULONGLONG now = GetTickCount64();
    SetPower(cfg_.powerSource == "always" || !valid ? true : cfg_.powerSource == "engine" ? engine : electric);
    // station-switch timeline (port of the jQuery animate() chain in setRadioStation)
    if (swapAt_ && now >= swapAt_) {
        swapAt_ = 0;
        if (pendingUrl_.empty()) audio_.StopStream(); else audio_.PlayUrl(ResolveUrl(pendingUrl_));
        switchUntil_ = now + 2200;
    }
    if (switchUntil_ && now >= switchUntil_) {
        switchUntil_ = 0;
        audio_.SetSwitchGain(0, 0.2f);
        audio_.SetStreamGain(1, 0.05f);
        noiseAllowed_ = true;
    }
    if (!powered_) { overlay_.Tick(); UpdateDisplay(); return; }
    if (valid && now - lastRefresh_ >= 1000) { lastRefresh_ = now; Refresh(x, z); }
    if (playing_ && currentIndex_ >= 0 && !swapAt_) {   // keep the overlay in sync with connection state and reception
        bool buf = audio_.IsBuffering();
        bool failed = audio_.StreamFailures() > 0;
        if (failed != lastFailed_) {
            lastFailed_ = failed;
            if (failed) ShowCurrent();   // tell the driver right away that this station does not play
        }
        auto it = last_.available.find(currentCountry_);
        int bars = it == last_.available.end() ? 0 : ReceptionBars(it->second.whitenoise);
        if (buf != lastBuffering_ || bars != lastBars_) { lastBuffering_ = buf; lastBars_ = bars; overlay_.Update(CurrentInfo()); }
    }
    overlay_.Tick();
    UpdateDisplay();
}

// Infotainment screen content (captured by PrismTextureStreamer)
void RadioManager::UpdateDisplay() {
    DisplayState d;
    d.powered = powered_; d.playing = playing_; d.muted = cfg_.muted;
    d.volume = (int)std::lround(cfg_.volume * 100);
    OverlayInfo i = CurrentInfo();
    d.headline = i.headline; d.title = i.title; d.bars = i.bars; d.favourite = i.favourite;
    auto* st = db_.StationsOf(currentCountry_);
    if (st && currentIndex_ >= 0) {
        d.country = Utf8ToWide(db_.CountryDisplayName(currentCountry_));
        if (!playing_) d.status = Utf8ToWide(cfg_.overlay.textStopped);
        else if (audio_.StreamFailures() > 0) d.status = Utf8ToWide(cfg_.overlay.textUnavailable);
        else if (audio_.IsBuffering() || swapAt_) d.status = Utf8ToWide(cfg_.overlay.textConnecting);
    } else {
        d.title = L"Local Radio"; d.headline.clear(); d.bars = -1;
        d.status = Utf8ToWide(cfg_.overlay.textConnecting);
    }
    // neighbours in the same order as Next/Previous
    if (!list_.empty()) {
        int idx = -1, n = (int)list_.size();
        for (int k = 0; k < n; ++k) if (list_[k].country == currentCountry_ && (int)list_[k].index == currentIndex_) { idx = k; break; }
        if (idx >= 0 && n > 1) {
            auto name = [&](int k) { const auto& e = list_[((k % n) + n) % n]; return Utf8ToWide((*db_.StationsOf(e.country))[e.index].name); };
            d.prevName = name(idx - 1); d.nextName = name(idx + 1);
        }
    }
    d.footer = d.favourite ? L"\u2665  Favorit" : std::to_wstring(list_.size()) + L" Sender in Reichweite";
    display_.Update(d, overlay_.CurrentLogo());
}

void RadioManager::Refresh(double x, double z) {
    ReceptionResult r = CalculateReception(db_, x, z, cfg_.radius);
    if (!r.valid) return;
    if (r.nearestCountry != last_.nearestCountry || r.nearestCity != last_.nearestCity)
        Log::Write("Nearest city: %s (%s), %.0f m; best reception: %s", r.nearestCity.c_str(), r.nearestCountry.c_str(), r.nearestDistance, r.bestCountry.c_str());
    bool countriesChanged = r.available.size() != last_.available.size() ||
        !std::equal(r.available.begin(), r.available.end(), last_.available.begin(), [](auto& a, auto& b) { return a.first == b.first; });
    last_ = r;
    // g_stations
    list_.clear();
    for (auto& [k, v] : r.available) if (auto* st = db_.StationsOf(k)) for (size_t i = 0; i < st->size(); ++i) list_.push_back({k, i, v.whitenoise});

    if (playing_ || !initialReceived_) {
        initialReceived_ = true;
        if (!playing_) return;
        auto cur = r.available.find(currentCountry_);
        const auto& best = r.available[r.bestCountry];
        if (cur == r.available.end() ||
            (best.whitenoise + cfg_.threshold < cur->second.whitenoise && lastNearestCountry_ != r.bestCountry)) {
            lastNearestCountry_ = r.bestCountry;
            if (db_.StationsOf(r.bestCountry)) {
                int idx = StationIndexByName(r.bestCountry, fav_.Get(r.bestCountry));
                SetRadioStation(r.bestCountry, idx < 0 ? 0 : idx, best.whitenoise);
            } else Log::Write("No stations for country '%s'", r.bestCountry.c_str());
        } else if (!countriesChanged && cur != r.available.end()) {
            SetWhitenoise(cur->second.whitenoise);
        }
    }
}

void RadioManager::SetRadioStation(const std::string& country, size_t index, double whitenoise) {
    auto* st = db_.StationsOf(country); if (!st || index >= st->size()) return;
    currentCountry_ = country; currentIndex_ = (int)index;
    const Station& s = (*st)[index];
    auto it = last_.available.find(country);
    Log::Write("Station: %s [%s] reception %d/5", s.name.c_str(), country.c_str(), it == last_.available.end() ? 0 : ReceptionBars(it->second.whitenoise));
    playing_ = true;
    noiseAllowed_ = false;
    audio_.SetStaticGain(0, 0.3f);
    audio_.SetSwitchGain(cfg_.transitionWhitenoise && !s.url.empty() ? 1.f : 0.f, 2.5f);
    audio_.SetStreamGain(0, 2.0f);
    pendingUrl_ = s.url;
    swapAt_ = GetTickCount64() + 2000;
    SetWhitenoise(whitenoise);
    lastBuffering_ = true; lastFailed_ = false;
    ShowCurrent();
}

// Port of setWhitenoise(): static volume grows with the square of the relative distance; weak signal makes the
// programme "flutter" (random drop-outs) exactly like the original.
void RadioManager::SetWhitenoise(double w) {
    if (!cfg_.whitenoise || !noiseAllowed_) { if (!cfg_.whitenoise) audio_.SetStaticGain(0, 0.5f); return; }
    double nv = std::clamp(w * w - 0.15, 0.0, 1.0);
    double player = 1.0;
    if (nv > 0.5) {
        player = std::clamp(audio_.StreamGain() + ((double)rand() / RAND_MAX - 0.45) / 2.0, 0.0, 1.0);
        audio_.SetStreamGain((float)player, 1.0f);
    } else audio_.SetStreamGain(1.f, 0.3f);
    audio_.SetStaticGain((float)nv, 0.5f);
}

void RadioManager::NextStation(int amount) {
    if (list_.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < list_.size(); ++i) if (list_[i].country == currentCountry_ && (int)list_[i].index == currentIndex_) { idx = (int)i; break; }
    int n = (int)list_.size();
    idx = ((idx + amount) % n + n) % n;
    SetRadioStation(list_[idx].country, list_[idx].index, list_[idx].whitenoise);
}

void RadioManager::OnAction(Action a) {
    if (!powered_ && a != Action::VolumeUp && a != Action::VolumeDown && a != Action::Mute) {
        overlay_.Show(OverlayInfo{L"", L"Local Radio", Utf8ToWide(cfg_.overlay.textPowerOff), -1, false}, "");
        return;
    }
    switch (a) {
    case Action::Next: NextStation(1); break;
    case Action::Previous: NextStation(-1); break;
    case Action::Stop:
        if (playing_) { playing_ = false; swapAt_ = 0; audio_.StopStream(); audio_.SetStaticGain(0, 0.2f); audio_.SetSwitchGain(0, 0.1f); Log::Write("Playback stopped"); ShowCurrent(); }
        else if (currentIndex_ >= 0) { auto it = last_.available.find(currentCountry_); SetRadioStation(currentCountry_, currentIndex_, it == last_.available.end() ? 0 : it->second.whitenoise); }
        else { playing_ = true; initialReceived_ = false; currentCountry_.clear(); lastNearestCountry_.clear(); }
        break;
    case Action::VolumeUp: case Action::VolumeDown: {
        double v = std::round(cfg_.volume * 20.0) + (a == Action::VolumeUp ? 1 : -1);   // 5 % steps
        cfg_.volume = std::clamp(v, 0.0, 20.0) / 20.0; audio_.SetMaster((float)cfg_.volume);
        Log::Write("Volume %.0f%%", cfg_.volume * 100);
        ShowCurrent(Utf8ToWide(cfg_.overlay.textVolume) + L" " + std::to_wstring((int)std::lround(cfg_.volume * 100)) + L" %");
        break; }
    case Action::Mute: cfg_.muted = !cfg_.muted; audio_.SetMuted(cfg_.muted); Log::Write(cfg_.muted ? "Muted" : "Unmuted");
        ShowCurrent(cfg_.muted ? Utf8ToWide(cfg_.overlay.textMuted) : Utf8ToWide(cfg_.overlay.textVolume) + L" " + std::to_wstring((int)std::lround(cfg_.volume * 100)) + L" %");
        break;
    case Action::MakeFavourite:
        if (auto* st = db_.StationsOf(currentCountry_); st && currentIndex_ >= 0) {
            fav_.Set(currentCountry_, (*st)[currentIndex_].name);
            Log::Write("Favourite for %s: %s", currentCountry_.c_str(), (*st)[currentIndex_].name.c_str());
            ShowCurrent(Utf8ToWide(cfg_.overlay.textFavourite));
        }
        break;
    case Action::GoToFavourite: {   // port of the "goToFavourite" command: cycles through favourites of countries in range
        std::string country = currentCountry_;
        int idx = StationIndexByName(country, fav_.Get(country));
        if (idx < 0 || idx == currentIndex_) {
            std::vector<std::string> favCountries;
            for (auto& [k, v] : last_.available) if (fav_.Has(k) && db_.StationsOf(k)) favCountries.push_back(k);
            if (favCountries.empty()) break;
            auto it = std::find(favCountries.begin(), favCountries.end(), currentCountry_);
            country = (it == favCountries.end() || it + 1 == favCountries.end()) ? favCountries.front() : *(it + 1);
            idx = std::max(0, StationIndexByName(country, fav_.Get(country)));
        }
        SetRadioStation(country, idx, last_.available.count(country) ? last_.available[country].whitenoise : 0);
        break; }
    case Action::ShowOverlay:
        ShowCurrent();
        break;
    default: break;
    }
}
