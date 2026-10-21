// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Config.h"
#include <cmath>
#include "Logger.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

using nlohmann::json;

static void LoadPhone(const json& p, PhoneConfig& c) {
    if (!p.is_object()) return;
    c.enabled = p.value("enabled", c.enabled); c.name = p.value("name", c.name); c.messages = p.value("messages", c.messages);
    c.sender = p.value("sender", c.sender); c.text = p.value("text", c.text);
    c.minutes = std::clamp(p.value("minutes", c.minutes), 5.0, 600.0); c.sound = p.value("sound", c.sound);
    c.calls = p.value("calls", c.calls); c.voice = p.value("voice", c.voice); c.fines = p.value("fines", c.fines); c.dispatcher = p.value("dispatcher", c.dispatcher);
}
static json PhoneJson(const PhoneConfig& c) {
    return {{"enabled", c.enabled}, {"name", c.name}, {"messages", c.messages}, {"sender", c.sender}, {"text", c.text}, {"minutes", c.minutes}, {"sound", c.sound},
            {"calls", c.calls}, {"voice", c.voice}, {"fines", c.fines}, {"dispatcher", c.dispatcher}};
}

static const char* kActionNames[] = {"next", "previous", "stop", "volumeUp", "volumeDown", "makeFavourite", "goToFavourite", "mute", "showOverlay", "source", "nextDisc", "previousDisc", "screenPage", "menu",
                                     "band", "memory1", "memory2", "memory3", "memory4", "memory5", "memory6", "memory7", "memory8", "memory9",
                                     "tuneUp", "tuneDown", "menu2", "tunnelMark", "phone"};
static_assert(sizeof(kActionNames) / sizeof(kActionNames[0]) == (size_t)Action::Count, "every action needs a name");
const char* ActionName(Action a) { return kActionNames[(int)a]; }

Config::Config() {
    for (auto& b : buttons) b = -1;
    // Default keys: arrows for station/volume (.NET Keys names)
    keys[(int)Action::Previous] = ParseKeyBinding("Left");
    keys[(int)Action::Next] = ParseKeyBinding("Right");
    keys[(int)Action::Stop] = ParseKeyBinding("End");
    keys[(int)Action::VolumeUp] = ParseKeyBinding("Up");
    keys[(int)Action::VolumeDown] = ParseKeyBinding("Down");
    keys[(int)Action::MakeFavourite] = ParseKeyBinding("Pause");
    keys[(int)Action::Source] = ParseKeyBinding("Home");
    keys[(int)Action::ScreenPage] = ParseKeyBinding("Insert");
    keys[(int)Action::Menu] = ParseKeyBinding("Ctrl+0");
    keys[(int)Action::Band] = ParseKeyBinding("Shift+Home");
    for (int i = 0; i < 9; ++i) keys[(int)Action::Memory1 + i] = ParseKeyBinding("NumPad" + std::to_string(i + 1));   // station buttons
    keys[(int)Action::TuneUp] = ParseKeyBinding("Shift+Right");   // frequency radio: 0.1 MHz steps
    keys[(int)Action::TuneDown] = ParseKeyBinding("Shift+Left");
    keys[(int)Action::MenuAlt] = ParseKeyBinding("Ctrl+F1");
    keys[(int)Action::Phone] = ParseKeyBinding("Shift+Insert");   // virtual phone   // second menu key: Ctrl+0 can be taken by the free camera (developer mode)
}

int ParseKeyName(const std::string& in) {
    std::string n = in; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.empty()) return 0;
    if (n.size() == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) return ::toupper(n[0]);
    if (n.size() == 2 && n[0] == 'd' && isdigit((unsigned char)n[1])) return n[1];       // .NET "D1"
    if (n[0] == 'f' && n.size() <= 3 && isdigit((unsigned char)n[1])) { int f = atoi(n.c_str() + 1); if (f >= 1 && f <= 24) return VK_F1 + f - 1; }
    if (n.rfind("numpad", 0) == 0 && n.size() == 7 && isdigit((unsigned char)n[6])) return VK_NUMPAD0 + (n[6] - '0');
    if (n.rfind("0x", 0) == 0) return (int)strtol(n.c_str(), nullptr, 16);
    static const std::map<std::string, int> m = {
        {"pageup", VK_PRIOR}, {"prior", VK_PRIOR}, {"pagedown", VK_NEXT}, {"next", VK_NEXT}, {"end", VK_END}, {"home", VK_HOME},
        {"insert", VK_INSERT}, {"delete", VK_DELETE}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN},
        {"pause", VK_PAUSE}, {"scroll", VK_SCROLL}, {"space", VK_SPACE}, {"tab", VK_TAB}, {"enter", VK_RETURN}, {"return", VK_RETURN},
        {"backspace", VK_BACK}, {"back", VK_BACK}, {"oemplus", VK_OEM_PLUS}, {"oemminus", VK_OEM_MINUS}, {"oemcomma", VK_OEM_COMMA},
        {"oemperiod", VK_OEM_PERIOD}, {"add", VK_ADD}, {"subtract", VK_SUBTRACT}, {"multiply", VK_MULTIPLY}, {"divide", VK_DIVIDE},
        {"decimal", VK_DECIMAL}, {"medianexttrack", VK_MEDIA_NEXT_TRACK}, {"mediaprevioustrack", VK_MEDIA_PREV_TRACK},
        {"mediaplaypause", VK_MEDIA_PLAY_PAUSE}, {"mediastop", VK_MEDIA_STOP}, {"volumeup", VK_VOLUME_UP}, {"volumedown", VK_VOLUME_DOWN},
        {"volumemute", VK_VOLUME_MUTE}};
    auto it = m.find(n);
    if (it != m.end()) return it->second;
    Log::Write("Unknown key name '%s'", in.c_str());
    return 0;
}

