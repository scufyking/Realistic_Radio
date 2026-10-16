// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Config.h"
#include "Logger.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

using nlohmann::json;

static const char* kActionNames[] = {"next", "previous", "stop", "volumeUp", "volumeDown", "makeFavourite", "goToFavourite", "mute", "showOverlay"};
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
    "showOverlay": ""
  },
  "controllerIndex": 0,
  "buttons": {
    "previous": -1, "next": -1, "stop": -1, "volumeUp": -1,
    "volumeDown": -1, "makeFavourite": -1, "goToFavourite": -1, "mute": -1, "showOverlay": -1
  },
  "display": {
    "enabled": false,
    "width": 1280,
    "height": 720,
    "offscreen": true,
    "title": "Realistic Radio Display"
  },
  "overlay": {
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
    "mode": "auto",
    "position": "top-center",
    "offsetX": 0.0,
    "offsetY": 0.07,
    "scale": 1.0,
    "durationSeconds": 6,
    "alwaysVisible": false,
    "showLogos": true,
    "urlPrefix": "https://radio.scufystudio.de/logo/",
    "textNowPlaying": "Jetzt l\u00e4uft:",
    "textVolume": "Lautst\u00e4rke",
    "textStopped": "Radio aus",
    "textMuted": "Stumm",
    "textFavourite": "Als Favorit gespeichert",
    "textConnecting": "verbinde\u2026",
    "textUnavailable": "Sender nicht erreichbar",
    "textPowerOff": "Z\u00fcndung aus"
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
        muted = j.value("muted", muted);
        autoPlay = j.value("autoPlay", autoPlay);
        powerSource = j.value("powerSource", powerSource);
        dataFile = j.value("dataFile", dataFile);
        onlyWhenGameFocused = j.value("onlyWhenGameFocused", onlyWhenGameFocused);
        reconnectMaxSeconds = j.value("reconnectMaxSeconds", reconnectMaxSeconds);
        controllerIndex = j.value("controllerIndex", controllerIndex);
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
        }
        for (int i = 0; i < (int)Action::Count; ++i) {
            if (j.contains("keys") && j["keys"].contains(kActionNames[i]))
                keys[i] = ParseKeyBinding(j["keys"][kActionNames[i]].get<std::string>());
            if (j.contains("buttons") && j["buttons"].contains(kActionNames[i]))
                buttons[i] = j["buttons"][kActionNames[i]].get<int>();
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
        if (migratedUrlPrefix || migratedKeys) {   // write the changes back so the file matches what is used
            f.close();
            try {
                std::ifstream in{std::filesystem::path(path)};
                json k = json::parse(in, nullptr, true, true); in.close();
                if (migratedUrlPrefix) { k["overlay"]["urlPrefix"] = overlay.urlPrefix; Log::Write("Logo address updated to %s", overlay.urlPrefix.c_str()); }
                if (migratedKeys) {
                    k["keys"]["previous"] = "Left"; k["keys"]["next"] = "Right"; k["keys"]["volumeUp"] = "Up"; k["keys"]["volumeDown"] = "Down";
                    Log::Write("Keys updated to the new defaults: Left/Right = station, Up/Down = volume");
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

void Config::SaveVolume(const std::wstring& path) const {
    try {
        std::ifstream f{std::filesystem::path(path)};
        json j = f ? json::parse(f, nullptr, true, true) : json::object();
        j["volume"] = volume; j["muted"] = muted;
        std::ofstream o{std::filesystem::path(path)}; o << j.dump(2);
    } catch (...) {}
}
