// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "RadioManager.h"
#include "EmbeddedData.h"
#include "D3D11Overlay.h"
#include "GameAudio.h"
#include "Logger.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
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
    BuildFamilies();
    db_.AssignFrequencies();
    tunnels_.Init(dir, game);
    game_ = game; startedAt_ = GetTickCount64();
    tuned_ = cfg_.tunedFreq;
    fav_.Load(dir + L"favourites-" + Utf8ToWide(game) + L".json");
    memPath_ = dir + L"trucks-" + Utf8ToWide(game) + L".json";
    LoadMemory();
    StreamSession::SetMetadataEnabled(cfg_.songTitle);   // ask the stations for the song title
    if (!audio_.Init(dir, cfg_.reconnectMaxSeconds)) return false;
    GameAudio::Configure(cfg_.audioOutput == "cabin", cfg_.muteGameRadio, cfg_.gameBus,
                         [](float* o, unsigned n, void* u) { ((AudioEngine*)u)->PullMix(o, n); }, &audio_);
    if (cfg_.audioOutput == "cabin") Log::Write("Sound output: the game's cab (beta)");
    g_outsideStatus = [this](bool en) { return OutsideStatus(en); };
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    playing_ = cfg_.autoPlay;
    overlay_.Init(cfg_.overlay, cfg_.screen, dir);
    InitPhone();
    {   // folder for the truck logos of the start screen (the player puts the pictures there)
        std::wstring b = overlay_.TruckLogoDir();
        bool fresh = CreateDirectoryW(b.c_str(), nullptr) != 0;
        // one folder per manufacturer of the game, so the pictures only have to be put in (mod brands: see SetTruck)
        std::vector<const char*> brands = game == "ats" ? std::vector<const char*>{"freightliner", "international", "kenworth", "mack", "peterbilt", "volvo", "westernstar"}
                                                         : std::vector<const char*>{"daf", "iveco", "man", "mercedes", "renault", "scania", "volvo"};
        for (const char* id : brands) CreateDirectoryW((b + Utf8ToWide(id)).c_str(), nullptr);
        ExtractTruckLogos(b, std::vector<std::string>(brands.begin(), brands.end()));   // logos delivered with the plugin
        if (fresh) {
            std::ofstream r{std::filesystem::path(b + L"LIES MICH - READ ME.txt")};
            r << "Realistic Radio - Truck-Logos fuer den Startbildschirm / truck logos for the start screen\r\n\r\n"
                 "DE: Beim Einschalten der Zuendung zeigt das Navi kurz das Logo des Herstellers. Die Bilder legst du hier ab\r\n"
                 "(PNG, JPG oder BMP). Logos fuer DAF, Iveco, MAN, Mercedes-Benz (8 Farben), Scania und Volvo liefert das\r\n"
                 "Plugin schon mit; geloeschte oder ersetzte Logos werden nicht wiederhergestellt. Ein dunkler Hintergrund wird automatisch durchsichtig und das Bild auf das Logo zugeschnitten,\r\n"
                 "Handy-Hintergrundbilder gehen also auch.\r\n"
                 "  - ein Ordner pro Hersteller-ID: man\\, scania\\, volvo\\, daf\\, mercedes\\, iveco\\, renault\\\r\n"
                 "    (darin beliebig viele Bilder, z. B. mercedes\\blau.png, mercedes\\gold.png)\r\n"
                 "  - oder Dateien direkt hier: scania.png, scania-neon.png\r\n"
                 "  - Mod-LKW: Ordner mit der Hersteller-ID oder der LKW-ID des Mods (steht im realistic-radio.log in der Zeile\r\n"
                 "    \"Start logo\"); ein Ordner mit der LKW-ID gilt nur fuer dieses Modell.\r\n"
                 "Gibt es mehrere Bilder, waehlst du im Einstellungsmenue (Strg + 0) auf der Seite LKW unter \"Start-Logo\", welches\r\n"
                 "gezeigt wird; die Wahl wird im LKW-Preset gespeichert. Ohne Bild wird der Herstellername gezeigt.\r\n\r\n"
                 "EN: When the ignition is switched on, the navigation screen briefly shows the manufacturer's logo. Put the\r\n"
                 "pictures here (PNG, JPG or BMP). Logos for DAF, Iveco, MAN, Mercedes-Benz (8 colours), Scania and Volvo come\r\n"
                 "with the plugin; logos you delete or replace are not restored. A dark background becomes transparent and the picture is cut to the logo\r\n"
                 "automatically, so phone wallpapers work too.\r\n"
                 "  - one folder per brand id: man\\, scania\\, volvo\\, daf\\, mercedes\\, iveco\\, renault\\ (any number of pictures)\r\n"
                 "  - or files right here: scania.png, scania-neon.png\r\n"
                 "  - mod trucks: a folder named after the mod's brand id or truck id (see the \"Start logo\" line in\r\n"
                 "    realistic-radio.log); a truck id folder is used for that model only.\r\n"
                 "With several pictures choose one in the settings menu (Ctrl + 0), page Trucks, \"Start logo\"; the choice is\r\n"
                 "stored in the truck preset. Without a picture the brand name is shown.\r\n";
        }
    }
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
        // no music in the folder: like a car radio without a medium ("no media detected")
        const bool en = cfg_.menuLanguage == "en";
        OverlayInfo i; i.source = en ? L"Media" : L"Medien"; i.media = true; i.mediaType = "none"; i.paused = true;
        i.title = Utf8ToWide(cfg_.media.textEmpty); i.lcd = i.title;
        i.subtitle = (en ? L"Add music to realistic-radio\\" : L"Musik in realistic-radio\\") + Utf8ToWide(cfg_.media.folder) + (en ? L"" : L" ablegen");
        std::wstring menuKey = Utf8ToWide(KeyBindingName(cfg_.keys[(int)Action::Menu]));
        if (!en && menuKey.rfind(L"Ctrl+", 0) == 0) menuKey = L"Strg+" + menuKey.substr(5);
        if (!menuKey.empty()) i.detail = menuKey + (en ? L" \u2192 USB / CD \u2192 Music folder" : L" \u2192 USB / CD \u2192 Musikordner \u00f6ffnen");
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
    // USB/CD never has static or the tuning sound (safety net for a radio fade that was still running)
    if (audio_.NoiseActive()) { audio_.SetStaticGain(0, 0.2f); audio_.SetSwitchGain(0, 0.1f); }
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
    if (menuOpenAt_ && GetTickCount64() - menuOpenAt_ > 2500) {   // no frame drawn since the menu opened: it cannot be seen
        bool drawn = D3D11Overlay::D3D11Frames() > menuFrames_;
        menuOpenAt_ = 0;
        if (!drawn) {
            Log::Write("Settings menu: no picture reached the overlay since it opened (%lld D3D11 / %lld other frames) - closed again so the "
                       "game can be used. Another graphics tool may be presenting the picture differently; please send this log.",
                       D3D11Overlay::D3D11Frames(), D3D11Overlay::OtherFrames());
            menu_.Close(); overlay_.HideMenu();
            return true;
        }
    }
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
    case SettingsMenu::AutoStoreButtons: {
        int n = AutoStore(true);
        const bool en = cfg_.menuLanguage == "en";
        menu_.SetStatus(n > 0 ? (en ? L"Station buttons 1-" : L"Stationstasten 1\u2013") + std::to_wstring(n) +
                                    (en ? L" stored with the stations received here" : L" mit den Sendern in Reichweite belegt")
                              : std::wstring(en ? L"No stations in range here" : L"Hier sind keine Sender in Reichweite"));
        break; }
    case SettingsMenu::PhoneTest: PhoneTest(); break;
    case SettingsMenu::PhoneTestCall: {
        const bool en = cfg_.menuLanguage == "en";
        dispatchText_ = en ? L"Hello, dispatch here. This is a test call - the phone works. Drive safely!"
                           : L"Hallo, hier ist die Disposition. Das ist ein Testanruf \u2013 das Handy funktioniert. Gute Fahrt!";
        dispatchAt_ = GetTickCount64() + 3000;
        menu_.SetStatus("Anruf kommt in 3 Sekunden \u2013 Men\u00fc schlie\u00dfen, dann Handy-Taste", "Call comes in 3 seconds - close the menu, then press the phone key");
        break;
    }
    case SettingsMenu::OpenStylesFolder: {
        std::wstring f = dir_ + L"styles\\";
        CreateDirectoryW(f.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case SettingsMenu::OpenTruckLogoFolder: {   // the folder of this truck's brand, else truck-logos\
        CreateDirectoryW(overlay_.TruckLogoDir().c_str(), nullptr);
        std::wstring f = overlay_.TruckLogoDir();
        if (!brandId_.empty()) { std::wstring s = f + Utf8ToWide(brandId_); if (GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) f = s; }
        ShellExecuteW(nullptr, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        menu_.SetLogos(overlay_.StartLogoFiles(truckId_, brandId_));   // pictures added meanwhile
        break; }
    case SettingsMenu::PreviewStartLogo:
        menu_.SetLogos(overlay_.StartLogoFiles(truckId_, brandId_));
        if (brand_.empty() && truckId_.empty()) { menu_.SetStatus("Kein LKW erkannt \u2013 erst einsteigen", "No truck detected - get into a truck first"); break; }
        overlay_.ShowStartLogo(truckId_, brandId_, Utf8ToWide(brand_), Utf8ToWide(model_));
        menu_.SetStatus("Start-Logo wird auf dem Navi gezeigt", "The start logo is shown on the navigation screen");
        break;
    case SettingsMenu::DetectScreen:
        overlay_.StartScreenDetect();
        menu_.SetStatus("Erkenne den sichtbaren Bereich \u2026 (Navigation im LKW muss zu sehen sein)",
                        "Detecting the visible area \u2026 (the navigation must be visible in the truck)");
        break;
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
    {   // auto-detection result (beta)
        float r[4];
        int d = overlay_.PollScreenDetect(r);
        if (d == 1) menu_.SetScreenArea(r);
        else if (d == -1) menu_.SetStatus("Die Anzeige auf dem Navi ist aus oder nicht bereit", "The navigation screen display is off or not ready");
        else if (d == -2) menu_.SetStatus("Navi-Bildschirm nicht gefunden \u2013 im LKW sitzen, Navigation sichtbar, nochmal versuchen",
                                          "Navigation screen not found - sit in the truck with the navigation visible and try again");
        else if (d < 0) menu_.SetStatus("Automatische Erkennung hier nicht m\u00f6glich \u2013 bitte von Hand einstellen",
                                        "Automatic detection is not possible here - please adjust by hand");
    }
    if (res == SettingsMenu::Result::Open) {
        float mx, my;
        if (menu_.TakeMoved(mx, my)) overlay_.SetMenuPosition(mx, my);
        if (auto img = menu_.TakeImage()) overlay_.ShowMenu(img);
        return false;
    }
    overlay_.HideMenu();
    return true;
}

void RadioManager::SetTruck(const std::string& id, const std::string& name, const std::string& brandId, const std::string& brand,
                            const std::string& model) {
    brandId_ = brandId; brand_ = brand; model_ = model;
    if (!brandId.empty() && brandId.find_first_of("\\/:*?\"<>|") == std::string::npos)   // mod trucks get their folder too
        if (CreateDirectoryW((overlay_.TruckLogoDir() + Utf8ToWide(brandId)).c_str(), nullptr)) Log::Write("Truck logos: folder '%s' created", brandId.c_str());
    if (UseMemory() && id != truckId_) StoreMemory();   // the truck we are leaving keeps its radio
    bool changed = id != truckId_;
    truckId_ = id; truckName_ = name;
    Log::Write("Truck: %s (%s)", name.c_str(), id.c_str());
    if (menu_.IsOpen()) { menu_.SetTruck(id, name); menu_.SetLogos(overlay_.StartLogoFiles(id, brandId)); }
    if (cfg_.presetAuto && AutoSelectPreset(cfg_, id)) {
        overlay_.Reconfigure(cfg_.overlay, cfg_.screen);
        if (cfg_.activePreset.empty()) Log::Write("Truck presets: no preset for this truck - current screen settings kept");
        else Log::Write("Truck preset '%s' loaded", cfg_.activePreset.c_str());
    }
    if (UseMemory() && changed) { RestoreMemory(); SaveMemory(); autoStoreChecked_ = false; }
}

// ---------------------------------------------------------------------------------------------
// Radio memory per truck
namespace {
using mjson = nlohmann::json;
std::map<std::string, std::string> StrMap(const mjson& j, const char* key) {
    std::map<std::string, std::string> m;
    if (j.contains(key) && j[key].is_object()) for (auto& [k, v] : j[key].items()) if (v.is_string()) m[k] = v.get<std::string>();
    return m;
}
}
void RadioManager::LoadMemory() {
    try {
        std::ifstream f{std::filesystem::path(memPath_)};
        if (!f) return;
        mjson j = mjson::parse(f, nullptr, true, true);
        if (!j.contains("trucks") || !j["trucks"].is_object()) return;
        for (auto& [id, t] : j["trucks"].items()) {
            if (!t.is_object()) continue;
            TruckMemory m;
            m.name = t.value("name", std::string()); m.source = t.value("source", m.source); m.mediaTrack = t.value("mediaTrack", std::string());
            m.volume = t.value("volume", -1.0); m.muted = t.value("muted", false);
            m.mediaResume = StrMap(t, "mediaResume"); m.stations = StrMap(t, "stations"); m.favourites = StrMap(t, "favourites");
            m.buttons = StrMap(t, "buttons"); m.band = t.value("band", std::string()); m.freq = t.value("freq", 0);
            mem_[id] = m;
        }
        Log::Write("Radio memory: %d truck(s)", (int)mem_.size());
    } catch (...) { Log::Write("Radio memory: could not read trucks file"); }
}
void RadioManager::SaveMemory() {
    if (mem_.empty()) return;
    mjson j; j["trucks"] = mjson::object();
    for (auto& [id, m] : mem_) {
        mjson t;
        t["name"] = m.name; t["source"] = m.source; t["mediaTrack"] = m.mediaTrack; t["volume"] = m.volume; t["muted"] = m.muted;
        t["mediaResume"] = m.mediaResume; t["stations"] = m.stations; t["favourites"] = m.favourites;
        t["buttons"] = m.buttons; t["band"] = m.band; t["freq"] = m.freq;
        j["trucks"][id] = t;
    }
    std::string s = j.dump(2);
    if (s == memSaved_) return;
    try { std::ofstream o{std::filesystem::path(memPath_)}; o << s; memSaved_ = s; } catch (...) {}
}
void RadioManager::StoreMemory() {
    if (truckId_.empty()) return;
    TruckMemory& m = mem_[truckId_];
    if (!truckName_.empty()) m.name = truckName_;
    bool fresh = m.volume < 0;
    m.volume = cfg_.volume; m.muted = cfg_.muted; m.source = cfg_.source; m.mediaTrack = cfg_.mediaTrack; m.mediaResume = cfg_.mediaResume;
    m.band = cfg_.band;
    if (tuned_ > 0) m.freq = tuned_;
    if (fresh && m.buttons.empty()) m.buttons = cfg_.stationButtons;   // a new truck starts with the general station buttons
    if (auto* st = db_.StationsOf(currentCountry_); st && currentIndex_ >= 0 && currentIndex_ < (int)st->size())
        m.stations[currentCountry_] = (*st)[currentIndex_].name;
}
void RadioManager::RestoreMemory() {
    auto it = mem_.find(truckId_);
    if (it == mem_.end()) { StoreMemory(); Log::Write("Radio memory: new truck, current radio settings taken over"); return; }
    const TruckMemory m = it->second;
    if (m.volume >= 0) cfg_.volume = std::clamp(m.volume, 0.0, 1.0);
    cfg_.muted = m.muted;
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    cfg_.mediaResume = m.mediaResume; cfg_.mediaTrack = m.mediaTrack;
    if (m.band == "fm" || m.band == "dab") { if (m.band != cfg_.band) lastRefresh_ = 0; cfg_.band = m.band; }
    if (m.source == "usb" && cfg_.media.enabled && !lib_.Discs().empty()) {
        EnterMedia(-1, false);   // the USB / CD and track this truck played last
    } else if (FreqMode() && m.freq > 0) {   // frequency radio: the frequency this truck was tuned to
        if (source_ == Source::Media) EnterRadio();
        SetTuned(m.freq);
    } else {
        if (source_ == Source::Media) EnterRadio();
        auto st = m.stations.find(currentCountry_);
        auto av = last_.available.find(currentCountry_);
        if (st != m.stations.end() && av != last_.available.end()) {
            int idx = StationIndexByName(currentCountry_, st->second);
            if (idx >= 0 && idx != currentIndex_) SetRadioStation(currentCountry_, idx, av->second.whitenoise);
        }
    }
    Log::Write("Radio memory of %s restored (%s)", m.name.c_str(), cfg_.source.c_str());
}
std::string RadioManager::FavGet(const std::string& country) const {
    if (UseMemory()) {
        auto it = mem_.find(truckId_);
        if (it != mem_.end()) { auto f = it->second.favourites.find(country); if (f != it->second.favourites.end()) return f->second; }
    }
    return fav_.Get(country);
}
bool RadioManager::FavHas(const std::string& country) const { return !FavGet(country).empty(); }

void RadioManager::ApplySettings(const Config& e) {
    bool memWasOn = cfg_.truckMemory;
    std::string oldBand = cfg_.band, oldTuning = cfg_.tuning;
    bool oldSong = cfg_.songTitle;
    std::string oldOutput = cfg_.audioOutput; bool oldMuteGame = cfg_.muteGameRadio;
    CopyMenuSettings(cfg_, e);
    if (cfg_.audioOutput != oldOutput || cfg_.muteGameRadio != oldMuteGame) {
        GameAudio::Configure(cfg_.audioOutput == "cabin", cfg_.muteGameRadio, cfg_.gameBus,
                             [](float* o, unsigned n, void* u) { ((AudioEngine*)u)->PullMix(o, n); }, &audio_);
        Log::Write("Sound output: %s, game radio %s", cfg_.audioOutput == "cabin" ? "the game's cab (beta)" : "Windows device",
                   cfg_.muteGameRadio ? "muted while playing" : "not touched");
    }
    if (cfg_.songTitle != oldSong) {
        StreamSession::SetMetadataEnabled(cfg_.songTitle);
        Log::Write("Live song title: %s", cfg_.songTitle ? "on" : "off");
        if (!cfg_.songTitle) song_.clear();
        // the station sends titles only on a new connection: connect again (without the tuning sound)
        if (cfg_.songTitle && source_ == Source::Radio && playing_ && !pendingUrl_.empty() && !swapAt_) audio_.PlayUrl(ResolveUrl(pendingUrl_));
        overlay_.Update(CurrentInfo());
    }
    if (cfg_.tuning != oldTuning) { lastRefresh_ = 0; flipUntil_ = 0; Log::Write("Tuning: %s", cfg_.tuning.c_str()); }
    if (cfg_.band != oldBand) {   // band changed in the menu: tune again like the band key (stays in USB mode)
        Log::Write("Band: %s", Dab() ? "DAB+" : "FM");
        lastRefresh_ = 0;
        auto it = last_.available.find(currentCountry_);
        if (source_ == Source::Radio && playing_ && currentIndex_ >= 0 && it != last_.available.end())
            SetRadioStation(currentCountry_, currentIndex_, it->second.whitenoise, true);
    }
    if (!memWasOn && UseMemory()) { StoreMemory(); SaveMemory(); Log::Write("Radio memory per truck: on"); }
    audio_.SetMaster((float)cfg_.volume); audio_.SetMuted(cfg_.muted);
    if (!cfg_.whitenoise) audio_.SetStaticGain(0, 0.3f);
    overlay_.Reconfigure(cfg_.overlay, cfg_.screen);
    overlay_.SetScreenPinned(powered_ && cfg_.screenPage == "radio");
    if (source_ == Source::Media && !cfg_.media.enabled) EnterRadio();
    if (source_ == Source::Media) overlay_.Update(CurrentInfo());   // media type of the folder may have changed
}

void RadioManager::Shutdown() { g_outsideStatus = nullptr; phone_.Shutdown(); GameAudio::SetRadioAudible(false); GameAudio::Configure(false, false, cfg_.gameBus, nullptr, nullptr); audio_.SetExternal(false); if (UseMemory()) { StoreMemory(); SaveMemory(); } menu_.Close(); overlay_.HideMenu(); display_.Stop(); overlay_.Shutdown(); audio_.Shutdown(); cfg_.SaveVolume(cfgPath_); }


std::string RadioManager::ResolveUrl(const std::string& url) const {
    if (url.empty() || url.find("://") != std::string::npos || (url.size() > 1 && url[1] == ':')) return url;
    return WideToUtf8(dir_) + url;   // relative local file -> realistic-radio folder
}
// station buttons 1..9 as tiles (station-list design): name, logo, active
void RadioManager::FillTiles(OverlayInfo& i) const {
    if (!cfg_.memoryButtons) return;
    for (int n = 1; n <= 9; ++n) {
        OverlayInfo::Tile t; t.button = n; t.active = n == i.button;
        std::string v = ButtonGet(n);
        if (v.rfind("f:", 0) == 0) { int f = atoi(v.c_str() + 2); if (f > 0) t.name = FreqText(f); }
        else if (size_t bar = v.find('|'); bar != std::string::npos) {
            std::string region = v.substr(0, bar), name = v.substr(bar + 1);
            t.name = Utf8ToWide(name);
            int k = StationIndexByName(region, name);
            if (auto* st = db_.StationsOf(region); st && k >= 0) t.logo = (*st)[k].logo;
        }
        i.tiles.push_back(t);
    }
}
OverlayInfo RadioManager::CurrentInfo(const std::wstring& sub) {
    if (source_ == Source::Media) return MediaInfo(sub);
    OverlayInfo i;
    i.source = Utf8ToWide(cfg_.overlay.textRadio) + (Dab() ? L" DAB+" : L" FM");
    auto* st = db_.StationsOf(currentCountry_);
    if (FreqMode() && tuned_ > 0 && (!st || currentIndex_ < 0 || currentIndex_ >= (int)st->size())) {   // dial on an empty frequency
        const bool en = cfg_.menuLanguage == "en";
        i.headline = playing_ ? Utf8ToWide(cfg_.overlay.textNowPlaying) : Utf8ToWide(cfg_.overlay.textStopped);
        i.title = FreqText(tuned_);
        i.subtitle = sub.empty() ? std::wstring(en ? L"No station on this frequency" : L"Kein Sender auf dieser Frequenz") : sub;
        i.bars = playing_ ? 0 : -1;
        if (int b = CurrentButton()) { i.button = b; i.buttonText = std::wstring(en ? L"Button " : L"Taste ") + std::to_wstring(b); i.detail = i.buttonText; }
        FillTiles(i);
        i.lcd = FreqText(tuned_);
        return i;
    }
    if (!st || currentIndex_ < 0 || currentIndex_ >= (int)st->size()) {
        i.title = L"Realistic Radio"; i.subtitle = sub.empty() ? Utf8ToWide(cfg_.overlay.textConnecting) : sub; return i;
    }
    const Station& s = (*st)[currentIndex_];
    i.headline = playing_ ? Utf8ToWide(cfg_.overlay.textNowPlaying) : Utf8ToWide(cfg_.overlay.textStopped);
    i.title = Utf8ToWide(s.name);
    auto it = last_.available.find(currentCountry_);
    i.bars = playing_ ? (it == last_.available.end() || inTunnel_ ? 0 : ReceptionBars(it->second.whitenoise)) : -1;
    i.favourite = FavGet(currentCountry_) == s.name;
    for (size_t k = 0; k < list_.size(); ++k)   // "Sender 3 / 12" (position in the Next/Previous order)
        if (list_[k].country == currentCountry_ && (int)list_[k].index == currentIndex_) {
            i.detail = Utf8ToWide(cfg_.overlay.textStations) + L" " + std::to_wstring(k + 1) + L" / " + std::to_wstring(list_.size());
            i.position = std::to_wstring(k + 1) + L" / " + std::to_wstring(list_.size());
            break;
        }
    if (FreqMode() && s.freq > 0) i.detail = FreqText(s.freq);   // the dial instead of "station 3 / 12"
    if (int b = CurrentButton()) {
        i.button = b; i.buttonText = std::wstring(cfg_.menuLanguage == "en" ? L"Button " : L"Taste ") + std::to_wstring(b);
        i.detail += (i.detail.empty() ? L"" : L"  \u2022  ") + i.buttonText;
    }
    FillTiles(i);
    i.lcd = i.title;
    if (FreqMode() && s.freq > 0) i.lcd = i.title + L" " + FreqText(s.freq);
    if (playing_ && cfg_.songTitle && !song_.empty()) { i.song = song_; i.lcd = i.title + L"  \u266A " + song_; }
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
    if (valid) { lastX_ = x; lastZ_ = z; }
    UpdateTunnel(x, z, valid, now);
    if (electric && !lastElectric_) startLogoAt_ = now;   // ignition on (also with the radio off)
    if (!electric) startLogoAt_ = 0;
    lastElectric_ = electric;
    if (startLogoAt_ && (!brand_.empty() || now - startLogoAt_ > 3000)) {   // wait a moment for the truck data after loading a save
        startLogoAt_ = 0;
        overlay_.ShowStartLogo(truckId_, brandId_, Utf8ToWide(brand_), Utf8ToWide(model_));
    }
    if (UseMemory() && now >= memStoreAt_) { memStoreAt_ = now + 30000; StoreMemory(); SaveMemory(); }   // volume, track, ... (crash-safe)
    SetPower(cfg_.powerSource == "always" || !valid ? true : cfg_.powerSource == "engine" ? engine : electric);
    // station-switch timeline (port of the jQuery animate() chain in setRadioStation)
    if (swapAt_ && now >= swapAt_) {
        swapAt_ = 0;
        if (pendingUrl_.empty()) audio_.StopStream(); else audio_.PlayUrl(ResolveUrl(pendingUrl_));
        switchUntil_ = now + switchHoldMs_;
    }
    if (switchUntil_ && now >= switchUntil_) {
        switchUntil_ = 0;
        audio_.SetSwitchGain(0, 0.2f);
        audio_.SetStreamGain(1, 0.05f);
        noiseAllowed_ = true;
    }
    PhoneTick(GetTickCount64());
    phone_.Update(GetTickCount64());
    audio_.SetExternal(GameAudio::Playing());   // cab output: the game plays the mix, the Windows device stays silent
    GameAudio::SetRadioAudible(powered_ && !cfg_.muted && (source_ == Source::Media ? !mediaPaused_ : playing_));
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
        bool changed = buf != lastBuffering_ || bars != lastBars_;
        if (now >= songAt_) {   // live song title: switches when that part of the audio is played
            songAt_ = now + 200;
            std::string t;
            std::wstring song = cfg_.songTitle && audio_.SongTitle(t) ? Utf8ToWide(t) : std::wstring();
            if (song != song_) {
                song_ = song;
                if (!song_.empty()) Log::Write("Song: %s", t.c_str());
                changed = true;
            }
        }
        if (changed) { lastBuffering_ = buf; lastBars_ = bars; overlay_.Update(CurrentInfo()); }
    } else if (!song_.empty()) { song_.clear(); }
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
    ReceptionResult r = CalculateReception(db_, x, z, cfg_.radius * (Dab() ? cfg_.dabRange : 1.0));   // DAB+ reaches further
    if (!r.valid) return;
    if (r.nearestCountry != last_.nearestCountry || r.nearestCity != last_.nearestCity)
        Log::Write("Nearest city: %s (%s), %.0f m; best reception: %s", r.nearestCity.c_str(), r.nearestCountry.c_str(), r.nearestDistance, r.bestCountry.c_str());
    bool countriesChanged = r.available.size() != last_.available.size() ||
        !std::equal(r.available.begin(), r.available.end(), last_.available.begin(), [](auto& a, auto& b) { return a.first == b.first; });
    last_ = r;
    // g_stations; the same programme from several regions (SWR1 BW / SWR1 RP) is listed once: the current one,
    // otherwise the best received
    list_.clear();
    std::map<std::string, size_t> fam;
    for (auto& [k, v] : r.available) if (auto* st = db_.StationsOf(k)) for (size_t i = 0; i < st->size(); ++i) {
        Entry e{k, i, v.whitenoise};
        std::string key = FamilyKey(k, (*st)[i].name);
        auto it = fam.find(key);
        if (it == fam.end()) { fam[key] = list_.size(); list_.push_back(e); continue; }
        if (list_[it->second].country == k) { list_.push_back(e); continue; }   // never merge two stations of one region
        Entry& kept = list_[it->second];
        bool isCur = k == currentCountry_ && (int)i == currentIndex_;
        bool keptCur = kept.country == currentCountry_ && (int)kept.index == currentIndex_;
        if (isCur || (!keptCur && e.whitenoise < kept.whitenoise)) kept = e;
    }

    if (!autoStoreChecked_ && !list_.empty()) {   // first start (or a new truck): station buttons come pre-stored
        autoStoreChecked_ = true;
        bool empty = true;
        for (int n = 1; n <= 9; ++n) if (!ButtonGet(n).empty()) empty = false;
        if (empty && AutoStore(false) > 0) Log::Write("Station buttons pre-stored with the stations received here");
    }
    if (source_ == Source::Radio && (playing_ || !initialReceived_)) {   // USB mode: keep tracking reception only
        initialReceived_ = true;
        if (!playing_) return;
        if (FreqMode()) { RefreshFreq(r); return; }
        auto cur = r.available.find(currentCountry_);
        const auto& best = r.available[r.bestCountry];
        bool better = cfg_.switchMode == "better" && cur != r.available.end() &&
                      best.whitenoise + cfg_.threshold < cur->second.whitenoise && lastNearestCountry_ != r.bestCountry;
        // out of range only counts after a few seconds, so a region at the edge of its range does not switch back and forth
        ULONGLONG now = GetTickCount64();
        bool lost = false;
        if (cur != r.available.end()) lostSince_ = 0;
        else if (currentIndex_ < 0 || currentCountry_.empty()) lost = true;   // nothing chosen yet
        else { if (!lostSince_) lostSince_ = now; lost = now - lostSince_ >= 5000; }
        if (lost || better) {
            lostSince_ = 0;
            // the station is out of range ("lost") or another region is clearly better ("better"):
            // keep the programme if another region broadcasts it, otherwise a station of the best region
            lastNearestCountry_ = r.bestCountry;
            std::string family = CurrentFamily(), region; int idx = -1;
            if (better) {   // the same programme in the better region?
                if (auto* bst = db_.StationsOf(r.bestCountry); bst && !family.empty())
                    for (size_t i = 0; i < bst->size(); ++i)
                        if (FamilyKey(r.bestCountry, (*bst)[i].name) == family) { idx = (int)i; region = r.bestCountry; break; }
            } else if (!family.empty()) FindVariant(family, region, idx);
            if (idx >= 0) SetRadioStation(region, idx, r.available[region].whitenoise, true);
            else PickStation(r.bestCountry, best.whitenoise);
        } else if (cur == r.available.end()) {
            SetWhitenoise(1.0);   // leaving the range: the signal fades before the radio changes
        } else if (!countriesChanged) {
            SetWhitenoise(cur->second.whitenoise);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Frequency radio: every station has an FM frequency (StationDatabase::AssignFrequencies). The radio stays on the
// frequency: whatever is received best there plays - drive on and another station can take the frequency over, at
// borders two stations can share it and the weaker one breaks through now and then. Without a station: static.
std::vector<RadioManager::OnFreq> RadioManager::StationsOn(int f) const {
    std::vector<OnFreq> v;
    for (auto& [k, rec] : last_.available)
        if (auto* st = db_.StationsOf(k))
            for (size_t i = 0; i < st->size(); ++i) if ((*st)[i].freq == f && !(*st)[i].url.empty()) v.push_back({k, (int)i, rec.whitenoise});
    std::stable_sort(v.begin(), v.end(), [](const OnFreq& a, const OnFreq& b) { return a.w < b.w; });
    return v;
}
std::wstring RadioManager::FreqText(int f) const {
    wchar_t b[32]; swprintf(b, 32, L"%d%lc%d MHz", f / 10, cfg_.menuLanguage == "en" ? L'.' : L',', f % 10);
    return b;
}
void RadioManager::GoSilent() {
    bool was = silent_ && currentIndex_ < 0;
    currentCountry_.clear(); currentIndex_ = -1;
    pendingUrl_.clear(); swapAt_ = 0; switchUntil_ = 0;
    audio_.StopStream(); audio_.SetSwitchGain(0, 0.2f);
    audio_.SetStaticGain(cfg_.whitenoise ? 0.55f : 0.f, 0.4f);
    noiseAllowed_ = true; playing_ = true; silent_ = true;
    if (!was) { Log::Write("Frequency %.1f MHz: no station", tuned_ / 10.0); ShowCurrent(); }
}
void RadioManager::SetTuned(int f) {
    tuned_ = f; cfg_.tunedFreq = f; flipUntil_ = 0;
    auto on = StationsOn(f);
    if (on.empty()) { silent_ = false; GoSilent(); return; }
    SetRadioStation(on[0].region, (size_t)on[0].index, on[0].w);
}
void RadioManager::SeekFreq(int dir) {
    int f = tuned_ > 0 ? tuned_ : 875;
    for (int n = 1; n <= 206; ++n) {
        int g = f + dir * n;
        while (g > 1080) g -= 206;
        while (g < 875) g += 206;
        if (!StationsOn(g).empty()) { SetTuned(g); return; }
    }
    ShowCurrent(cfg_.menuLanguage == "en" ? L"No station found" : L"Kein Sender gefunden");
}
void RadioManager::RefreshFreq(const ReceptionResult& r) {
    if (tuned_ <= 0) {   // first time: the station the radio would pick anyway, then stay on its frequency
        if (currentIndex_ >= 0) { auto* st = db_.StationsOf(currentCountry_); if (st && (*st)[currentIndex_].freq > 0) tuned_ = (*st)[currentIndex_].freq; }
        if (tuned_ <= 0) { PickStation(r.bestCountry, r.available.count(r.bestCountry) ? r.available.at(r.bestCountry).whitenoise : 0.5); return; }
    }
    ULONGLONG now = GetTickCount64();
    auto on = StationsOn(tuned_);
    auto isCur = [&](const OnFreq& o) { return o.region == currentCountry_ && o.index == currentIndex_; };
    const OnFreq* cur = nullptr;
    for (auto& o : on) if (isCur(o)) cur = &o;
    if (cfg_.rdsAf && currentIndex_ >= 0 && (!cur || !isCur(on.front()))) {   // RDS AF: follow the programme
        std::string family = CurrentFamily(), region; int idx = -1;
        auto it = r.available.find(currentCountry_);
        if (!cur && it == r.available.end() && !family.empty() && FindVariant(family, region, idx)) {
            Log::Write("RDS AF: %s continues on another frequency", family.c_str());
            SetRadioStation(region, (size_t)idx, r.available.at(region).whitenoise, true);
            return;
        }
        if (cur) { SetWhitenoise(cur->w); return; }   // AF keeps the programme even if another station is stronger here
    }
    if (on.empty()) { if (currentIndex_ >= 0 || !silent_) GoSilent(); return; }
    if (flipUntil_ && now < flipUntil_ && cur) { SetWhitenoise(std::min(1.0, cur->w + 0.35)); return; }   // the weaker one, for a moment
    flipUntil_ = 0;
    const OnFreq& best = on.front();
    bool shared = on.size() > 1 && on[1].w - best.w < 0.12;   // two stations of similar strength on one frequency
    if (shared && cur && rand() % 100 < 4) {
        const OnFreq& other = isCur(on[0]) ? on[1] : on[0];
        flipUntil_ = now + 7000 + (ULONGLONG)(rand() % 8000);
        Log::Write("Frequency %.1f MHz: two stations - %s breaks through", tuned_ / 10.0, other.region.c_str());
        SetRadioStation(other.region, (size_t)other.index, std::min(1.0, other.w + 0.35));
        return;
    }
    if (!isCur(best)) {   // another station takes the frequency over (or the first one after static)
        if (currentIndex_ >= 0) Log::Write("Frequency %.1f MHz: now %s", tuned_ / 10.0, best.region.c_str());
        SetRadioStation(best.region, (size_t)best.index, shared ? std::min(1.0, best.w + 0.35) : best.w);
        return;
    }
    SetWhitenoise(shared ? std::min(1.0, best.w + 0.35) : best.w);
}

// ---------------------------------------------------------------------------------------------
// Tunnels: inside, FM fades into static, DAB+ stutters for a moment and then goes silent; USB / CD keeps playing.
void RadioManager::UpdateTunnel(double x, double z, bool valid, ULONGLONG now) {
    if (now >= tunCheckAt_) {   // position check 5x per second, map mods every 20 s
        tunCheckAt_ = now + 200;
        tunnels_.Tick();
        bool raw = cfg_.tunnelSignal && valid && tunnels_.Inside(x, z);
        // a short moment inside / outside does not count (portals, GPS jitter)
        if (raw) { tunExitAt_ = 0; if (!tunEnterAt_) tunEnterAt_ = now; if (!inTunnel_ && now - tunEnterAt_ >= 400) { inTunnel_ = true; Log::Write("Tunnel: in"); if (source_ == Source::Radio) overlay_.Update(CurrentInfo()); } }
        else { tunEnterAt_ = 0; if (!tunExitAt_) tunExitAt_ = now; if (inTunnel_ && now - tunExitAt_ >= 600) { inTunnel_ = false; Log::Write("Tunnel: out"); if (source_ == Source::Radio) overlay_.Update(CurrentInfo()); } }
    }
    if (now < tunStepAt_) return;
    tunStepAt_ = now + 100;
    tunnelDepth_ = inTunnel_ ? std::min(1.f, tunnelDepth_ + 0.05f) : std::max(0.f, tunnelDepth_ - 0.08f);   // ~2 s in, ~1.2 s out
    float mul = 1.f, add = 0.f, secs = 0.15f;
    if (source_ == Source::Radio && playing_ && tunnelDepth_ > 0.f) {
        if (Dab()) {   // digital: a moment of drop-outs, then silence (and a stutter when the signal comes back)
            mul = tunnelDepth_ >= 0.6f ? 0.f : (rand() % 100 < (int)(tunnelDepth_ * 140) ? 0.f : 1.f);
            secs = 0.03f;
        } else {
            mul = 1.f - tunnelDepth_;
            add = cfg_.whitenoise ? 0.8f * tunnelDepth_ : 0.f;
        }
    }
    if (mul != tunMul_ || add != tunAdd_) { tunMul_ = mul; tunAdd_ = add; audio_.SetTunnel(mul, add, secs); }
}

void RadioManager::PickStation(const std::string& region, double whitenoise) {
    if (!db_.StationsOf(region)) { Log::Write("No stations for country '%s'", region.c_str()); return; }
    int idx = -1;
    if (UseMemory()) {   // the station this truck played last in this region
        auto m = mem_.find(truckId_);
        if (m != mem_.end()) { auto s = m->second.stations.find(region); if (s != m->second.stations.end()) idx = StationIndexByName(region, s->second); }
    }
    if (idx < 0) idx = StationIndexByName(region, FavGet(region));
    SetRadioStation(region, idx < 0 ? 0 : idx, whitenoise);
}

// Same programme in several regions (only within one country: part of the region key before '-' / '_'):
//  - the same name ("Radio BOB!" in several regions)
//  - the same brand with a number: "SWR1 BW" / "SWR1 Rheinland-Pfalz", "NDR 1 Niedersachsen" / "NDR 1 Welle Nord"
//  - the same brand followed by a place: "ENERGY Stuttgart" / "ENERGY München", "MDR Sachsen" / "MDR Thüringen"
// Stations of one region are never merged: if a key is not unique within a region ("RTL 102.5" / "RTL 102.5 Disco"),
// those stations keep their full name as key.
namespace {
std::vector<std::string> NameTokens(const std::string& s) {
    std::vector<std::string> tok; std::string t;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0xC3 && i + 1 < s.size() && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0x9E && (unsigned char)s[i + 1] != 0x97) {
            t += (char)ch; t += (char)((unsigned char)s[i + 1] + 0x20); ++i; continue;   // Ä Ö Ü É ... -> lower case
        }
        if (isalnum(ch) || ch >= 0x80) t += (char)tolower(ch);
        else if (!t.empty()) { tok.push_back(t); t.clear(); }
    }
    if (!t.empty()) tok.push_back(t);
    return tok;
}
std::string Join(const std::vector<std::string>& t, size_t from, size_t to) {
    std::string r;
    for (size_t i = from; i < to; ++i) r += (i > from ? " " : "") + t[i];
    return r;
}
}
void RadioManager::BuildFamilies() {
    places_.clear(); famAmbiguous_.clear(); famCache_.clear();
    for (const City& c : db_.cities) if (!c.realName.empty()) { auto t = NameTokens(c.realName); places_.insert(Join(t, 0, t.size())); }
    for (auto& [k, info] : db_.countries) {
        size_t a = info.nativeName.find('['), b = info.nativeName.find(']');
        if (a != std::string::npos && b != std::string::npos && b > a) { auto t = NameTokens(info.nativeName.substr(a + 1, b - a - 1)); places_.insert(Join(t, 0, t.size())); }
        size_t d = k.find('-');
        if (d != std::string::npos) { auto t = NameTokens(k.substr(d + 1)); places_.insert(Join(t, 0, t.size())); }
    }
    for (const char* p : {"nrw", "bw", "rp", "rlp", "mv", "sh", "nds", "hh", "nordrhein westfalen", "niedersachsen", "schleswig holstein",
                          "mecklenburg vorpommern", "hessen", "bayern", "saarland", "brandenburg", "sachsen", "sachsen anhalt", "th\xc3\xbcringen",
                          "baden w\xc3\xbcrttemberg", "rheinland pfalz", "berlin", "hamburg", "bremen", "ruhr", "franken", "schwaben"})
        places_.insert(p);
    for (auto& [region, list] : db_.stations) {
        std::map<std::string, int> n;
        for (auto& s : list) ++n[RawFamilyKey(region, s.name)];
        for (auto& [k, cnt] : n) if (cnt > 1) famAmbiguous_[region].insert(k);
    }
}
std::string RadioManager::RawFamilyKey(const std::string& region, const std::string& name) const {
    static const std::set<std::string> generic = {"radio", "antenne", "hitradio", "hit", "rock", "star", "kiss", "fm", "classic", "klassik",
        "schlager", "the", "das", "die", "der", "top", "oldie", "oldies", "jazz", "country", "news", "info", "sport", "dance", "pop", "city",
        "welle", "power", "love", "smooth", "gold", "heart", "capital", "magic", "bbc", "rtl", "hits", "music", "mix", "one", "radio fm"};
    std::string nation = region.substr(0, region.find_first_of("-_"));
    auto t = NameTokens(name);
    size_t n = t.size();
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    bool numbered = false;
    for (size_t i = 0; i < t.size() && i < 2; ++i)
        if (std::any_of(t[i].begin(), t[i].end(), digit)) {
            if (!(i == 0 && std::all_of(t[i].begin(), t[i].end(), digit))) n = i + 1;   // "105.5 ROCK": a frequency is no brand
            numbered = true;
            break;
        }
    if (!numbered)
        for (size_t k = 3; k >= 1; --k)   // brand + place
            if (t.size() > k && places_.count(Join(t, t.size() - k, t.size()))) {
                std::string rest = Join(t, 0, t.size() - k);
                if (!generic.count(rest) && !(t.size() - k == 1 && t[0].size() < 3)) n = t.size() - k;
                break;
            }
    return nation + "|" + Join(t, 0, n);
}
std::string RadioManager::FamilyKey(const std::string& region, const std::string& name) const {
    std::string ck = region + "\n" + name;
    auto c = famCache_.find(ck);
    if (c != famCache_.end()) return c->second;
    std::string k = RawFamilyKey(region, name);
    auto a = famAmbiguous_.find(region);
    if (a != famAmbiguous_.end() && a->second.count(k)) { auto t = NameTokens(name); k = region.substr(0, region.find_first_of("-_")) + "|" + Join(t, 0, t.size()); }
    famCache_[ck] = k;
    return k;
}
std::string RadioManager::CurrentFamily() const {
    auto* st = db_.StationsOf(currentCountry_);
    if (!st || currentIndex_ < 0 || currentIndex_ >= (int)st->size()) return "";
    return FamilyKey(currentCountry_, (*st)[currentIndex_].name);
}
bool RadioManager::FindVariant(const std::string& family, std::string& region, int& index) const {
    double bestW = 1e9; bool found = false;
    for (auto& [k, v] : last_.available) {
        auto* st = db_.StationsOf(k); if (!st) continue;
        for (size_t i = 0; i < st->size(); ++i)
            if (FamilyKey(k, (*st)[i].name) == family && v.whitenoise < bestW) { bestW = v.whitenoise; region = k; index = (int)i; found = true; break; }
    }
    return found;
}

// Station buttons (numpad 1-9): tap = select, hold = save - like the preset buttons of a car radio
std::string RadioManager::ButtonGet(int n) const {
    std::string k = std::to_string(n);
    if (UseMemory()) {
        auto m = mem_.find(truckId_);
        if (m == mem_.end()) return "";
        auto b = m->second.buttons.find(k);
        return b == m->second.buttons.end() ? "" : b->second;
    }
    auto b = cfg_.stationButtons.find(k);
    return b == cfg_.stationButtons.end() ? "" : b->second;
}
void RadioManager::ButtonSet(int n, const std::string& v) {
    if (UseMemory()) { mem_[truckId_].buttons[std::to_string(n)] = v; SaveMemory(); }
    else { cfg_.stationButtons[std::to_string(n)] = v; cfg_.SaveVolume(cfgPath_); }
}
int RadioManager::CurrentButton() const {
    if (!cfg_.memoryButtons) return 0;
    if (FreqMode() && tuned_ > 0)
        for (int n = 1; n <= 9; ++n) if (ButtonGet(n) == "f:" + std::to_string(tuned_)) return n;
    std::string family = CurrentFamily();
    if (family.empty()) return 0;
    for (int n = 1; n <= 9; ++n) {
        std::string v = ButtonGet(n); size_t bar = v.find('|');
        if (bar != std::string::npos && FamilyKey(v.substr(0, bar), v.substr(bar + 1)) == family) return n;
    }
    return 0;
}
void RadioManager::StationButton(int n, bool hold) {
    const bool en = cfg_.menuLanguage == "en";
    std::wstring num = std::to_wstring(n);
    if (hold && FreqMode() && source_ == Source::Radio && tuned_ > 0) {   // frequency radio: the button keeps the frequency
        ButtonSet(n, "f:" + std::to_string(tuned_));
        Log::Write("Station button %d: %.1f MHz", n, tuned_ / 10.0);
        ShowCurrent((en ? L"Saved on button " : L"Auf Taste ") + num + (en ? L": " : L" gespeichert: ") + FreqText(tuned_));
        return;
    }
    if (hold) {   // save the current station
        auto* st = db_.StationsOf(currentCountry_);
        if (source_ != Source::Radio || !st || currentIndex_ < 0 || currentIndex_ >= (int)st->size()) return;
        ButtonSet(n, currentCountry_ + "|" + (*st)[currentIndex_].name);
        Log::Write("Station button %d: %s [%s]", n, (*st)[currentIndex_].name.c_str(), currentCountry_.c_str());
        ShowCurrent((en ? L"Saved on button " : L"Auf Taste ") + num + (en ? L"" : L" gespeichert"));
        return;
    }
    std::string v = ButtonGet(n);
    if (v.rfind("f:", 0) == 0) {   // a frequency: whatever is broadcast there, here and now
        int f = atoi(v.c_str() + 2);
        if (f < 875 || f > 1080) return;
        if (!FreqMode()) {   // station list: the best station on that frequency here
            auto on = StationsOn(f);
            if (on.empty()) { if (source_ == Source::Radio) ShowCurrent((en ? L"Button " : L"Taste ") + num + L": " + FreqText(f) + (en ? L" - nothing received" : L" \u2013 kein Empfang")); return; }
            if (source_ == Source::Media) EnterRadio();
            SetRadioStation(on[0].region, (size_t)on[0].index, on[0].w);
            return;
        }
        if (source_ == Source::Media) EnterRadio();
        SetTuned(f);
        return;
    }
    size_t bar = v.find('|');
    if (bar == std::string::npos) {
        if (source_ == Source::Radio) ShowCurrent((en ? L"Button " : L"Taste ") + num + (en ? L" is empty - hold it to save" : L" ist leer \u2013 gedr\u00fcckt halten zum Speichern"));
        return;
    }
    std::string region = v.substr(0, bar), name = v.substr(bar + 1);
    int idx = last_.available.count(region) ? StationIndexByName(region, name) : -1;
    if (idx < 0 && !FindVariant(FamilyKey(region, name), region, idx)) idx = -1;
    if (idx < 0) {   // out of range here
        if (source_ == Source::Radio) ShowCurrent((en ? L"Button " : L"Taste ") + num + L": " + Utf8ToWide(name) + (en ? L" - out of range" : L" \u2013 nicht in Reichweite"));
        return;
    }
    if (source_ == Source::Media) EnterRadio();
    if (region == currentCountry_ && idx == currentIndex_ && playing_) { ShowCurrent(); return; }
    SetRadioStation(region, idx, last_.available[region].whitenoise);
}

int RadioManager::AutoStore(bool overwrite) {
    // like "travel store" on a car radio: the best received stations, one per programme
    std::vector<Entry> picks(list_.begin(), list_.end());
    std::stable_sort(picks.begin(), picks.end(), [](const Entry& a, const Entry& b) { return a.whitenoise < b.whitenoise; });
    std::set<std::string> seen;
    std::map<std::string, std::string>& buttons = UseMemory() ? mem_[truckId_].buttons : cfg_.stationButtons;
    int n = 0;
    for (const Entry& e : picks) {
        if (n >= 9) break;
        auto* st = db_.StationsOf(e.country);
        if (!st || e.index >= st->size() || (*st)[e.index].url.empty()) continue;
        if (!seen.insert(FamilyKey(e.country, (*st)[e.index].name)).second) continue;
        ++n;
        std::string key = std::to_string(n);
        auto b = buttons.find(key);
        std::string value = FreqMode() && (*st)[e.index].freq > 0 ? "f:" + std::to_string((*st)[e.index].freq) : e.country + "|" + (*st)[e.index].name;
        if (overwrite || b == buttons.end() || b->second.empty()) buttons[key] = value;
    }
    if (n > 0) { if (UseMemory()) SaveMemory(); else cfg_.SaveVolume(cfgPath_); }
    if (n > 0) Log::Write("Station buttons 1-%d stored (%s)", n, overwrite ? "menu" : "first start");
    return n;
}

void RadioManager::ToggleBand() {
    cfg_.band = Dab() ? "fm" : "dab";
    Log::Write("Band: %s", Dab() ? "DAB+" : "FM");
    lastRefresh_ = 0;   // reception again with the range of the new band
    if (source_ == Source::Media) { EnterRadio(); return; }
    if (playing_) {
        auto it = last_.available.find(currentCountry_);
        if (it != last_.available.end()) SetRadioStation(currentCountry_, currentIndex_, it->second.whitenoise, true);
        else ShowCurrent();
    } else ShowCurrent();
}

void RadioManager::SetRadioStation(const std::string& country, size_t index, double whitenoise, bool variant) {
    auto* st = db_.StationsOf(country); if (!st || index >= st->size()) return;
    currentCountry_ = country; currentIndex_ = (int)index;
    const Station& s = (*st)[index];
    if (UseMemory()) mem_[truckId_].stations[country] = s.name;
    if (s.freq > 0) { tuned_ = s.freq; cfg_.tunedFreq = s.freq; }
    silent_ = false;
    auto it = last_.available.find(country);
    Log::Write("Station: %s [%s] reception %d/5", s.name.c_str(), country.c_str(), it == last_.available.end() ? 0 : ReceptionBars(it->second.whitenoise));
    playing_ = true;
    noiseAllowed_ = false;
    audio_.SetStaticGain(0, 0.3f);
    // FM: tuning noise; DAB+ and the same programme from another region: short, quiet change
    bool quiet = Dab() || variant;
    audio_.SetSwitchGain(cfg_.transitionWhitenoise && !s.url.empty() && !quiet ? 1.f : 0.f, 2.5f);
    audio_.SetStreamGain(0, quiet ? 0.4f : 2.0f);
    pendingUrl_ = s.url;
    song_.clear();   // the title of the old station
    swapAt_ = GetTickCount64() + (quiet ? 500 : 2000);
    switchHoldMs_ = quiet ? 300 : 2200;
    SetWhitenoise(whitenoise);
    lastBuffering_ = true; lastFailed_ = false;
    ShowCurrent();
}

// Port of setWhitenoise(): static volume grows with the square of the relative distance; weak signal makes the
// programme "flutter" (random drop-outs) exactly like the original.
void RadioManager::SetWhitenoise(double w) {
    if (Dab()) {   // digital: no static; a weak signal drops out ("bubbling"), without reception it is silent
        audio_.SetStaticGain(0, 0.3f);
        if (!noiseAllowed_) return;
        double q = std::clamp(w * w - 0.15, 0.0, 1.0);
        if (q < 0.5) audio_.SetStreamGain(1.f, 0.2f);
        else if (q < 0.8) audio_.SetStreamGain((double)rand() / RAND_MAX < (q - 0.5) / 0.3 * 0.7 ? 0.f : 1.f, 0.05f);
        else audio_.SetStreamGain(0.f, 0.1f);
        return;
    }
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

void RadioManager::OnAction(Action a, bool hold) {
    if (a == Action::MenuAlt) a = Action::Menu;   // second key for the settings menu
    if (a == Action::Menu) {
        overlay_.PrepareMenu();
        menu_.Open(cfg_);
        menuOpenAt_ = GetTickCount64(); menuFrames_ = D3D11Overlay::D3D11Frames();
        menu_.SetTruck(truckId_, truckName_);
        menu_.SetLogos(overlay_.StartLogoFiles(truckId_, brandId_));
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
    if (a == Action::Phone) { if (phone_.IsOpen()) { phone_.Close(); overlay_.HideMenu(); } else OpenPhone(); return; }
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
    if (a == Action::Band) { ToggleBand(); return; }
    if (a == Action::TunnelMark) { ShowCurrent(tunnels_.Mark(lastX_, lastZ_, cfg_.menuLanguage == "en")); return; }
    if (IsMemoryAction(a)) { if (cfg_.memoryButtons) StationButton((int)a - (int)Action::Memory1 + 1, hold); return; }
    if (a == Action::TuneUp || a == Action::TuneDown) {   // FM dial, 0.1 MHz
        if (!FreqMode()) {
            if (!Dab()) ShowCurrent(cfg_.menuLanguage == "en" ? L"Tuning by frequency: settings menu \u2192 Radio" : L"Frequenzwahl: Einstellungsmen\u00fc \u2192 Radio \u2192 Senderwahl");
            return;
        }
        if (source_ == Source::Media) EnterRadio();
        int f = tuned_ > 0 ? tuned_ : 875;
        f += a == Action::TuneUp ? 1 : -1;
        if (f > 1080) f = 875; if (f < 875) f = 1080;
        SetTuned(f);
        return;
    }
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
    case Action::Next: if (FreqMode()) SeekFreq(1); else NextStation(1); break;
    case Action::Previous: if (FreqMode()) SeekFreq(-1); else NextStation(-1); break;
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
            if (UseMemory()) { mem_[truckId_].favourites[currentCountry_] = (*st)[currentIndex_].name; SaveMemory(); }
            else fav_.Set(currentCountry_, (*st)[currentIndex_].name);
            Log::Write("Favourite for %s: %s", currentCountry_.c_str(), (*st)[currentIndex_].name.c_str());
            ShowCurrent(Utf8ToWide(cfg_.overlay.textFavourite));
        }
        break;
    case Action::GoToFavourite: {   // port of the "goToFavourite" command: cycles through favourites of countries in range
        std::string country = currentCountry_;
        int idx = StationIndexByName(country, FavGet(country));
        if (idx < 0 || idx == currentIndex_) {
            std::vector<std::string> favCountries;
            for (auto& [k, v] : last_.available) if (FavHas(k) && db_.StationsOf(k)) favCountries.push_back(k);
            if (favCountries.empty()) break;
            auto it = std::find(favCountries.begin(), favCountries.end(), currentCountry_);
            country = (it == favCountries.end() || it + 1 == favCountries.end()) ? favCountries.front() : *(it + 1);
            idx = std::max(0, StationIndexByName(country, FavGet(country)));
        }
        SetRadioStation(country, idx, last_.available.count(country) ? last_.available[country].whitenoise : 0);
        break; }
    case Action::ShowOverlay:
        ShowCurrent();
        break;
    default: break;
    }
}

// Cab screen at night: with the lights on a real infotainment display dims to the dashboard backlight setting
void RadioManager::SetLights(float backlight, bool lightsOn) {
    float target = 0.f;
    if (cfg_.screen.dimWithLights && lightsOn) target = 1.f - (0.35f + 0.45f * std::clamp(backlight, 0.f, 1.f));   // 35 % .. 80 % brightness
    float step = 0.05f;   // ~1 s from day to night brightness (called ~20x per second)
    float d = dim_ + std::clamp(target - dim_, -step, step);
    if (std::fabs(d - dim_) > 1e-4f || (target == 0.f && dim_ != 0.f)) {
        if (std::fabs(target - d) < 1e-3f) d = target;
        if ((d > 0.f) != (dim_ > 0.f)) Log::Write("Screen: %s", d > 0.f ? "dimmed (lights on)" : "full brightness");
        dim_ = d;
        D3D11Overlay::SetScreenDim(dim_);
    }
}

// ------------------------------------------------------------------------------------------------
// Quieter outside the truck (e.g. walking around with a walk mod): the game tells its sound system where the camera is
// (FMOD listener). Compared with the driver's head from the telemetry this gives the distance to the cab: in the cab the
// radio is unchanged, outside it is muffled by the closed cab (less with an open window) and fades with the distance.
// The listener may use another coordinate system than the telemetry; the matching one is found while the camera is at
// the driver's seat, until then nothing changes.
void RadioManager::SetPose(bool valid, const double truck[3], const double head[3], float speed, double heading) {
    if (valid) { for (int k = 0; k < 3; ++k) truckPos_[k] = truck[k]; heading_ = heading; }
    const bool want = cfg_.outsideFade;
    GameAudio::WatchListener(want);
    ULONGLONG now = GetTickCount64();
    float cabinOut = -1, wl = 0, wr = 0, L[3];
    const bool state = want && GameAudio::CabState(cabinOut, wl, wr);
    const bool haveL = want && GameAudio::Listener(L);
    outsideMap_ = state ? 1 : -1;
    if (!state) { audio_.SetOutside(1.f, 20000.f, 0.f); outsideNow_ = false; return; }
    // The game's sound coordinates are world coordinates minus an origin that stays put while the truck moves (it is
    // moved now and then). While the camera is in the cab, origin = driver's head (telemetry) - listener; outside, the
    // camera's distance from the cab is |listener + origin - head| - right also after the truck was moved.
    float o = std::clamp(cabinOut, 0.f, 1.f);
    if (std::abs(speed) > 1.5f) o = 0.f;   // driving: you are in the truck (also with an outside camera)
    if (haveL && valid && cabinOut < 0.05f) {
        double n[3] = {head[0] - L[0], head[1] - L[1], head[2] - L[2]};
        double jump = cabRefValid_ ? std::hypot(n[0] - cabRef_[0], n[2] - cabRef_[2]) : 1e9;
        for (int k = 0; k < 3; ++k) cabRef_[k] = jump > 20 ? n[k] : cabRef_[k] + (n[k] - cabRef_[k]) * 0.2;   // > 20 m: the game moved its origin
        if (!cabRefValid_) Log::Write("Outside: camera in the cab - distance outside is measured from here");
        cabRefValid_ = true;
    }
    float dist = 0.f;
    if (cabinOut >= 0.05f) {
        if (haveL && valid && cabRefValid_) {
            double dx = L[0] + cabRef_[0] - head[0], dy = L[1] + cabRef_[1] - head[1], dz = L[2] + cabRef_[2] - head[2];
            dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
        } else dist = 2.f;   // outside, distance not known yet (never been in the cab since loading): right behind the cab wall
    }
    const float window = std::clamp(std::max(wl, wr), 0.f, 1.f);   // 0 = closed .. 1 = fully open
    auto smooth = [](float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); };
    const float maxD = (float)std::max(5.0, cfg_.outsideDistance) * (1.f + window);
    const float dd = std::max(0.f, dist - 1.5f);                 // distance from the cab
    const float att = 1.f / (1.f + dd / 4.f) * (1.f - smooth(0.6f * maxD, maxD, dist));
    const float shell = 0.3f + 0.4f * window;                     // closed cab: muffled and quieter, open window lets more through
    const float cutOut = std::exp(std::log(1000.f) + (std::log(4500.f) - std::log(1000.f)) * window) / (1.f + dd / 60.f);
    const float gain = 1.f + (shell * att - 1.f) * o;
    const float cut = o > 0.001f ? std::exp(std::log(20000.f) + (std::log(cutOut) - std::log(20000.f)) * o) : 20000.f;
    audio_.SetOutside(gain, cut, 0.7f * o);
    outsideDist_ = dist; outsideNow_ = o > 0.5f; outsideWindow_ = window > 0.2f;
    if (outsideNow_ != outsideWasOut_ && outsideLogs_ < 100) {
        ++outsideLogs_;
        if (outsideNow_) Log::Write("Outside: left the cab (window %.0f %% open)", window * 100);
        else Log::Write("Outside: back in the cab");
        outsideLogAt_ = now + 3000;
    }
    outsideWasOut_ = outsideNow_;
    if (outsideNow_ && now >= outsideLogAt_ && outsideLogs_ < 100) {
        ++outsideLogs_; outsideLogAt_ = now + 5000;
        Log::Write("Outside: %.1f m from the cab, volume %.0f %%, window %.0f %% open", dist, gain * 100, window * 100);
    }
}
std::wstring RadioManager::OutsideStatus(bool en) const {
    if (!cfg_.outsideFade) return en ? L"Walk away from the truck: quieter and muffled, an open window lets more through" : L"Vom LKW weggehen: leiser und dumpfer, offenes Fenster l\u00e4sst mehr durch";
    if (outsideMap_ < 0) return en ? L"Waiting for the game's sound system \u2026" : L"Wartet auf den Sound des Spiels \u2026";
    wchar_t b[128];
    if (!outsideNow_) return en ? L"Active \u2022 you are in the truck" : L"Aktiv \u2022 du bist im LKW";
    swprintf(b, 128, en ? L"Active \u2022 outside, %.0f m from the cab%s" : L"Aktiv \u2022 drau\u00dfen, %.0f m vom LKW%s", outsideDist_,
             outsideWindow_ ? (en ? L", window open" : L", Fenster offen") : L"");
    return b;
}

// ------------------------------------------------------------------------------------------------
// Virtual phone (1.6.0): "connected" after the radio starts, message from the customer when the delivery is due soon.
// Shown like a smartphone notification at the top of the navigation screen (and in the pop-up overlay), with a tone.
namespace {
std::wstring Fill(std::wstring t, const JobInfo& j, int remaining, bool en) {
    wchar_t tm[64];
    if (remaining >= 60) swprintf(tm, 64, L"%d h %d min", remaining / 60, remaining % 60); else swprintf(tm, 64, L"%d min", std::max(0, remaining));
    auto rep = [&](const std::wstring& key, const std::wstring& val) {
        for (size_t p = t.find(key); p != std::wstring::npos; p = t.find(key, p + val.size())) t.replace(p, key.size(), val);
    };
    rep(L"{city}", Utf8ToWide(j.city.empty() ? (en ? "the destination" : "dem Ziel") : j.city));
    rep(L"{company}", Utf8ToWide(j.company));
    rep(L"{cargo}", Utf8ToWide(j.cargo.empty() ? (en ? "the cargo" : "die Ladung") : j.cargo));
    rep(L"{time}", tm);
    return t;
}
}
void RadioManager::PhoneNotice(const std::string& icon, const std::wstring& app, const std::wstring& title, const std::wstring& text) {
    const bool en = cfg_.menuLanguage == "en";
    OverlayInfo i;
    i.notice = true; i.noticeIcon = icon; i.noticeApp = app; i.noticeTitle = title; i.noticeText = text;
    i.noticeTime = en ? L"now" : L"jetzt";
    i.headline = app; i.title = title; i.subtitle = text; i.lcd = title + L": " + text;   // pop-up overlay
    overlay_.Show(i, "", 7.0);
    if (cfg_.screen.phone.sound) audio_.PlayChime(icon == "phone" ? 0 : 1);
    Log::Write("Phone: %s - %s: %s", WideToUtf8(app).c_str(), WideToUtf8(title).c_str(), WideToUtf8(text).c_str());
}
void RadioManager::SetJob(const JobInfo& j) {
    bool newJob = j.deliveryTime != job_.deliveryTime || j.city != job_.city || j.cargo != job_.cargo;
    bool keepCityForDelivered = !j.active && job_.active;
    if (keepCityForDelivered) { job_.active = false; }   // keep the last destination for the "delivered" call
    else job_ = j;
    if (newJob) jobMessageSent_ = false;
    const PhoneConfig& ph = cfg_.screen.phone;
    // a job that comes with loading a save (shortly after the game time appeared) is not a new job
    const bool loadedWithSave = !gameTimeAt_ || GetTickCount64() - gameTimeAt_ < 20000;
    if (newJob && j.active && !loadedWithSave && ph.enabled && ph.calls) {   // a new job was taken
        const bool en = cfg_.menuLanguage == "en";
        unsigned d = j.deliveryTime;
        wchar_t when[48]; swprintf(when, 48, L"%02u:%02u", (d / 60) % 24, d % 60);
        dispatchText_ = en ? L"Hello, dispatch here. Your new job: " + Utf8ToWide(j.cargo) + L" from " + Utf8ToWide(j.sourceCity) + L" to " + Utf8ToWide(j.city) +
                                 L", " + Utf8ToWide(j.company) + L". Please deliver by " + when + L". Drive safely!"
                           : L"Hallo, hier ist die Disposition. Dein neuer Auftrag: " + Utf8ToWide(j.cargo) + L" von " + Utf8ToWide(j.sourceCity) + L" nach " + Utf8ToWide(j.city) +
                                 L", Firma " + Utf8ToWide(j.company) + L". Lieferung bis " + when + L" Uhr. Gute Fahrt!";
        dispatchAt_ = GetTickCount64() + 8000;
    }
    if (j.active) Log::Write("Job: %s to %s (%s), delivery window ends at game minute %u%s", j.cargo.c_str(), j.city.c_str(), j.company.c_str(), j.deliveryTime,
                             j.loaded ? "" : " - cargo not loaded yet");
    else Log::Write("Job: none");
}
void RadioManager::SetGameTime(unsigned minutes) {
    // the game was loaded (time appears, or jumps back / far ahead: another save): remember when, for "new job"
    if (minutes && (!gameTime_ || minutes < gameTime_ || minutes > gameTime_ + 24 * 60)) gameTimeAt_ = GetTickCount64();
    gameTime_ = minutes;
}
void RadioManager::PhoneTest() {
    const bool en = cfg_.menuLanguage == "en";
    const PhoneConfig& ph = cfg_.screen.phone;
    JobInfo j = job_;
    if (!j.active) { j.city = en ? "Berlin" : "Berlin"; j.cargo = en ? "Furniture" : "M\xc3\xb6" "bel"; j.company = "Tradeaux"; }
    int remaining = j.active && gameTime_ ? (int)j.deliveryTime - (int)gameTime_ : (int)ph.minutes;
    std::wstring text = Fill(Utf8ToWide(ph.text), j, remaining, en);
    PhoneNotice("message", en ? L"Messages" : L"Nachrichten", Utf8ToWide(ph.sender), text);
    phone_.AddMessage(Utf8ToWide(ph.sender), text);
}
void RadioManager::PhoneTick(ULONGLONG now) {
    const PhoneConfig& ph = cfg_.screen.phone;
    const bool en = cfg_.menuLanguage == "en";
    const bool on = ph.enabled;
    if (!powered_ && wasPowered_) powerOffAt_ = now;
    // "connected" after the start logo - not again when the ignition was off only for a moment (engine start)
    if (on && powered_ && !wasPowered_ && (!powerOffAt_ || now - powerOffAt_ > 15000))
        phoneConnectAt_ = now + (cfg_.screen.startLogo ? (ULONGLONG)(cfg_.screen.startLogoSeconds * 1000) : 0) + 1500;
    if (on && !phoneWasOn_ && powered_ && wasPowered_) phoneConnectAt_ = now + 300;   // switched on in the menu
    wasPowered_ = powered_; phoneWasOn_ = on;
    if (!on || !powered_) { phoneConnectAt_ = 0; return; }
    if (dispatchAt_ && now >= dispatchAt_) {   // the dispatcher calls
        dispatchAt_ = 0;
        const std::wstring disp = Utf8ToWide(ph.dispatcher.empty() ? std::string(en ? "Dispatch" : "Disposition") : ph.dispatcher);
        phone_.Call(disp, dispatchText_);
    }
    if (phoneConnectAt_ && now >= phoneConnectAt_) {
        phoneConnectAt_ = 0;
        PhoneNotice("phone", L"Bluetooth", Utf8ToWide(ph.name.empty() ? "Phone" : ph.name), en ? L"Connected" : L"Verbunden");
        return;
    }
    if (!ph.messages || jobMessageSent_ || !job_.active || !job_.loaded || !gameTime_ || phoneConnectAt_) return;
    const int remaining = (int)job_.deliveryTime - (int)gameTime_;
    if (remaining > 0 && remaining <= (int)ph.minutes) {
        jobMessageSent_ = true;
        std::wstring text = Fill(Utf8ToWide(ph.text), job_, remaining, en);
        PhoneNotice("message", en ? L"Messages" : L"Nachrichten", Utf8ToWide(ph.sender), text);
        phone_.AddMessage(Utf8ToWide(ph.sender), text);
    }
}

// ------------------------------------------------------------------------------------------------
// The phone itself (1.6.0): smartphone in the corner of the screen, calls from the dispatcher, messages, job, music
void RadioManager::InitPhone() {
    PhoneHost h;
    h.english = [this] { return cfg_.menuLanguage == "en"; };
    h.gameTime = [this] { return gameTime_; };
    h.fillJob = [this](PhoneView& v) { FillPhoneJob(v); };
    h.fillMusic = [this](PhoneView& v) { FillPhoneMusic(v); };
    h.musicFolder = [this](int i) {
        auto bt = BluetoothDiscs();
        if (i < 0 || i >= (int)bt.size()) return;
        if (source_ == Source::Media && disc_ == bt[i]) return;
        EnterMedia(bt[i], false);
    };
    h.musicControl = [this](int what) {
        if (source_ != Source::Media || order_.empty()) return;
        if (what) { StepTrack(what); return; }
        if (!audio_.Track().active) { PlayTrack(orderPos_, true); return; }
        mediaPaused_ = !mediaPaused_; audio_.SetPaused(mediaPaused_ || !powered_);
        ShowCurrent();
    };
    h.settings = [this]() -> const PhoneConfig& { return cfg_.screen.phone; };
    h.design = [this] { return cfg_.screen.design; };
    h.key = [this] { return cfg_.keys[(int)Action::Phone]; };
    h.play = [this](Speech::Clip c, bool loop, float gain) { audio_.PlayPhone(c, loop, gain); };
    h.stopSound = [this] { audio_.StopPhone(); };
    h.soundPlaying = [this] { return audio_.PhonePlaying(); };
    h.callBanner = [this](const std::wstring& caller, const std::wstring&) {
        const bool en = cfg_.menuLanguage == "en";
        std::wstring key = Utf8ToWide(KeyBindingName(cfg_.keys[(int)Action::Phone]));
        if (!en && key.rfind(L"Shift+", 0) == 0) key = L"Umschalt+" + key.substr(6);
        if (!en && key.rfind(L"Ctrl+", 0) == 0) key = L"Strg+" + key.substr(5);
        OverlayInfo i;
        i.notice = true; i.noticeIcon = "call"; i.noticeApp = en ? L"Phone" : L"Telefon"; i.noticeTitle = caller;
        i.noticeText = en ? L"Incoming call • " + key + L" to answer" : L"Eingehender Anruf • " + key + L" zum Annehmen";
        i.noticeTime = en ? L"now" : L"jetzt";
        i.headline = i.noticeApp; i.title = caller; i.subtitle = i.noticeText; i.lcd = caller;
        overlay_.Show(i, "", 10.0);
    };
    phone_.Init(dir_ + L"phone-" + Utf8ToWide(game_) + L".json", h);
}

void RadioManager::OpenPhone() {
    const bool en = cfg_.menuLanguage == "en";
    if (menu_.IsOpen()) return;
    if (!cfg_.screen.phone.enabled) {
        PhoneNotice("phone", en ? L"Phone" : L"Handy", en ? L"The phone is off" : L"Das Handy ist aus",
                    en ? L"Settings menu → Phone → Virtual mobile device" : L"Einstellungsmenü → Handy → Virtuelles Handy");
        return;
    }
    overlay_.PrepareMenu();
    phone_.Open();
    if (auto img = phone_.TakeImage()) overlay_.ShowPhone(img);
}

void RadioManager::PhoneUiTick(InputManager& input) {
    phone_.Tick(input);
    if (!phone_.IsOpen()) { overlay_.HideMenu(); return; }
    if (auto img = phone_.TakeImage()) overlay_.ShowPhone(img);
}

std::wstring RadioManager::Money(long long amount) const {
    std::wstring n = std::to_wstring(amount < 0 ? -amount : amount), out;
    for (int i = 0; i < (int)n.size(); ++i) { if (i && (n.size() - i) % 3 == 0) out += cfg_.menuLanguage == "en" ? L"," : L"."; out += n[i]; }
    if (amount < 0) out = L"-" + out;
    return game_ == "ats" ? L"$" + out : out + L" €";
}

void RadioManager::FillPhoneJob(PhoneView& v) {
    const bool en = cfg_.menuLanguage == "en";
    v.jobActive = job_.active;
    if (!job_.active) return;
    v.cargo = Utf8ToWide(job_.cargo);
    v.from = Utf8ToWide(job_.sourceCity); v.fromCompany = Utf8ToWide(job_.sourceCompany);
    v.to = Utf8ToWide(job_.city); v.toCompany = Utf8ToWide(job_.company);
    auto hm = [&](int m) { wchar_t b[32]; if (m >= 60) swprintf(b, 32, L"%d h %d min", m / 60, m % 60); else swprintf(b, 32, L"%d min", m); return std::wstring(b); };
    if (gameTime_ && job_.deliveryTime) {
        int rem = (int)job_.deliveryTime - (int)gameTime_;
        v.late = rem < 0;
        v.remaining = rem < 0 ? (en ? L"late " : L"zu spät ") + hm(-rem) : hm(rem);
    }
    if (navDist_ > 0) { wchar_t b[32]; swprintf(b, 32, L"%.0f km", navDist_ / 1000.f); v.distance = b; } else v.distance = L"–";
    if (navTime_ > 0 && gameTime_) {
        unsigned eta = gameTime_ + (unsigned)(navTime_ / 60.f);
        wchar_t b[16]; swprintf(b, 16, L"%02u:%02u", (eta / 60) % 24, eta % 60); v.eta = b;
    } else v.eta = L"–";
    v.income = job_.income ? Money(job_.income) : L"–";
    // small map: cities around the truck and the destination (world x / z, north up)
    const City* dest = nullptr; const City* src = nullptr;
    for (auto& c : db_.cities) {
        if (!job_.cityId.empty() && c.gameName == job_.cityId) dest = &c;
        if (!job_.sourceCityId.empty() && c.gameName == job_.sourceCityId) src = &c;
    }
    double tx = truckPos_[0], tz = truckPos_[2];
    if (tx == 0 && tz == 0) return;
    double minX = tx, maxX = tx, minZ = tz, maxZ = tz;
    if (dest) { minX = std::min(minX, dest->x); maxX = std::max(maxX, dest->x); minZ = std::min(minZ, dest->z); maxZ = std::max(maxZ, dest->z); }
    double span = std::max({maxX - minX, maxZ - minZ, 8000.0}) * 1.25, cx = (minX + maxX) / 2, cz = (minZ + maxZ) / 2;
    auto toMap = [&](double x, double z, float& mx, float& my) { mx = (float)((x - (cx - span / 2)) / span); my = (float)((z - (cz - span / 2)) / span); };
    v.haveMap = true;
    toMap(tx, tz, v.truckX, v.truckY);
    v.truckHeading = (float)(heading_ * 6.283185307179586);
    std::vector<std::pair<double, const City*>> close;
    for (auto& c : db_.cities) {
        if (&c == dest || &c == src) continue;
        float mx, my; toMap(c.x, c.z, mx, my);
        if (mx < 0.02f || mx > 0.98f || my < 0.02f || my > 0.98f) continue;
        close.push_back({std::hypot(c.x - cx, c.z - cz), &c});
    }
    std::sort(close.begin(), close.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < close.size() && i < 30; ++i) {
        PhoneMapPoint p; toMap(close[i].second->x, close[i].second->z, p.x, p.y);
        if (i < 5) p.name = Utf8ToWide(close[i].second->realName.empty() ? close[i].second->gameName : close[i].second->realName);
        v.map.push_back(p);
    }
    if (src) { PhoneMapPoint p; toMap(src->x, src->z, p.x, p.y); p.kind = 2; p.name = v.from; if (p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1) v.map.push_back(p); }
    if (dest) { PhoneMapPoint p; toMap(dest->x, dest->z, p.x, p.y); p.kind = 1; p.name = v.to; v.map.push_back(p); }
}

std::vector<int> RadioManager::BluetoothDiscs() const {
    std::vector<int> r;
    for (int k = 0; k < (int)lib_.Discs().size(); ++k) if (DiscType(k) == "bluetooth") r.push_back(k);
    return r;
}

void RadioManager::FillPhoneMusic(PhoneView& v) {
    if (lib_.Discs().empty() && cfg_.media.enabled) lib_.Scan(MediaFolder(), cfg_.media.folderPerDisc);
    auto bt = BluetoothDiscs();
    for (size_t i = 0; i < bt.size(); ++i) {
        const auto& n = lib_.Discs()[bt[i]].name;
        v.musicFolders.push_back(n.empty() ? std::wstring(L"Bluetooth") : n);
        if (source_ == Source::Media && disc_ == bt[i]) v.musicFolderPlaying = (int)i;
    }
    if (v.musicFolderPlaying >= 0 && track_ >= 0 && track_ < (int)lib_.Tracks().size()) {
        const MediaTrack& t = lib_.Tracks()[track_];
        v.musicActive = true; v.musicPaused = mediaPaused_;
        v.musicTitle = t.title; v.musicArtist = t.artist;
        auto ts = audio_.Track();
        v.musicPos = ts.active ? (int)ts.pos : 0; v.musicLen = ts.active && ts.len > 0 ? (int)(ts.len + 0.5) : -1;
    }
}

void RadioManager::OnGameEvent(const GameEvent& e) {
    const bool en = cfg_.menuLanguage == "en";
    const PhoneConfig& ph = cfg_.screen.phone;
    Log::Write("Game event: %s (%lld)%s%s", e.id.c_str(), e.amount, e.text.empty() ? "" : " ", e.text.c_str());
    if (!ph.enabled) return;
    const std::wstring disp = Utf8ToWide(ph.dispatcher.empty() ? std::string(en ? "Dispatch" : "Disposition") : ph.dispatcher);
    if (e.id == "job.delivered" && ph.calls) {
        std::wstring t = en ? L"Hello, dispatch here. The delivery to " + Utf8ToWide(job_.city) + L" has arrived, thank you! " + Money(e.amount) + L" have been transferred to your account."
                            : L"Hallo, hier ist die Disposition. Die Lieferung nach " + Utf8ToWide(job_.city) + L" ist angekommen, danke! " + Money(e.amount) + L" sind überwiesen.";
        dispatchText_ = t; dispatchAt_ = GetTickCount64() + 6000;
    } else if (e.id == "job.cancelled") {
        PhoneNotice("message", en ? L"Messages" : L"Nachrichten", disp,
                    en ? L"Job cancelled. Penalty: " + Money(e.amount) : L"Auftrag abgebrochen. Vertragsstrafe: " + Money(e.amount));
        phone_.AddMessage(disp, en ? L"Job cancelled. Penalty: " + Money(e.amount) : L"Auftrag abgebrochen. Vertragsstrafe: " + Money(e.amount));
    } else if (e.id == "player.fined" && ph.fines) {
        static const std::map<std::string, std::pair<const wchar_t*, const wchar_t*>> why = {
            {"crash", {L"Unfall", L"Crash"}}, {"avoid_sleeping", {L"Ruhezeit nicht eingehalten", L"No rest taken"}}, {"wrong_way", {L"Falsche Richtung", L"Wrong way"}},
            {"speeding_camera", {L"Geblitzt", L"Speed camera"}}, {"no_lights", {L"Ohne Licht", L"No lights"}}, {"red_signal", {L"Rote Ampel", L"Red light"}},
            {"speeding", {L"Zu schnell", L"Speeding"}}, {"avoid_weighing", {L"Waage umfahren", L"Avoided weighing"}}, {"illegal_trailer", {L"Unerlaubter Anhänger", L"Illegal trailer"}},
            {"avoid_inspection", {L"Kontrolle umfahren", L"Avoided inspection"}}, {"illegal_border_crossing", {L"Illegaler Grenzübertritt", L"Illegal border crossing"}},
            {"hard_shoulder_violation", {L"Standstreifen befahren", L"Hard shoulder"}}, {"damaged_vehicle_usage", {L"Beschädigtes Fahrzeug", L"Damaged vehicle"}},
            {"generic", {L"Verstoß", L"Offence"}}};
        auto it = why.find(e.text);
        std::wstring reason = it != why.end() ? (en ? it->second.second : it->second.first) : Utf8ToWide(e.text);
        std::wstring from = en ? L"Police" : L"Polizei";
        std::wstring text = (en ? L"Fine: " : L"Bußgeld: ") + Money(e.amount) + L" – " + reason;
        PhoneNotice("message", en ? L"Messages" : L"Nachrichten", from, text);
        phone_.AddMessage(from, text);
    }
}