KeyBinding ParseKeyBinding(const std::string& s) {
    KeyBinding b;
    std::stringstream ss(s); std::string part;
    while (std::getline(ss, part, '+')) {
        part.erase(0, part.find_first_not_of(" \t")); part.erase(part.find_last_not_of(" \t") + 1);
        std::string l = part; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l == "ctrl" || l == "control") b.ctrl = true;
        else if (l == "shift") b.shift = true;
        else if (l == "alt") b.alt = true;
        else if (!l.empty()) b.vk = ParseKeyName(part);
    }
    return b;
}

static const char* kDefaultConfig = R"({
  "radius": 7500,
  "threshold": 0.15,
  "whitenoise": true,
  "transitionWhitenoise": true,
  "volume": 0.6,
  "autoVolume": 0,
  "muted": false,
  "autoPlay": true,
  "powerSource": "ignition",
  "dataFile": "",
  "onlyWhenGameFocused": true,
  "reconnectMaxSeconds": 30,
  "keys": {
    "previous": "Left",
    "next": "Right",
    "stop": "End",
    "volumeUp": "Up",
    "volumeDown": "Down",
    "makeFavourite": "Pause",
    "goToFavourite": "",
    "mute": "",
    "showOverlay": "",
    "source": "Home",
    "nextDisc": "",
    "previousDisc": "",
    "screenPage": "Insert",
    "menu": "Ctrl+0"
  },
  "controllerIndex": 0,
  "buttons": {
    "previous": -1, "next": -1, "stop": -1, "volumeUp": -1,
    "volumeDown": -1, "makeFavourite": -1, "goToFavourite": -1, "mute": -1, "showOverlay": -1, "source": -1,
    "nextDisc": -1, "previousDisc": -1, "screenPage": -1, "menu": -1
  },
  "menuLanguage": "de",
  "screenPage": "navi",
  "source": "radio",
  "media": {
    "enabled": true,
    "folder": "usb",
    "folderPerDisc": true,
    "label": "USB",
    "types": {},
    "shuffle": false,
    "repeat": "all",
    "textTrack": "Titel",
    "textPaused": "Pause",
    "textEmpty": "Kein Medientyp erkannt"
  },
  "screen": {
    "enabled": true,
    "probe": false,
    "grid": false,
    "width": 1024,
    "height": 1024,
    "format": 29,
    "flipY": true,
    "flipX": false,
    "areaX": 0.1,
    "areaY": 0.3,
    "areaW": 0.8,
    "areaH": 0.4,
    "durationSeconds": 4,
    "probeSeconds": 5,
    "design": "modern"
  },
  "display": {
    "enabled": false,
    "width": 1280,
    "height": 720,
    "offscreen": true,
    "title": "Realistic Radio Display"
  },
  "overlay": {
    "mode": "off",
    "style": "panel",
    "radio": {
      "position": "bottom-center",
      "offsetX": 0.0,
      "offsetY": 0.20,
      "scale": 1.0,
      "columns": 16,
      "color": "amber",
      "scrollSeconds": 0.3
    },
    "position": "top-center",
    "offsetX": 0.0,
    "offsetY": 0.07,
    "scale": 1.0,
    "durationSeconds": 6,
    "alwaysVisible": false,
    "showLogos": true,
    "logoCacheMB": 30,
    "urlPrefix": "https://radio.scufystudio.de/logo/",
    "textNowPlaying": "Jetzt l\u00e4uft:",
    "textVolume": "Lautst\u00e4rke",
    "textStopped": "Radio aus",
    "textMuted": "Stumm",
    "textFavourite": "Als Favorit gespeichert",
    "textConnecting": "verbinde\u2026",
    "textUnavailable": "Sender nicht erreichbar",
    "textPowerOff": "Z\u00fcndung aus",
    "textRadio": "Radio",
    "textStations": "Sender"
  }
}
)";

