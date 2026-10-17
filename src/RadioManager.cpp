// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "RadioManager.h"
#include "Logger.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <shellapi.h>

bool RadioManager::Init(const std::wstring& dir, const std::string& game) {
    dir_ = dir; cfgPath_ = dir + L"realistic-radio.json";
    cfg_.Load(cfgPath_);
    std::string file = cfg_.dataFile.empty() ? game + ".json" : cfg_.dataFile;
    if (!db_.Load(dir + Utf8ToWide(file))) return false;
    db_.LoadCustom(dir + L"custom-stations-" + Utf8ToWide(game) + L".json");
    fav_.Load(dir + L"favourites-" + Utf8ToWide(game) + L".json");
    if (!audio_.Init(dir, cfg_.reconnectMaxSeconds)) return false;
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    playing_ = cfg_.autoPlay;
    overlay_.Init(cfg_.overlay, cfg_.screen, dir);
    display_.Start(cfg_.display);
    if (cfg_.media.enabled) {
        lib_.Scan(MediaFolder(), cfg_.media.folderPerDisc);   // creates realistic-radio\usb\ on first start
        if (cfg_.source == "usb") EnterMedia(-1, false);
    }
    overlay_.Update(CurrentInfo());
    overlay_.SetScreenPinned(cfg_.screenPage == "radio");   // radio page stays on the screen from the last session
    return true;
}

// ---------------------------------------------------------------------------------------------
// USB / CD mode
std::wstring RadioManager::MediaFolder() const {
    std::wstring f = Utf8ToWide(cfg_.media.folder.empty() ? "usb" : cfg_.media.folder);
    for (auto& c : f) if (c == L'/') c = L'\\';
    bool absolute = (f.size() > 1 && f[1] == L':') || f.rfind(L"\\\\", 0) == 0;
    if (!absolute) f = dir_ + f;
    if (f.back() != L'\\') f += L'\\';
    return f;
}

void RadioManager::BuildOrder() {   // play order of the current USB / CD
    order_.clear();
    if (disc_ < 0 || disc_ >= (int)lib_.Discs().size()) return;
    const MediaDisc& d = lib_.Discs()[disc_];
    for (int i = 0; i < d.count; ++i) order_.push_back(d.first + i);
    if (cfg_.media.shuffle && order_.size() > 1) { std::mt19937 rng((unsigned)GetTickCount64()); std::shuffle(order_.begin(), order_.end(), rng); }
}

std::string RadioManager::DiscKey() const {
    return disc_ >= 0 && disc_ < (int)lib_.Discs().size() ? WideToUtf8(lib_.Discs()[disc_].name) : "";
}

// disc: index of the USB / CD, -1 = the one of the last played track (game start), -2 = the last one
bool RadioManager::EnterMedia(int disc, bool rescan) {
    if (rescan) lib_.Scan(MediaFolder(), cfg_.media.folderPerDisc);   // pick up files added while the game is running
    const int n = (int)lib_.Discs().size();
    if (n == 0) {
        cfg_.source = "radio";
        OverlayInfo i; i.source = Utf8ToWide(cfg_.media.label); i.media = true; i.paused = true;
        i.title = Utf8ToWide(cfg_.media.textEmpty); i.lcd = i.title;
        i.subtitle = L"realistic-radio\\" + Utf8ToWide(cfg_.media.folder);
        overlay_.Show(i, "");
        return false;
    }
    if (disc == -1) disc = std::max(0, lib_.DiscOf(lib_.IndexOf(Utf8ToWide(cfg_.mediaTrack))));
    else if (disc == -2) disc = n - 1;
    disc = ((disc % n) + n) % n;
    if (source_ == Source::Radio) {
        swapAt_ = 0; switchUntil_ = 0; pendingUrl_.clear();
        audio_.StopStream(); audio_.SetStaticGain(0, 0.2f); audio_.SetSwitchGain(0, 0.1f);
    }
    source_ = Source::Media; cfg_.source = "usb";
    disc_ = disc;
    BuildOrder();
    // resume where this USB / CD was left; 1.1.0 only stored "mediaTrack", which is used if it belongs to this disc
    std::string rel;
    auto it = cfg_.mediaResume.find(DiscKey());
    if (it != cfg_.mediaResume.end()) rel = it->second;
    else if (lib_.DiscOf(lib_.IndexOf(Utf8ToWide(cfg_.mediaTrack))) == disc_) rel = cfg_.mediaTrack;
    int idx = lib_.IndexOf(Utf8ToWide(rel));
    int pos = 0;
    if (idx >= 0) for (int k = 0; k < (int)order_.size(); ++k) if (order_[k] == idx) { pos = k; break; }
    mediaFailStreak_ = 0;
    Log::Write("Source: %s %d/%d \"%s\" (%d tracks)", cfg_.media.label.c_str(), disc_ + 1, n, DiscKey().c_str(), (int)order_.size());
    PlayTrack(pos, true);
    return true;
}

