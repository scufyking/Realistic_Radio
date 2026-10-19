// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include "DisplayWindow.h"

enum class Action { Next, Previous, Stop, VolumeUp, VolumeDown, MakeFavourite, GoToFavourite, Mute, ShowOverlay, Source, NextDisc, PreviousDisc, ScreenPage, Menu,
                    Band, Memory1, Memory2, Memory3, Memory4, Memory5, Memory6, Memory7, Memory8, Memory9, TuneUp, TuneDown, MenuAlt, TunnelMark, Count };
inline bool IsMemoryAction(Action a) { return a >= Action::Memory1 && a <= Action::Memory9; }

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
    bool flipY = false;                 // upside down: only needed on drawing surface 2 (target 2)
    bool flipX = false;                 // flipX + flipY = rotated by 180 degrees
    bool grid = false;                  // test mode: coordinate grid + the card's area on the target (to find areaX/Y/W/H)
    std::string design = "modern";      // look of the card: modern | ocean | sport | light | retro
    bool startLogo = true;              // start screen with the manufacturer's logo (truck-logos\\) or name when the ignition is switched on
    double startLogoSeconds = 3.5;
    double startLogoScale = 0.55;       // height of the start logo, part of the display area (0.2 - 1)
    int target = 1;                     // several textures of that size per frame: draw into the 1st, 2nd, ... (0 = all)
    double scale = 1.0;                 // size of text and pictures on the display (0.6 - 1.6, never taller than the area)
    double logoSize = 1.0;              // station logo / cover tile (0.5 - 1.3)
    std::string startLogoFile;          // picture in truck-logos\\ ("" = automatic: the first one for the truck, "text" = only the name)
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
    std::string textEmpty = "Kein Medientyp erkannt";
    // media type per folder (folder name -> usb | cd | sd | cassette | bluetooth | aux); folders not listed use `label`
    std::map<std::string, std::string> types;
};

// Truck preset: the in-cab screen settings for one or more trucks (screen target, area, flip, design, time)
struct TruckPreset {
    std::string name;
    std::vector<std::string> trucks;    // truck ids from the game (e.g. vehicle.man.tgx_2020) this preset is used for
    int width = 1024, height = 1024, format = 29;
    double areaX = 0.1, areaY = 0.3, areaW = 0.8, areaH = 0.4, durationSeconds = 4.0;
    bool flipX = false, flipY = false;
    std::string design = "modern";
    std::string logo;                   // start logo (ScreenConfig::startLogoFile)
    int target = 1;
    double scale = 1.0, logoScale = 0.55, logoSize = 1.0;
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
    std::map<std::string, std::string> mediaResume;   // last track per USB / CD (folder name -> track), remembered
    std::string screenPage = "navi";   // navi | radio: radio page kept on the in-cab screen (remembered)
    std::string menuLanguage = "de";   // key menu (Ctrl+0): de | en
    std::vector<TruckPreset> presets;
    std::string activePreset;          // name, empty = none
    bool presetAuto = true;            // load the preset linked to the truck when the truck changes
    bool truckMemory = false;          // every truck remembers its own stations, favourites, volume, source and USB track
    // Radio (1.4.0)
    std::string band = "fm";           // fm | dab (remembered)
    double dabRange = 1.5;             // DAB+ reaches this much further than FM
    std::string switchMode = "lost";   // lost: change region only when the station is out of range | better: when another region is received better
    bool memoryButtons = true;         // station buttons 1-9 (numpad): tap = select, hold = save
    double memoryHoldSeconds = 1.5;
    std::map<std::string, std::string> stationButtons;   // "1".."9" -> "<region>|<station name>" or "f:<0.1 MHz>" (remembered)
    std::string tuning = "list";       // list: stations in range one after another | frequency: FM dial like an old radio
    bool rdsAf = false;
    bool tunnelSignal = true;          // no reception in tunnels (FM: static, DAB+: drop-outs then silence); USB / CD keeps playing                // frequency radio: follow the programme to another frequency (RDS AF); off = like in the old days
    int tunedFreq = 0;                 // last FM frequency in 0.1 MHz (remembered)
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
    bool migratedEmptyText = false;
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
std::string KeyBindingName(const KeyBinding& b);   // inverse of ParseKeyBinding, e.g. "Ctrl+0", "Right", "" = none
void CopyMenuSettings(Config& dst, const Config& src);   // only the values the settings menu edits
TruckPreset PresetFromScreen(const std::string& name, const ScreenConfig& s);
void ApplyPreset(ScreenConfig& s, const TruckPreset& p);
// Standard presets built into the DLL (set up by ScufyKing). They are used until the player saves an own preset with
// the same name (= override) or links the truck to an own preset; they are never written to realistic-radio.json.
const std::vector<TruckPreset>& BuiltinPresets();
TruckPreset* FindPreset(Config& c, const std::string& name);                 // own preset by name, nullptr = none
const TruckPreset* LookupPreset(const Config& c, const std::string& name);   // own preset, else standard preset
bool IsStandardPreset(const Config& c, const std::string& name);              // standard preset without own override
const TruckPreset* PresetForTruck(const Config& c, const std::string& truckId);   // own first, then standard; nullptr = none
std::vector<const TruckPreset*> AllPresets(const Config& c);                  // own presets + standard presets not overridden
TruckPreset* OwnPreset(Config& c, const std::string& name);   // own preset by name; a standard preset is copied first
// Truck changed (presetAuto): load the truck's preset into c.screen. Without a preset of its own, a preset that
// belongs to other trucks is deselected (the screen values stay) so later changes do not overwrite it.
// Returns true when c.screen or c.activePreset changed.
bool AutoSelectPreset(Config& c, const std::string& truckId);
void StoreActivePreset(Config& c);   // copy c.screen into the active preset (after a change in the menu)
inline bool operator==(const KeyBinding& a, const KeyBinding& b) { return a.vk == b.vk && a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt; }