bool Config::Load(const std::wstring& path) {
    std::ifstream f{std::filesystem::path(path)};
    if (!f) {
        std::ofstream o{std::filesystem::path(path)}; o << kDefaultConfig;
        Log::Write("Config not found, wrote defaults to %s", WideToUtf8(path).c_str());
        return true;
    }
    try {
        json j = json::parse(f, nullptr, true, true);   // allow comments
        radius = j.value("radius", radius);
        threshold = j.value("threshold", threshold);
        whitenoise = j.value("whitenoise", whitenoise);
        transitionWhitenoise = j.value("transitionWhitenoise", transitionWhitenoise);
        volume = std::clamp(j.value("volume", volume), 0.0, 1.0);
        autoVolume = std::clamp(j.value("autoVolume", autoVolume), 0.0, 1.0);
        muted = j.value("muted", muted);
        autoPlay = j.value("autoPlay", autoPlay);
        powerSource = j.value("powerSource", powerSource);
        dataFile = j.value("dataFile", dataFile);
        onlyWhenGameFocused = j.value("onlyWhenGameFocused", onlyWhenGameFocused);
        reconnectMaxSeconds = j.value("reconnectMaxSeconds", reconnectMaxSeconds);
        controllerIndex = j.value("controllerIndex", controllerIndex);
        source = j.value("source", source);
        mediaTrack = j.value("mediaTrack", mediaTrack);
        screenPage = j.value("screenPage", screenPage);
        band = j.value("band", band); if (band != "dab") band = "fm";
        dabRange = std::clamp(j.value("dabRange", dabRange), 1.0, 3.0);
        switchMode = j.value("switchMode", switchMode); if (switchMode != "better") switchMode = "lost";
        memoryButtons = j.value("memoryButtons", memoryButtons);
        tuning = j.value("tuning", tuning); if (tuning != "frequency") tuning = "list";
        rdsAf = j.value("rdsAf", rdsAf);
        tunnelSignal = j.value("tunnelSignal", tunnelSignal);
        songTitle = j.value("songTitle", songTitle);
        audioOutput = j.value("audioOutput", audioOutput); if (audioOutput != "cabin") audioOutput = "device";
        muteGameRadio = j.value("muteGameRadio", muteGameRadio);
        gameBus = j.value("gameBus", gameBus);
        outsideFade = j.value("outsideFade", outsideFade);
        outsideDistance = std::clamp(j.value("outsideDistance", outsideDistance), 5.0, 2000.0);
        tunedFreq = j.value("tunedFreq", tunedFreq);
        memoryHoldSeconds = std::clamp(j.value("memoryHoldSeconds", memoryHoldSeconds), 0.5, 3.0);
        if (j.contains("stationButtons") && j["stationButtons"].is_object())
            for (auto& [k, v] : j["stationButtons"].items()) if (v.is_string()) stationButtons[k] = v.get<std::string>();
        menuLanguage = j.value("menuLanguage", menuLanguage);
        activePreset = j.value("activePreset", activePreset);
        presetAuto = j.value("presetAuto", presetAuto);
        truckMemory = j.value("truckMemory", truckMemory);
        if (j.contains("presets") && j["presets"].is_array())
            for (auto& p : j["presets"]) {
                if (!p.is_object()) continue;
                TruckPreset t; t.name = p.value("name", std::string());
                if (t.name.empty()) continue;
                if (p.contains("trucks") && p["trucks"].is_array()) for (auto& id : p["trucks"]) if (id.is_string()) t.trucks.push_back(id.get<std::string>());
                if (p.contains("screen") && p["screen"].is_object()) {
                    auto& s = p["screen"];
                    t.width = s.value("width", t.width); t.height = s.value("height", t.height); t.format = s.value("format", t.format);
                    t.areaX = s.value("areaX", t.areaX); t.areaY = s.value("areaY", t.areaY); t.areaW = s.value("areaW", t.areaW); t.areaH = s.value("areaH", t.areaH);
                    t.flipX = s.value("flipX", t.flipX); t.flipY = s.value("flipY", t.flipY); t.design = s.value("design", t.design); t.logo = s.value("logo", t.logo);
                    t.target = std::clamp(s.value("target", t.target), 0, 8); t.scale = std::clamp(s.value("scale", t.scale), 0.6, 1.6);
                    t.logoScale = std::clamp(s.value("logoScale", t.logoScale), 0.2, 1.0);
                    t.logoSize = std::clamp(s.value("logoSize", t.logoSize), 0.5, 1.3);
                    t.durationSeconds = s.value("durationSeconds", t.durationSeconds);
                }
                if (p.contains("phone")) LoadPhone(p["phone"], t.phone);
                // an unchanged copy of a standard preset is dropped, so it keeps following the standard values of new versions
                bool standardCopy = false;
                auto approx = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
                for (auto& b : BuiltinPresets())
                    if (b.name == t.name && b.trucks == t.trucks && b.width == t.width && b.height == t.height && b.format == t.format &&
                        approx(b.areaX, t.areaX) && approx(b.areaY, t.areaY) && approx(b.areaW, t.areaW) && approx(b.areaH, t.areaH) &&
                        b.flipX == t.flipX && b.flipY == t.flipY && b.design == t.design && b.logo == t.logo && b.target == t.target && approx(b.scale, t.scale) && approx(b.logoScale, t.logoScale) && approx(b.logoSize, t.logoSize) && approx(b.durationSeconds, t.durationSeconds) && b.phone == t.phone)
                        standardCopy = true;
                if (!standardCopy) presets.push_back(t);
            }
        if (j.contains("mediaResume") && j["mediaResume"].is_object())
            for (auto& [k, v] : j["mediaResume"].items()) if (v.is_string()) mediaResume[k] = v.get<std::string>();
        if (j.contains("media")) {
            auto& m = j["media"]; MediaConfig& c = media;
            c.enabled = m.value("enabled", c.enabled); c.folder = m.value("folder", c.folder); c.label = m.value("label", c.label);
            c.folderPerDisc = m.value("folderPerDisc", c.folderPerDisc);
            c.shuffle = m.value("shuffle", c.shuffle); c.repeat = m.value("repeat", c.repeat);
            if (m.contains("types") && m["types"].is_object())
                for (auto& [k, v] : m["types"].items()) if (v.is_string()) c.types[k] = v.get<std::string>();
            c.textTrack = m.value("textTrack", c.textTrack); c.textPaused = m.value("textPaused", c.textPaused); c.textEmpty = m.value("textEmpty", c.textEmpty);
            if (c.textEmpty == "Keine Musik im USB-Ordner") { c.textEmpty = MediaConfig().textEmpty; migratedEmptyText = true; }   // old default (1.1.0 - 1.2.1)
        }
        if (j.contains("screen")) {
            auto& sc = j["screen"]; ScreenConfig& c = screen;
            c.enabled = sc.value("enabled", c.enabled);
            c.width = sc.value("width", c.width); c.height = sc.value("height", c.height); c.format = sc.value("format", c.format);
            c.probe = sc.value("probe", c.probe); c.probeSeconds = std::clamp(sc.value("probeSeconds", c.probeSeconds), 1.0, 30.0);
            c.durationSeconds = std::clamp(sc.value("durationSeconds", c.durationSeconds), 0.5, 60.0);
            c.areaX = sc.value("areaX", c.areaX); c.areaY = sc.value("areaY", c.areaY);
            c.areaW = sc.value("areaW", c.areaW); c.areaH = sc.value("areaH", c.areaH);
            c.flipY = sc.value("flipY", c.flipY); c.flipX = sc.value("flipX", c.flipX); c.grid = sc.value("grid", c.grid);
            c.design = sc.value("design", c.design);
            c.dimWithLights = sc.value("dimWithLights", c.dimWithLights);
            c.volumeStyle = sc.value("volumeStyle", c.volumeStyle); if (c.volumeStyle != "bar") c.volumeStyle = "card";
            c.startLogo = sc.value("startLogo", c.startLogo);
            c.startLogoSeconds = std::clamp(sc.value("startLogoSeconds", c.startLogoSeconds), 1.5, 10.0);
            c.startLogoFile = sc.value("startLogoFile", c.startLogoFile);
            c.startLogoScale = std::clamp(sc.value("startLogoScale", c.startLogoScale), 0.2, 1.0);
            c.target = std::clamp(sc.value("target", c.target), 0, 8);
            c.scale = std::clamp(sc.value("scale", c.scale), 0.6, 1.6);
            c.logoSize = std::clamp(sc.value("logoSize", c.logoSize), 0.5, 1.3);
        }
        if (j.contains("phone")) LoadPhone(j["phone"], screen.phone);
        if (j.contains("display")) {
            auto& d = j["display"];
            display.enabled = d.value("enabled", display.enabled);
            display.width = std::clamp(d.value("width", display.width), 160, 3840);
            display.height = std::clamp(d.value("height", display.height), 120, 2160);
            display.offscreen = d.value("offscreen", display.offscreen);
            display.title = d.value("title", display.title);
        }
        if (j.contains("overlay")) {
            auto& o = j["overlay"]; OverlayConfig& c = overlay;
            c.style = o.value("style", c.style);
            if (o.contains("radio")) {
                auto& r = o["radio"]; RadioStyleConfig& rc = c.radio;
                rc.position = r.value("position", rc.position);
                rc.offsetX = r.value("offsetX", rc.offsetX); rc.offsetY = r.value("offsetY", rc.offsetY);
                rc.scale = std::clamp(r.value("scale", rc.scale), 0.3, 4.0);
                rc.columns = std::clamp(r.value("columns", rc.columns), 8, 40);
                rc.color = r.value("color", rc.color);
                rc.scrollSeconds = std::clamp(r.value("scrollSeconds", rc.scrollSeconds), 0.05, 2.0);
            }
            c.mode = o.value("mode", c.mode); c.position = o.value("position", c.position);
            c.offsetX = o.value("offsetX", c.offsetX); c.offsetY = o.value("offsetY", c.offsetY);
            c.scale = std::clamp(o.value("scale", c.scale), 0.3, 4.0); c.durationSeconds = o.value("durationSeconds", c.durationSeconds);
            c.alwaysVisible = o.value("alwaysVisible", c.alwaysVisible); c.showLogos = o.value("showLogos", c.showLogos);
            c.logoCacheMB = std::clamp(o.value("logoCacheMB", c.logoCacheMB), 5, 2000);
            c.urlPrefix = o.value("urlPrefix", c.urlPrefix);
            if (c.urlPrefix.find("localradio.koenvh.nl") != std::string::npos) {   // old default from earlier versions
                c.urlPrefix = OverlayConfig().urlPrefix;
                migratedUrlPrefix = true;
            }
            c.textNowPlaying = o.value("textNowPlaying", c.textNowPlaying); c.textVolume = o.value("textVolume", c.textVolume);
            c.textStopped = o.value("textStopped", c.textStopped); c.textMuted = o.value("textMuted", c.textMuted);
            c.textFavourite = o.value("textFavourite", c.textFavourite); c.textConnecting = o.value("textConnecting", c.textConnecting);
            c.textUnavailable = o.value("textUnavailable", c.textUnavailable);
            c.textPowerOff = o.value("textPowerOff", c.textPowerOff);
            c.textRadio = o.value("textRadio", c.textRadio); c.textStations = o.value("textStations", c.textStations);
        }
        for (int i = 0; i < (int)Action::Count; ++i) {
            if (j.contains("keys") && j["keys"].contains(kActionNames[i]))
                keys[i] = ParseKeyBinding(j["keys"][kActionNames[i]].get<std::string>());
            if (j.contains("buttons") && j["buttons"].contains(kActionNames[i])) {
                const json& v = j["buttons"][kActionNames[i]];
                if (v.is_number_integer()) buttons[i] = std::clamp(v.get<int>(), -1, 143);   // any controller (older files)
                else if (v.is_string()) buttons[i] = ButtonFromText(v.get<std::string>());   // "Device name/12"
            }
        }
        // Earlier versions wrote PageUp / Next / Oemplus / OemMinus as defaults. If the file still has exactly
        // those (i.e. the user never changed them), switch to the new default: arrow keys.
        bool migratedKeys = false;
        if (j.contains("keys")) {
            auto& k = j["keys"];
            auto is = [&](const char* name, const char* val) { return k.contains(name) && k[name].is_string() && k[name].get<std::string>() == val; };
            if (is("previous", "PageUp") && is("next", "Next") && is("volumeUp", "Oemplus") && is("volumeDown", "OemMinus")) {
                keys[(int)Action::Previous] = ParseKeyBinding("Left");
                keys[(int)Action::Next] = ParseKeyBinding("Right");
                keys[(int)Action::VolumeUp] = ParseKeyBinding("Up");
                keys[(int)Action::VolumeDown] = ParseKeyBinding("Down");
                migratedKeys = true;
            }
        }
        // 1.1.0: add the USB/CD settings to files written by older versions, so they can be found and changed
        // 1.2.0: keys nextDisc / previousDisc and media.folderPerDisc
        std::vector<std::string> missingKeys;
        if (j.contains("keys") && j["keys"].is_object())
            for (const char* n : {"source", "nextDisc", "previousDisc", "screenPage", "menu", "band", "memory1", "memory2", "memory3", "memory4",
                                  "memory5", "memory6", "memory7", "memory8", "memory9", "tuneUp", "tuneDown", "menu2", "tunnelMark"}) if (!j["keys"].contains(n)) missingKeys.push_back(n);
        bool addSource = !missingKeys.empty();
        bool addMedia = !j.contains("media");
        bool addFolderPerDisc = !addMedia && j["media"].is_object() && !j["media"].contains("folderPerDisc");
        if (migratedUrlPrefix || migratedKeys || addSource || addMedia || addFolderPerDisc || migratedEmptyText) {   // write the changes back so the file matches what is used
            f.close();
            try {
                std::ifstream in{std::filesystem::path(path)};
                json k = json::parse(in, nullptr, true, true); in.close();
                if (migratedUrlPrefix) { k["overlay"]["urlPrefix"] = overlay.urlPrefix; Log::Write("Logo address updated to %s", overlay.urlPrefix.c_str()); }
                if (migratedKeys) {
                    k["keys"]["previous"] = "Left"; k["keys"]["next"] = "Right"; k["keys"]["volumeUp"] = "Up"; k["keys"]["volumeDown"] = "Down";
                    Log::Write("Keys updated to the new defaults: Left/Right = station, Up/Down = volume");
                }
                for (auto& n : missingKeys) {
                    std::string def;
                    for (int a = 0; a < (int)Action::Count; ++a) if (n == kActionNames[a]) def = KeyBindingName(Config().keys[a]);
                    k["keys"][n] = def;
                    if (k.contains("buttons") && k["buttons"].is_object() && !k["buttons"].contains(n)) k["buttons"][n] = -1;
                    Log::Write("Added key '%s' to the settings", n.c_str());
                }
                if (migratedEmptyText) k["media"]["textEmpty"] = media.textEmpty;
                if (addFolderPerDisc) { k["media"]["folderPerDisc"] = true; Log::Write("Added media.folderPerDisc (every sub folder of usb is its own USB / CD)"); }
                if (addMedia) {
                    k["media"] = json::parse(R"({"enabled": true, "folder": "usb", "folderPerDisc": true, "label": "USB", "shuffle": false, "repeat": "all",
                        "textTrack": "Titel", "textPaused": "Pause", "textEmpty": "Kein Medientyp erkannt"})");
                    Log::Write("Added settings block 'media' (USB/CD mode)");
                }
                std::ofstream o{std::filesystem::path(path)}; o << k.dump(2);
            } catch (...) {}
        }
        return true;
    } catch (const std::exception& e) {
        Log::Write("Config parse error (%s), using defaults", e.what());
        return false;
    }
}

