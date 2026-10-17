// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include "DisplayWindow.h"

enum class Action { Next, Previous, Stop, VolumeUp, VolumeDown, MakeFavourite, GoToFavourite, Mute, ShowOverlay, Source, NextDisc, PreviousDisc, ScreenPage, Menu, Count };

struct KeyBinding { int vk = 0; bool ctrl = false, shift = false, alt = false; };

struct RadioStyleConfig {
    std::string position = "bottom-center";
    double offsetX = 0.0, offsetY = 0.20;
    double scale = 1.0;
    int columns = 16;                   // characters on the display
    std::string color = "amber";        // amber | green | blue | white | red | #RRGGBB
    double scrollSeconds = 0.3;         // marquee: seconds per character
};

struct ScreenConfig {
    bool enabled = true;                // show station/volume on the in-cab screen over the navigation
    // defaults: navigation screen of the MAN TGX in ETS2 1.61 (found with "probe" + "grid"); other trucks may differ
    int width = 1024, height = 1024;    // size of the screen's render target (find it with "probe")
    int format = 29;                    // DXGI format of that target (29 = R8G8B8A8_UNORM_SRGB), 0 = any
    bool probe = false;                 // mark all candidate render targets one after another with a test pattern
    double probeSeconds = 5.0;
    double durationSeconds = 4.0;
    double areaX = 0.1, areaY = 0.3, areaW = 0.8, areaH = 0.4;   // part of the render target to draw into (0..1)
    bool flipY = true;
    bool flipX = false;                 // flipX + flipY = rotated by 180 degrees
    bool grid = false;                  // show a coordinate grid on the configured target (to find areaX/Y/W/H)
};

// USB / CD mode: own music files from realistic-radio\usb\ (or another folder)
struct MediaConfig {
    bool enabled = true;
    std::string folder = "usb";         // relative to the realistic-radio folder, or a full path (e.g. D:\\Music)
    std::string label = "USB";          // name of the source on the screen, e.g. USB or CD
    bool folderPerDisc = true;          // every sub folder of the usb folder is its own USB / CD (false = one USB, as in 1.1.0)
    bool shuffle = false;
    std::string repeat = "all";         // all (start again after the last track) | one (repeat the track) | off (stop at the end)
    std::string textTrack = "Titel";
    std::string textPaused = "Pause";
    std::string textEmpty = "Keine Musik im USB-Ordner";
    // media type per folder (folder name -> usb | cd | sd | cassette | bluetooth | aux); folders not listed use `label`
    std::map<std::string, std::string> types;
};

struct OverlayConfig {
    std::string style = "panel";        // panel (info panel with logo) | radio (car-radio dot-matrix display)
    RadioStyleConfig radio;
    std::string mode = "off";           // off (default) | auto | d3d11 | window
    std::string position = "top-center"; // top-left | top-center | top-right | bottom-left | bottom-center | bottom-right
    double offsetX = 0.0;               // fraction of screen height
    double offsetY = 0.07;
    double scale = 1.0;
    double durationSeconds = 6.0;
    bool alwaysVisible = false;
    bool showLogos = true;
    int logoCacheMB = 30;               // downloaded logos / covers in realistic-radio\\logos\\: oldest are deleted above this size
    std::string urlPrefix = "https://radio.scufystudio.de/logo/";   // logos: <urlPrefix><logo path>, cached in realistic-radio\\logos\\; empty = local only
    std::string textNowPlaying = "Jetzt l\u00e4uft:";
    std::string textVolume = "Lautst\u00e4rke";
    std::string textStopped = "Radio aus";
    std::string textMuted = "Stumm";
    std::string textFavourite = "Als Favorit gespeichert";
    std::string textConnecting = "verbinde\u2026";
    std::string textUnavailable = "Sender nicht erreichbar";
    std::string textPowerOff = "Z\u00fcndung aus";
    std::string textRadio = "Radio";
    std::string textStations = "Sender";
};

struct Config {
    OverlayConfig overlay;
    ScreenConfig screen;
    DisplayConfig display;
    MediaConfig media;
    std::string source = "radio";   // radio | usb (remembered)
    std::string mediaTrack;         // last USB track, relative to the folder (remembered)
    std::map<std::string, std::string> mediaResume;
    std::string screenPage = "navi";
    std::string menuLanguage = "de";   // key menu (Ctrl+0): de | en   // navi | radio: radio page kept on the in-cab screen (remembered)   // last track per USB / CD (folder name -> track), remembered
    // Reception (original web/config.js)
    double radius = 7500.0;
    double threshold = 0.15;
    bool whitenoise = true;
    bool transitionWhitenoise = true;
    // Playback
    double volume = 0.6;          // 0..1 slider position; output gain = volume^2
    double autoVolume = 0.0;      // speed-dependent volume: 0 = off, 1 = up to +6 dB at 90 km/h (starts at 30 km/h)
    bool muted = false;
    bool autoPlay = true;
    std::string powerSource = "ignition";   // ignition (truck.electric.enabled) | engine (truck.engine.enabled) | always
    std::string dataFile;         // empty = "<game>.json" from game id
    // Input (original settings.json keys/buttons)
    KeyBinding keys[(int)Action::Count];
    int buttons[(int)Action::Count];
    int controllerIndex = 0;      // winmm joystick id (JOYSTICKID1 = 0)
    bool onlyWhenGameFocused = true;
    int reconnectMaxSeconds = 30;

    bool migratedUrlPrefix = false;
    Config();
    bool Load(const std::wstring& path);   // creates default file if missing
    void SaveVolume(const std::wstring& path) const;   // volume, mute, source and USB track
    void SaveBindings(const std::wstring& path) const; // keys + buttons
    // settings menu (Ctrl+0): every value it can change, as JSON in the file layout; saved by merging into the file
    std::string MenuJson() const;
    void SaveMenuSettings(const std::wstring& path) const;
};

int ParseKeyName(const std::string& name);
KeyBinding ParseKeyBinding(const std::string& s);
const char* ActionName(Action a);
std::string KeyBindingName(const KeyBinding& b);
void CopyMenuSettings(Config& dst, const Config& src);   // only the values the settings menu edits   // inverse of ParseKeyBinding, e.g. "Ctrl+0", "Right", "" = none
inline bool operator==(const KeyBinding& a, const KeyBinding& b) { return a.vk == b.vk && a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt; }