void RadioManager::EnterRadio() {
    source_ = Source::Radio; cfg_.source = "radio";
    audio_.StopStream(); audio_.SetPaused(false);
    track_ = -1; disc_ = -1; mediaPaused_ = false; mediaLogo_.clear();
    Log::Write("Source: radio");
    playing_ = true;
    auto it = last_.available.find(currentCountry_);
    if (currentIndex_ >= 0 && it != last_.available.end()) SetRadioStation(currentCountry_, currentIndex_, it->second.whitenoise);
    else { currentCountry_.clear(); currentIndex_ = -1; lastNearestCountry_.clear(); lastRefresh_ = 0; ShowCurrent(); }   // next refresh picks a station
}

// Source key, like on a car radio: Radio -> USB 1 -> USB 2 -> ... -> Radio
void RadioManager::ToggleSource() {
    if (!cfg_.media.enabled) { Log::Write("USB mode is disabled (media.enabled = false)"); return; }
    if (source_ == Source::Radio) EnterMedia(0, true);
    else if (disc_ + 1 < (int)lib_.Discs().size()) EnterMedia(disc_ + 1, false);
    else EnterRadio();
}

// nextDisc / previousDisc keys: change the USB / CD directly (from the radio: first / last one)
void RadioManager::StepDisc(int dir) {
    if (!cfg_.media.enabled) return;
    if (source_ == Source::Radio) EnterMedia(dir > 0 ? 0 : -2, true);
    else EnterMedia(disc_ + dir, false);
}

void RadioManager::PlayTrack(int pos, bool show) {
    if (order_.empty()) return;
    pos = ((pos % (int)order_.size()) + (int)order_.size()) % (int)order_.size();
    orderPos_ = pos; track_ = order_[pos];
    const MediaTrack& t = lib_.Tracks()[track_];
    audio_.SetStaticGain(0, 0.2f); audio_.SetSwitchGain(0, 0.1f); audio_.SetStreamGain(1, 0.05f);
    audio_.PlayUrl(WideToUtf8(t.path), true);
    mediaPaused_ = false;
    audio_.SetPaused(!powered_);
    cfg_.mediaTrack = WideToUtf8(t.rel);
    cfg_.mediaResume[DiscKey()] = cfg_.mediaTrack;
    std::wstring cover = lib_.CoverFor(t, dir_ + L"logos\\");
    mediaLogo_ = cover.empty() ? "" : "file:" + WideToUtf8(cover);
    Log::Write("USB track %d/%d: %s", pos + 1, (int)order_.size(), WideToUtf8(t.rel).c_str());
    if (show) ShowCurrent();
}

void RadioManager::StepTrack(int dir) {
    if (order_.empty()) return;
    if (dir < 0 && audio_.Track().pos > 3.0) { PlayTrack(orderPos_, true); return; }   // like a CD player: back = start of the track first
    PlayTrack(orderPos_ + dir, true);
}