std::string KeyBindingName(const KeyBinding& b) {
    if (!b.vk) return "";
    std::string n;
    int vk = b.vk;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) n = std::string(1, (char)vk);
    else if (vk >= VK_F1 && vk <= VK_F24) n = "F" + std::to_string(vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) n = "NumPad" + std::to_string(vk - VK_NUMPAD0);
    else {
        static const std::map<int, const char*> m = {
            {VK_PRIOR, "PageUp"}, {VK_NEXT, "Next"}, {VK_END, "End"}, {VK_HOME, "Home"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"},
            {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_PAUSE, "Pause"}, {VK_SCROLL, "Scroll"},
            {VK_SPACE, "Space"}, {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_BACK, "Back"}, {VK_OEM_PLUS, "Oemplus"},
            {VK_OEM_MINUS, "OemMinus"}, {VK_OEM_COMMA, "Oemcomma"}, {VK_OEM_PERIOD, "OemPeriod"}, {VK_ADD, "Add"},
            {VK_SUBTRACT, "Subtract"}, {VK_MULTIPLY, "Multiply"}, {VK_DIVIDE, "Divide"}, {VK_DECIMAL, "Decimal"},
            {VK_MEDIA_NEXT_TRACK, "MediaNextTrack"}, {VK_MEDIA_PREV_TRACK, "MediaPreviousTrack"}, {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"},
            {VK_MEDIA_STOP, "MediaStop"}, {VK_VOLUME_UP, "VolumeUp"}, {VK_VOLUME_DOWN, "VolumeDown"}, {VK_VOLUME_MUTE, "VolumeMute"}};
        auto it = m.find(vk);
        if (it != m.end()) n = it->second;
        else { char h[8]; snprintf(h, sizeof(h), "0x%02X", vk); n = h; }   // any other key (e.g. umlauts): virtual-key code
    }
    return std::string(b.ctrl ? "Ctrl+" : "") + (b.shift ? "Shift+" : "") + (b.alt ? "Alt+" : "") + n;
}

void Config::SaveBindings(const std::wstring& path) const {
    try {
        std::ifstream f{std::filesystem::path(path)};
        json j = f ? json::parse(f, nullptr, true, true) : json::object();
        f.close();
        for (int i = 0; i < (int)Action::Count; ++i) {
            j["keys"][kActionNames[i]] = KeyBindingName(keys[i]);
            std::string named = ButtonToText(buttons[i]);
            if (named.empty()) j["buttons"][kActionNames[i]] = buttons[i] < 0 ? -1 : buttons[i] % kPadSlot; else j["buttons"][kActionNames[i]] = named;
        }
        std::ofstream o{std::filesystem::path(path)}; o << j.dump(2);
    } catch (...) { Log::Write("Could not save key bindings"); }
}

static double R6(double v) { return std::round(v * 1e6) / 1e6; }   // 0.58 instead of 0.5800000000000001 in the file

static json MenuJsonObj(const Config& c) {
    json j;
    j["radius"] = c.radius; j["threshold"] = c.threshold; j["whitenoise"] = c.whitenoise; j["transitionWhitenoise"] = c.transitionWhitenoise;
    j["volume"] = c.volume; j["muted"] = c.muted; j["autoVolume"] = c.autoVolume; j["autoPlay"] = c.autoPlay; j["powerSource"] = c.powerSource;
    j["menuLanguage"] = c.menuLanguage; j["screenPage"] = c.screenPage;
    j["activePreset"] = c.activePreset; j["presetAuto"] = c.presetAuto; j["truckMemory"] = c.truckMemory;
    j["band"] = c.band; j["dabRange"] = c.dabRange; j["switchMode"] = c.switchMode;
    j["memoryButtons"] = c.memoryButtons; j["memoryHoldSeconds"] = c.memoryHoldSeconds;
    j["tuning"] = c.tuning; j["rdsAf"] = c.rdsAf; j["tunnelSignal"] = c.tunnelSignal; j["songTitle"] = c.songTitle; j["audioOutput"] = c.audioOutput; j["muteGameRadio"] = c.muteGameRadio; j["outsideFade"] = c.outsideFade; j["outsideDistance"] = c.outsideDistance;
    j["presets"] = json::array();
    for (auto& p : c.presets) {
        json o; o["name"] = p.name; o["trucks"] = p.trucks;
        o["screen"] = {{"width", p.width}, {"height", p.height}, {"format", p.format}, {"areaX", R6(p.areaX)}, {"areaY", R6(p.areaY)}, {"areaW", R6(p.areaW)},
                       {"areaH", R6(p.areaH)}, {"flipX", p.flipX}, {"flipY", p.flipY}, {"design", p.design}, {"durationSeconds", p.durationSeconds},
                       {"logo", p.logo}, {"target", p.target}, {"scale", R6(p.scale)}, {"logoScale", R6(p.logoScale)}, {"logoSize", R6(p.logoSize)}};
        o["phone"] = PhoneJson(p.phone);
        j["presets"].push_back(o);
    }
    j["phone"] = PhoneJson(c.screen.phone);
    j["screen"]["enabled"] = c.screen.enabled; j["screen"]["durationSeconds"] = c.screen.durationSeconds;
    j["screen"]["areaX"] = R6(c.screen.areaX); j["screen"]["areaY"] = R6(c.screen.areaY); j["screen"]["areaW"] = R6(c.screen.areaW); j["screen"]["areaH"] = R6(c.screen.areaH);
    j["screen"]["flipX"] = c.screen.flipX; j["screen"]["flipY"] = c.screen.flipY; j["screen"]["design"] = c.screen.design; j["screen"]["volumeStyle"] = c.screen.volumeStyle; j["screen"]["dimWithLights"] = c.screen.dimWithLights;
    j["screen"]["startLogo"] = c.screen.startLogo; j["screen"]["startLogoSeconds"] = c.screen.startLogoSeconds;
    j["screen"]["startLogoFile"] = c.screen.startLogoFile; j["screen"]["startLogoScale"] = R6(c.screen.startLogoScale);
    j["screen"]["target"] = c.screen.target; j["screen"]["scale"] = R6(c.screen.scale); j["screen"]["logoSize"] = R6(c.screen.logoSize);
    j["screen"]["grid"] = false;   // test mode is never saved as on
    j["screen"]["width"] = c.screen.width; j["screen"]["height"] = c.screen.height; j["screen"]["format"] = c.screen.format;
    auto& o = j["overlay"];
    o["mode"] = c.overlay.mode; o["style"] = c.overlay.style; o["position"] = c.overlay.position; o["scale"] = c.overlay.scale;
    o["durationSeconds"] = c.overlay.durationSeconds; o["showLogos"] = c.overlay.showLogos; o["logoCacheMB"] = c.overlay.logoCacheMB;
    o["radio"]["position"] = c.overlay.radio.position; o["radio"]["scale"] = c.overlay.radio.scale;
    auto& m = j["media"];
    m["enabled"] = c.media.enabled; m["folderPerDisc"] = c.media.folderPerDisc; m["label"] = c.media.label;
    m["shuffle"] = c.media.shuffle; m["repeat"] = c.media.repeat;
    m["types"] = json::object();
    for (auto& [k, v] : c.media.types) m["types"][k] = v;
    for (int i = 0; i < (int)Action::Count; ++i) {
        j["keys"][kActionNames[i]] = KeyBindingName(c.keys[i]);
        std::string named = c.ButtonToText(c.buttons[i]);
        if (named.empty()) j["buttons"][kActionNames[i]] = c.buttons[i] < 0 ? -1 : c.buttons[i] % Config::kPadSlot; else j["buttons"][kActionNames[i]] = named;
    }
    return j;
}
std::string Config::MenuJson() const { return MenuJsonObj(*this).dump(); }

void Config::SaveMenuSettings(const std::wstring& path) const {
    try {
        std::ifstream f{std::filesystem::path(path)};
        json j = f ? json::parse(f, nullptr, true, true) : json::object();
        f.close();
        j.merge_patch(MenuJsonObj(*this));
        std::ofstream o{std::filesystem::path(path)}; o << j.dump(2);
        Log::Write("Settings saved");
    } catch (...) { Log::Write("Could not save the settings"); }
}

void CopyMenuSettings(Config& d, const Config& s) {
    d.radius = s.radius; d.threshold = s.threshold; d.whitenoise = s.whitenoise; d.transitionWhitenoise = s.transitionWhitenoise;
    d.volume = s.volume; d.muted = s.muted; d.autoVolume = s.autoVolume; d.autoPlay = s.autoPlay; d.powerSource = s.powerSource;
    d.menuLanguage = s.menuLanguage; d.screenPage = s.screenPage;
    d.presets = s.presets; d.activePreset = s.activePreset; d.presetAuto = s.presetAuto; d.truckMemory = s.truckMemory;
    d.band = s.band; d.dabRange = s.dabRange; d.switchMode = s.switchMode; d.memoryButtons = s.memoryButtons; d.memoryHoldSeconds = s.memoryHoldSeconds;
    d.tuning = s.tuning; d.rdsAf = s.rdsAf; d.tunnelSignal = s.tunnelSignal; d.songTitle = s.songTitle; d.audioOutput = s.audioOutput; d.muteGameRadio = s.muteGameRadio; d.outsideFade = s.outsideFade; d.outsideDistance = s.outsideDistance;
    d.screen.width = s.screen.width; d.screen.height = s.screen.height; d.screen.format = s.screen.format;
    d.screen.enabled = s.screen.enabled; d.screen.durationSeconds = s.screen.durationSeconds;
    d.screen.areaX = s.screen.areaX; d.screen.areaY = s.screen.areaY; d.screen.areaW = s.screen.areaW; d.screen.areaH = s.screen.areaH;
    d.screen.flipX = s.screen.flipX; d.screen.flipY = s.screen.flipY; d.screen.design = s.screen.design; d.screen.volumeStyle = s.screen.volumeStyle; d.screen.dimWithLights = s.screen.dimWithLights;
    d.screen.startLogo = s.screen.startLogo; d.screen.startLogoSeconds = s.screen.startLogoSeconds;
    d.screen.phone = s.screen.phone;
    d.screen.startLogoFile = s.screen.startLogoFile; d.screen.startLogoScale = s.screen.startLogoScale;
    d.screen.target = s.screen.target; d.screen.scale = s.screen.scale; d.screen.logoSize = s.screen.logoSize;
    d.screen.grid = s.screen.grid;   // test mode: applied live, not part of MenuJson (not saved)
    d.overlay.mode = s.overlay.mode; d.overlay.style = s.overlay.style; d.overlay.position = s.overlay.position; d.overlay.scale = s.overlay.scale;
    d.overlay.durationSeconds = s.overlay.durationSeconds; d.overlay.showLogos = s.overlay.showLogos; d.overlay.logoCacheMB = s.overlay.logoCacheMB;
    d.overlay.radio.position = s.overlay.radio.position; d.overlay.radio.scale = s.overlay.radio.scale;
    d.media.enabled = s.media.enabled; d.media.folderPerDisc = s.media.folderPerDisc; d.media.label = s.media.label;
    d.media.shuffle = s.media.shuffle; d.media.repeat = s.media.repeat; d.media.types = s.media.types;
    d.controllers = s.controllers;
    for (int i = 0; i < (int)Action::Count; ++i) { d.keys[i] = s.keys[i]; d.buttons[i] = s.buttons[i]; }
}

TruckPreset PresetFromScreen(const std::string& name, const ScreenConfig& s) {
    TruckPreset p; p.name = name;
    p.width = s.width; p.height = s.height; p.format = s.format;
    p.areaX = s.areaX; p.areaY = s.areaY; p.areaW = s.areaW; p.areaH = s.areaH;
    p.flipX = s.flipX; p.flipY = s.flipY; p.design = s.design; p.durationSeconds = s.durationSeconds;
    p.logo = s.startLogoFile; p.target = s.target; p.scale = s.scale; p.logoScale = s.startLogoScale; p.logoSize = s.logoSize;
    p.phone = s.phone;
    return p;
}
void ApplyPreset(ScreenConfig& s, const TruckPreset& p) {
    s.width = p.width; s.height = p.height; s.format = p.format;
    s.areaX = p.areaX; s.areaY = p.areaY; s.areaW = p.areaW; s.areaH = p.areaH;
    s.flipX = p.flipX; s.flipY = p.flipY; s.design = p.design; s.durationSeconds = p.durationSeconds;
    s.startLogoFile = p.logo; s.target = p.target; s.scale = p.scale; s.startLogoScale = p.logoScale; s.logoSize = p.logoSize;
    s.phone = p.phone;
}
namespace {
TruckPreset Std(const char* name, std::vector<std::string> trucks, double x, double y, double w, double h, bool flipY, int target = 1) {
    TruckPreset p; p.name = name; p.trucks = std::move(trucks);
    p.width = 1024; p.height = 1024; p.format = 29; p.flipX = false; p.flipY = flipY; p.design = "modern"; p.durationSeconds = 4.0;
    p.areaX = x; p.areaY = y; p.areaW = w; p.areaH = h; p.target = target;
    return p;
}
}
// Set up and tested in the game by ScufyKing (1.4.1). Upside down only on drawing surface 2 (Mercedes). Scania R and S share the cab: the standard preset is the normal
// screen, "Scania R/S Smart Dashboard" is linked to no truck - choose it (or link it) for trucks with the smart dashboard.
const std::vector<TruckPreset>& BuiltinPresets() {
    static const std::vector<TruckPreset> list = {
        Std("MAN TGX", {"vehicle.man.tgx_2020"}, 0.11, 0.24, 0.78, 0.58, false),
        Std("Scania R/S", {"vehicle.scania.r_2016", "vehicle.scania.s_2016"}, 0.24, 0.16, 0.52, 0.59, false),
        Std("Scania R/S Smart Dashboard", {}, 0.12, 0.25, 0.77, 0.52, false),
        Std("Mercedes-Benz New Actros", {"vehicle.mercedes.actros2014"}, 0.25, 0.25, 0.50, 0.56, true, 2),
        Std("DAF NGD", {"vehicle.daf.2021"}, 0.10, 0.26, 0.78, 0.44, false),
        Std("Iveco S-Way", {"vehicle.iveco.sway"}, 0.02, 0.32, 0.75, 0.33, false),
    };
    return list;
}
TruckPreset* FindPreset(Config& c, const std::string& name) {
    if (name.empty()) return nullptr;
    for (auto& p : c.presets) if (p.name == name) return &p;
    return nullptr;
}
const TruckPreset* LookupPreset(const Config& c, const std::string& name) {
    if (TruckPreset* p = FindPreset(const_cast<Config&>(c), name)) return p;
    if (name.empty()) return nullptr;
    for (auto& p : BuiltinPresets()) if (p.name == name) return &p;
    return nullptr;
}
bool IsStandardPreset(const Config& c, const std::string& name) {
    return !FindPreset(const_cast<Config&>(c), name) && LookupPreset(c, name) != nullptr;
}
const TruckPreset* PresetForTruck(const Config& c, const std::string& id) {
    if (id.empty()) return nullptr;
    for (auto& p : c.presets) for (auto& t : p.trucks) if (t == id) return &p;
    for (auto& p : BuiltinPresets())
        if (!FindPreset(const_cast<Config&>(c), p.name)) for (auto& t : p.trucks) if (t == id) return &p;
    return nullptr;
}
std::vector<const TruckPreset*> AllPresets(const Config& c) {
    std::vector<const TruckPreset*> v;
    for (auto& p : c.presets) v.push_back(&p);
    for (auto& p : BuiltinPresets()) if (!FindPreset(const_cast<Config&>(c), p.name)) v.push_back(&p);
    return v;
}
TruckPreset* OwnPreset(Config& c, const std::string& name) {
    if (TruckPreset* p = FindPreset(c, name)) return p;
    const TruckPreset* s = LookupPreset(c, name);
    if (!s) return nullptr;
    c.presets.push_back(*s);
    return &c.presets.back();
}

namespace {
bool SameScreen(const TruckPreset& a, const TruckPreset& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.areaX == b.areaX && a.areaY == b.areaY &&
           a.areaW == b.areaW && a.areaH == b.areaH && a.flipX == b.flipX && a.flipY == b.flipY && a.design == b.design && a.logo == b.logo && a.target == b.target && a.scale == b.scale && a.logoScale == b.logoScale && a.logoSize == b.logoSize &&
           a.durationSeconds == b.durationSeconds && a.phone == b.phone;
}
}
bool AutoSelectPreset(Config& c, const std::string& id) {
    if (id.empty()) return false;
    if (const TruckPreset* p = PresetForTruck(c, id)) {
        bool changed = !SameScreen(PresetFromScreen(p->name, c.screen), *p) || c.activePreset != p->name;
        ApplyPreset(c.screen, *p);
        c.activePreset = p->name;
        return changed;
    }
    const TruckPreset* a = LookupPreset(c, c.activePreset);
    if (a && !a->trucks.empty()) { c.activePreset.clear(); return true; }
    return false;
}
void StoreActivePreset(Config& c) {
    const TruckPreset* a = LookupPreset(c, c.activePreset);
    if (!a) return;
    TruckPreset p = PresetFromScreen(a->name, c.screen);
    if (SameScreen(p, *a)) return;                  // unchanged: a standard preset stays a standard preset
    TruckPreset* own = OwnPreset(c, c.activePreset);   // a changed standard preset becomes an own preset (same name and trucks)
    p.trucks = own->trucks;
    *own = p;
}

void Config::SaveVolume(const std::wstring& path) const {
    try {
        std::ifstream f{std::filesystem::path(path)};
        json j = f ? json::parse(f, nullptr, true, true) : json::object();
        j["volume"] = volume; j["muted"] = muted; j["band"] = band; j["tunedFreq"] = tunedFreq;
        j["stationButtons"] = stationButtons;
        j["source"] = source; j["mediaTrack"] = mediaTrack; j["screenPage"] = screenPage;
        json r = json::object();
        for (auto& [k, v] : mediaResume) r[k] = v;
        j["mediaResume"] = r;
        std::ofstream o{std::filesystem::path(path)}; o << j.dump(2);
    } catch (...) {}
}

// --- controller buttons ---------------------------------------------------------------------------------------------
namespace {
const char* kPovDir[] = {"up", "right", "down", "left"};
}
int Config::ButtonSlot(const std::string& device) {
    if (device.empty()) return 0;
    for (size_t i = 0; i < controllers.size(); ++i) if (controllers[i] == device) return (int)i + 1;
    controllers.push_back(device);
    return (int)controllers.size();
}
int Config::ButtonFromText(const std::string& v) {
    size_t p = v.rfind('/');
    if (p == std::string::npos || p == 0) return -1;
    std::string dev = v.substr(0, p), b = v.substr(p + 1);
    int btn = -1;
    if (b.rfind("POV", 0) == 0) {   // "POV1 up"
        int n = atoi(b.c_str() + 3);
        size_t sp = b.find(' ');
        std::string d = sp == std::string::npos ? "" : b.substr(sp + 1);
        for (int k = 0; k < 4; ++k) if (d == kPovDir[k] && n >= 1 && n <= 4) btn = 128 + (n - 1) * 4 + k;
    } else if (!b.empty()) btn = atoi(b.c_str()) - 1;
    if (btn < 0 || btn >= 144) return -1;
    return ButtonSlot(dev) * kPadSlot + btn;
}
std::string Config::ButtonToText(int b) const {
    if (b < 0) return "";
    int slot = b / kPadSlot, btn = b % kPadSlot;
    if (slot <= 0 || slot > (int)controllers.size()) return "";
    std::string s = controllers[slot - 1] + "/";
    if (btn >= 128) s += "POV" + std::to_string((btn - 128) / 4 + 1) + " " + kPovDir[(btn - 128) % 4];
    else s += std::to_string(btn + 1);
    return s;
}
std::wstring Config::ButtonText(int b, bool en) const {
    if (b < 0) return L"\u2013";
    int slot = b / kPadSlot, btn = b % kPadSlot;
    std::wstring t;
    if (btn >= 128) {
        static const wchar_t* arrows[] = {L"\u2191", L"\u2192", L"\u2193", L"\u2190"};
        t = L"POV" + ((btn - 128) / 4 ? std::to_wstring((btn - 128) / 4 + 1) : std::wstring()) + L" " + arrows[(btn - 128) % 4];
    } else t = std::to_wstring(btn + 1);
    if (slot > 0 && slot <= (int)controllers.size()) {   // short device name: first word ("MOZA", "Logitech", "Xbox")
        std::wstring n = Utf8ToWide(controllers[slot - 1]);
        size_t sp = n.find(L' ');
        if (sp != std::wstring::npos && sp >= 2) n = n.substr(0, sp);
        if (n.size() > 10) n = n.substr(0, 10);
        return n + L" " + t;
    }
    return (btn >= 128 ? L"" : std::wstring(en ? L"Button " : L"Knopf ")) + t;
}