void RadioManager::TickMedia(ULONGLONG now) {
    if (track_ < 0 || order_.empty()) return;
    auto t = audio_.Track();
    if (!mediaPaused_ && t.active && t.ended) {
        if (t.failed) {
            if (++mediaFailStreak_ >= (int)order_.size()) {   // nothing in the folder can be played
                mediaPaused_ = true; audio_.StopStream();
                Log::Write("USB: none of the files could be played");
                ShowCurrent(Utf8ToWide(cfg_.overlay.textUnavailable));
                return;
            }
            PlayTrack(orderPos_ + 1, true);
        } else {
            mediaFailStreak_ = 0;
            if (cfg_.media.repeat == "one") PlayTrack(orderPos_, false);
            else if (cfg_.media.repeat == "off" && orderPos_ + 1 >= (int)order_.size()) {   // end of the list: stop on track 1
                audio_.StopStream(); orderPos_ = 0; track_ = order_[0]; mediaPaused_ = true;
                cfg_.mediaTrack = WideToUtf8(lib_.Tracks()[track_].rel);
                cfg_.mediaResume[DiscKey()] = cfg_.mediaTrack;
                std::wstring cover = lib_.CoverFor(lib_.Tracks()[track_], dir_ + L"logos\\");
                mediaLogo_ = cover.empty() ? "" : "file:" + WideToUtf8(cover);
                ShowCurrent();
            } else PlayTrack(orderPos_ + 1, true);
        }
        return;
    }
    if (now - lastMediaInfoAt_ >= 500) { lastMediaInfoAt_ = now; overlay_.Update(CurrentInfo()); }   // running time on the card
}

// Media type of a music folder: media.types[folder] or, if not set, media.label (USB / CD / ... or any own text)
std::string RadioManager::DiscType(int disc) const {
    if (disc >= 0 && disc < (int)lib_.Discs().size()) {
        auto it = cfg_.media.types.find(WideToUtf8(lib_.Discs()[disc].name));
        if (it != cfg_.media.types.end() && !MediaTypeId(it->second).empty()) return MediaTypeId(it->second);
    }
    return MediaTypeId(cfg_.media.label);   // "" = own label text
}
// Text of the source tag, numbered when several folders have the same type ("USB 2", "CD 1")
std::wstring RadioManager::DiscChip(int disc, std::string* typeOut) const {
    std::string t = DiscType(disc);
    if (typeOut) *typeOut = t.empty() ? "usb" : t;
    std::wstring chip = t.empty() ? Utf8ToWide(cfg_.media.label) : MediaTypeLabel(t, cfg_.menuLanguage == "en");
    int same = 0, nr = 0;
    for (int k = 0; k < (int)lib_.Discs().size(); ++k) if (DiscType(k) == t) { ++same; if (k == disc) nr = same; }
    if (same > 1 && nr > 0) chip += L" " + std::to_wstring(nr);
    return chip;
}

OverlayInfo RadioManager::MediaInfo(const std::wstring& sub) {
    OverlayInfo i;
    i.media = true; i.paused = mediaPaused_;
    const int discs = (int)lib_.Discs().size();
    i.source = DiscChip(disc_, &i.mediaType);   // "USB 2", "CD", "KASSETTE" ...
    std::wstring discName = disc_ >= 0 && disc_ < discs ? lib_.Discs()[disc_].name : L"";
    if (track_ < 0 || track_ >= (int)lib_.Tracks().size()) { i.title = Utf8ToWide(cfg_.media.textEmpty); i.lcd = i.title; return i; }
    const MediaTrack& t = lib_.Tracks()[track_];
    i.title = t.title; i.subtitle = t.artist; i.detail = t.album;
    i.headline = Utf8ToWide(cfg_.media.textTrack) + L" " + std::to_wstring(orderPos_ + 1) + L" / " + std::to_wstring(order_.size());
    if (!discName.empty()) i.headline = discName + L"  \u2022  " + i.headline;   // folder name of the USB / CD
    if (!discName.empty() && i.detail == discName) i.detail.clear();              // album fallback = folder name: shown once
    if (mediaPaused_) i.headline += L"  \u2022  " + Utf8ToWide(cfg_.media.textPaused);
    auto ts = audio_.Track();
    if (ts.active) { i.posSec = (int)ts.pos; i.lenSec = ts.len > 0 ? (int)(ts.len + 0.5) : -1; }
    else { i.posSec = 0; i.lenSec = -1; }
    i.lcd = t.artist.empty() ? t.title : t.artist + L" - " + t.title;
    if (mediaPaused_) i.lcd = Utf8ToWide(cfg_.media.textPaused) + L": " + i.lcd;
    if (!sub.empty()) { i.subtitle = sub; i.lcd = sub; }
    return i;
}
bool RadioManager::MenuTick(InputManager& input) {
    SettingsMenu::Result res = menu_.Tick(input);
    if (menu_.TakeChanged()) ApplySettings(menu_.Edited());   // changes apply immediately
    switch (menu_.TakeCommand()) {
    case SettingsMenu::OpenMusicFolder: {
        std::wstring f = MediaFolder();
        CreateDirectoryW(f.c_str(), nullptr);
        HINSTANCE r = ShellExecuteW(nullptr, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Log::Write("Open music folder %s (%s)", WideToUtf8(f).c_str(), (INT_PTR)r > 32 ? "ok" : "failed");
        menu_.SetStatus(Utf8ToWide(cfg_.menuLanguage == "en" ? "Folder opened in Explorer - new music appears when you switch to USB"
                                                             : "Ordner im Explorer ge\u00f6ffnet \u2013 neue Musik erscheint beim Umschalten auf USB"));
        break; }
    case SettingsMenu::OpenLogoFolder:
        ShellExecuteW(nullptr, L"open", overlay_.LogoDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case SettingsMenu::ClearLogos: {
        overlay_.ClearLogoCache();
        uint64_t b = 0; int n = 0; overlay_.LogoCacheUsage(b, n);
        menu_.SetCacheInfo(b, n);
        menu_.SetStatus(Utf8ToWide(cfg_.menuLanguage == "en" ? "Logo storage cleared - logos are downloaded again when needed"
                                                             : "Logo-Speicher geleert \u2013 Logos werden bei Bedarf neu geladen"));
        break; }
    default: break;
    }
    if (menu_.TakeSaveRequest()) { cfg_.SaveMenuSettings(cfgPath_); menu_.MarkSaved(); }
    if (res == SettingsMenu::Result::Open) {
        if (auto img = menu_.TakeImage()) overlay_.ShowMenu(img);
        return false;
    }
    overlay_.HideMenu();
    return true;
}

void RadioManager::ApplySettings(const Config& e) {
    CopyMenuSettings(cfg_, e);
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    if (!cfg_.whitenoise) audio_.SetStaticGain(0, 0.3f);
    overlay_.Reconfigure(cfg_.overlay, cfg_.screen);
    overlay_.SetScreenPinned(powered_ && cfg_.screenPage == "radio");
    if (source_ == Source::Media && !cfg_.media.enabled) EnterRadio();
    if (source_ == Source::Media) overlay_.Update(CurrentInfo());   // media type of the folder may have changed
}

void RadioManager::Shutdown() { menu_.Close(); overlay_.HideMenu(); display_.Stop(); overlay_.Shutdown(); audio_.Shutdown(); cfg_.SaveVolume(cfgPath_); }


std::string RadioManager::ResolveUrl(const std::string& url) const {
    if (url.empty() || url.find("://") != std::string::npos || (url.size() > 1 && url[1] == ':')) return url;
    return WideToUtf8(dir_) + url;   // relative local file -> realistic-radio folder
}
OverlayInfo RadioManager::CurrentInfo(const std::wstring& sub) {
    if (source_ == Source::Media) return MediaInfo(sub);
    OverlayInfo i;
    i.source = Utf8ToWide(cfg_.overlay.textRadio);
    auto* st = db_.StationsOf(currentCountry_);
    if (!st || currentIndex_ < 0 || currentIndex_ >= (int)st->size()) {
        i.title = L"Realistic Radio"; i.subtitle = sub.empty() ? Utf8ToWide(cfg_.overlay.textConnecting) : sub; return i;
    }
    const Station& s = (*st)[currentIndex_];
    i.headline = playing_ ? Utf8ToWide(cfg_.overlay.textNowPlaying) : Utf8ToWide(cfg_.overlay.textStopped);
    i.title = Utf8ToWide(s.name);
    auto it = last_.available.find(currentCountry_);
    i.bars = playing_ ? (it == last_.available.end() ? 0 : ReceptionBars(it->second.whitenoise)) : -1;
    i.favourite = fav_.Get(currentCountry_) == s.name;
    for (size_t k = 0; k < list_.size(); ++k)   // "Sender 3 / 12" (position in the Next/Previous order)
        if (list_[k].country == currentCountry_ && (int)list_[k].index == currentIndex_) {
            i.detail = Utf8ToWide(cfg_.overlay.textStations) + L" " + std::to_wstring(k + 1) + L" / " + std::to_wstring(list_.size());
            break;
        }
    i.lcd = i.title;
    if (!playing_) i.lcd = Utf8ToWide(cfg_.overlay.textStopped);
    if (!sub.empty()) { i.subtitle = sub; i.lcd = sub; }
    else {
        i.subtitle = Utf8ToWide(db_.CountryDisplayName(currentCountry_));
        if (playing_ && audio_.StreamFailures() > 0) i.subtitle += L"  \u2022  " + Utf8ToWide(cfg_.overlay.textUnavailable);
        if (playing_ && audio_.StreamFailures() > 0) i.lcd = i.title + L" - " + Utf8ToWide(cfg_.overlay.textUnavailable);
        else if (playing_ && audio_.IsBuffering()) i.subtitle += L"  \u2022  " + Utf8ToWide(cfg_.overlay.textConnecting);
    }
    return i;
}
void RadioManager::ShowCurrent(const std::wstring& sub, bool volumeMessage) {
    auto* st = db_.StationsOf(currentCountry_);
    std::string logo = (st && currentIndex_ >= 0 && currentIndex_ < (int)st->size()) ? (*st)[currentIndex_].logo : "";
    if (source_ == Source::Media) logo = mediaLogo_;
    OverlayInfo info = CurrentInfo(sub);
    if (volumeMessage) {   // the in-cab screen shows a volume bar instead of the station
        info.volume = (int)std::lround(cfg_.volume * 100); info.muted = cfg_.muted;
        info.volumeLabel = Utf8ToWide(cfg_.muted ? cfg_.overlay.textMuted : cfg_.overlay.textVolume);
    }
    overlay_.Show(info, logo);
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
    if (on) overlay_.Update(CurrentInfo());                        // page content = current station/track, not the power-off message
    overlay_.SetScreenPinned(on && cfg_.screenPage == "radio");   // radio off: navigation only
    if (source_ == Source::Media) { audio_.SetPaused(!on || mediaPaused_); return; }   // USB keeps its position
    if (!on) {
        swapAt_ = 0; switchUntil_ = 0;
        audio_.StopStream(); audio_.SetStaticGain(0, 0.3f); audio_.SetSwitchGain(0, 0.1f); audio_.SetStreamGain(0, 0.3f);
    } else if (playing_ && currentIndex_ >= 0) {
        auto it = last_.available.find(currentCountry_);
        if (it != last_.available.end()) SetRadioStation(currentCountry_, currentIndex_, it->second.whitenoise);
        else { currentCountry_.clear(); currentIndex_ = -1; }   // out of range meanwhile -> next refresh picks a station
    }
}

// Speed-dependent volume (like GALA in real car radios): from 30 km/h the volume rises smoothly and reaches the
// configured boost at 90 km/h. autoVolume 0..1 = up to +0..+6 dB (gain x1..x2). Changes are smoothed over ~3 s.
void RadioManager::UpdateSpeedVolume(float speed, ULONGLONG now) {
    float dt = lastSpeedAt_ ? std::min(0.5f, (now - lastSpeedAt_) / 1000.f) : 0.f;
    lastSpeedAt_ = now;
    float kmh = std::abs(speed) * 3.6f;
    float target = cfg_.autoVolume > 0 ? std::clamp((kmh - 30.f) / 60.f, 0.f, 1.f) : 0.f;
    float rate = dt / 3.f;
    speedBoost_ += std::clamp(target - speedBoost_, -rate, rate);
    audio_.SetBoost(1.f + (float)cfg_.autoVolume * speedBoost_);
}

void RadioManager::Tick(double x, double z, bool valid, bool electric, bool engine, float speed) {
    ULONGLONG now = GetTickCount64();
    UpdateSpeedVolume(speed, now);
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
    if (source_ == Source::Media) TickMedia(now);
    else if (playing_ && currentIndex_ >= 0 && !swapAt_) {   // keep the overlay in sync with connection state and reception
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
    if (source_ == Source::Media) {
        OverlayInfo i = MediaInfo(L"");
        d.playing = !mediaPaused_; d.headline = i.headline; d.title = i.title; d.country = i.subtitle; d.bars = -1;
        if (mediaPaused_) d.status = Utf8ToWide(cfg_.media.textPaused);
        int n = (int)order_.size();
        if (n > 1 && orderPos_ >= 0) {
            d.prevName = lib_.Tracks()[order_[(orderPos_ - 1 + n) % n]].title;
            d.nextName = lib_.Tracks()[order_[(orderPos_ + 1) % n]].title;
        }
        d.footer = i.source + L"  \u2022  " + i.detail;
        display_.Update(d, overlay_.CurrentLogo());
        return;
    }
    OverlayInfo i = CurrentInfo();
    d.headline = i.headline; d.title = i.title; d.bars = i.bars; d.favourite = i.favourite;
    auto* st = db_.StationsOf(currentCountry_);
    if (st && currentIndex_ >= 0) {
        d.country = Utf8ToWide(db_.CountryDisplayName(currentCountry_));
        if (!playing_) d.status = Utf8ToWide(cfg_.overlay.textStopped);
        else if (audio_.StreamFailures() > 0) d.status = Utf8ToWide(cfg_.overlay.textUnavailable);
        else if (audio_.IsBuffering() || swapAt_) d.status = Utf8ToWide(cfg_.overlay.textConnecting);
    } else {
        d.title = L"Realistic Radio"; d.headline.clear(); d.bars = -1;
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

    if (source_ == Source::Radio && (playing_ || !initialReceived_)) {   // USB mode: keep tracking reception only
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
    if (a == Action::Menu) {
        overlay_.PrepareMenu();
        menu_.Open(cfg_);
        if (cfg_.media.enabled) {   // music folders for the media type rows (read again: files may have been added)
            if (source_ != Source::Media) lib_.Scan(MediaFolder(), cfg_.media.folderPerDisc);
            std::vector<SettingsMenu::Folder> f;
            for (auto& d : lib_.Discs()) f.push_back({d.name, d.count});
            menu_.SetFolders(f);
        }
        uint64_t b = 0; int n = 0; overlay_.LogoCacheUsage(b, n);
        menu_.SetCacheInfo(b, n);
        overlay_.ShowMenu(menu_.TakeImage());
        return;
    }
    if (a == Action::ScreenPage) {   // in-cab screen: navigation <-> radio page
        bool page = cfg_.screenPage != "radio";
        cfg_.screenPage = page ? "radio" : "navi";
        Log::Write(page ? "Screen: radio page" : "Screen: navigation");
        overlay_.Update(CurrentInfo());
        overlay_.SetScreenPinned(page && powered_);
        return;
    }
    if (!powered_ && a != Action::VolumeUp && a != Action::VolumeDown && a != Action::Mute) {
        overlay_.Show(OverlayInfo{L"", L"Realistic Radio", Utf8ToWide(cfg_.overlay.textPowerOff), -1, false, Utf8ToWide(cfg_.overlay.textPowerOff)}, "");
        return;
    }
    if (a == Action::Source) { ToggleSource(); return; }
    if (a == Action::NextDisc || a == Action::PreviousDisc) { StepDisc(a == Action::NextDisc ? 1 : -1); return; }
    if (source_ == Source::Media) {
        switch (a) {
        case Action::Next: StepTrack(1); return;
        case Action::Previous: StepTrack(-1); return;
        case Action::Stop:   // pause / play
            if (order_.empty()) return;
            if (!audio_.Track().active) { PlayTrack(orderPos_, true); return; }
            mediaPaused_ = !mediaPaused_; audio_.SetPaused(mediaPaused_ || !powered_);
            Log::Write(mediaPaused_ ? "USB paused" : "USB playing");
            ShowCurrent();
            return;
        case Action::MakeFavourite: case Action::GoToFavourite: return;   // favourites are radio stations
        default: break;   // volume, mute, overlay: same as radio
        }
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
        ShowCurrent(Utf8ToWide(cfg_.overlay.textVolume) + L" " + std::to_wstring((int)std::lround(cfg_.volume * 100)) + L" %", true);
        break; }
    case Action::Mute: cfg_.muted = !cfg_.muted; audio_.SetMuted(cfg_.muted); Log::Write(cfg_.muted ? "Muted" : "Unmuted");
        ShowCurrent(cfg_.muted ? Utf8ToWide(cfg_.overlay.textMuted) : Utf8ToWide(cfg_.overlay.textVolume) + L" " + std::to_wstring((int)std::lround(cfg_.volume * 100)) + L" %", true);
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
