// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "SettingsMenu.h"
#include "MenuInput.h"
#include "InputManager.h"
#include "D3D11Overlay.h"
#include "Logger.h"
#include "MediaLibrary.h"
#include "InputBlock.h"
#include <cmath>

// ------------------------------------------------------------------------------------------------
// Settings


namespace {
using D = SettingsMenu::Def;
struct Page { const char *de, *en, *sde, *sen; std::vector<D> defs; bool keys; };

std::wstring Num(double v, int decimals, bool en) {
    wchar_t b[32]; swprintf(b, 32, L"%.*f", decimals, v);
    std::wstring s = b;
    if (!en) for (auto& c : s) if (c == L'.') c = L',';
    return s;
}
D Button(const char* de, const char* en, const char* hde, const char* hen, const char* bde, const char* ben, int cmd) {
    D d; d.kind = MenuItemKind::Button; d.de = de; d.en = en; d.hde = hde; d.hen = hen; d.btnDe = bde; d.btnEn = ben; d.command = cmd;
    d.get = [](const Config&) { return 0.0; };
    d.set = [](Config&, double) {};
    return d;
}
D Heading(const char* de, const char* en) {
    D d; d.kind = MenuItemKind::Heading; d.de = de; d.en = en;
    d.get = [](const Config&) { return 0.0; };
    d.set = [](Config&, double) {};
    return d;
}
D KeyRow(const char* de, const char* en, const char* hde, const char* hen, Action a) {   // key binding inside a settings page
    D d; d.kind = MenuItemKind::Key; d.de = de; d.en = en; d.hde = hde; d.hen = hen; d.action = a;
    d.get = [](const Config&) { return 0.0; };
    d.set = [](Config&, double) {};
    return d;
}
D Toggle(const char* de, const char* en, const char* hde, const char* hen, std::function<bool&(Config&)> ref, bool restart = false) {
    D d; d.kind = MenuItemKind::Toggle; d.de = de; d.en = en; d.hde = hde; d.hen = hen; d.restart = restart;
    d.get = [ref](const Config& c) { return ref(const_cast<Config&>(c)) ? 1.0 : 0.0; };
    d.set = [ref](Config& c, double v) { ref(c) = v > 0.5; };
    return d;
}
D Slider(const char* de, const char* en, const char* hde, const char* hen, std::function<double&(Config&)> ref, double mn, double mx, double step,
         std::function<std::wstring(double, bool)> fmt) {
    D d; d.kind = MenuItemKind::Slider; d.de = de; d.en = en; d.hde = hde; d.hen = hen; d.mn = mn; d.mx = mx; d.step = step; d.fmt = fmt;
    d.get = [ref](const Config& c) { return ref(const_cast<Config&>(c)); };
    d.set = [ref, mn, mx, step](Config& c, double v) { ref(c) = std::clamp(std::round((mn + std::round((v - mn) / step) * step) * 1e6) / 1e6, mn, mx); };   // 0.58, not 0.5800000000000001
    return d;
}
D Choice(const char* de, const char* en, const char* hde, const char* hen, std::function<std::string&(Config&)> ref,
         std::vector<const char*> values, std::vector<std::pair<const char*, const char*>> labels, bool restart = false) {
    D d; d.kind = MenuItemKind::Choice; d.de = de; d.en = en; d.hde = hde; d.hen = hen; d.labels = labels; d.restart = restart;
    d.mx = (double)values.size() - 1;
    d.get = [ref, values](const Config& c) {
        const std::string& s = ref(const_cast<Config&>(c));
        for (size_t i = 0; i < values.size(); ++i) if (s == values[i]) return (double)i;
        return 0.0;
    };
    d.set = [ref, values](Config& c, double v) { ref(c) = values[std::clamp((int)std::lround(v), 0, (int)values.size() - 1)]; };
    return d;
}

const std::vector<Page>& Pages() {
    static const std::vector<Page> pages = [] {
        std::vector<Page> p;
        auto pct = [](double v, bool) { return std::to_wstring((int)std::lround(v * 100)) + L" %"; };
        auto secs = [](double v, bool en) { return Num(v, std::fmod(v, 1.0) == 0 ? 0 : 1, en) + L" s"; };
        auto two = [](double v, bool en) { return Num(v, 2, en); };
        p.push_back({"Radio", "Radio", "Wiedergabe", "Playback", {
            Slider("Lautstärke", "Volume", "", "", [](Config& c) -> double& { return c.volume; }, 0, 1, 0.05, pct),
            Toggle("Stumm", "Mute", "", "", [](Config& c) -> bool& { return c.muted; }),
            Choice("Radio an bei", "Radio on with", "Wann das Radio spielt", "When the radio plays", [](Config& c) -> std::string& { return c.powerSource; },
                   {"ignition", "engine", "always"}, {{"Zündung", "Ignition"}, {"Motor läuft", "Engine running"}, {"Immer", "Always"}}),
            Toggle("Automatisch starten", "Start automatically", "Beim Spielstart sofort spielen", "Play as soon as the game starts", [](Config& c) -> bool& { return c.autoPlay; }),
            Choice("Sprache des Menüs", "Menu language", "", "", [](Config& c) -> std::string& { return c.menuLanguage; },
                   {"de", "en"}, {{"Deutsch", "Deutsch"}, {"English", "English"}}),
            Heading("Band", "Band"),
            Choice("Band", "Band", "DAB+: weiter, ohne Rauschen, bricht bei schwachem Signal ab", "DAB+: further, no static, drops out on a weak signal",
                   [](Config& c) -> std::string& { return c.band; }, {"fm", "dab"}, {{"FM (UKW)", "FM"}, {"DAB+", "DAB+"}}),
            KeyRow("Taste: Band wechseln", "Key: change band", "", "", Action::Band),
            Choice("Senderwahl (FM)", "Tuning (FM)", "Frequenz: Weiter/Zur\u00fcck sucht den n\u00e4chsten Sender, Umschalt+Pfeile 0,1 MHz",
                   "Frequency: Next/Previous seeks, Shift+arrows 0.1 MHz", [](Config& c) -> std::string& { return c.tuning; },
                   {"list", "frequency"}, {{"Senderliste", "Station list"}, {"Frequenz (wie fr\u00fcher)", "Frequency (old style)"}}),
            Toggle("RDS-AF: Programm folgen", "RDS AF: follow the programme", "Aus = wie fr\u00fcher: die Frequenz bleibt, auch wenn der Sender weg ist",
                   "Off = old style: the frequency stays even when the station is gone", [](Config& c) -> bool& { return c.rdsAf; }),
            Heading("Stationstasten", "Station buttons"),
            Toggle("Stationstasten 1\u20139", "Station buttons 1\u20139", "Nummernblock: kurz dr\u00fccken = w\u00e4hlen, gedr\u00fcckt halten = speichern",
                   "Numpad: tap = select, hold = save", [](Config& c) -> bool& { return c.memoryButtons; }),
            Slider("Halten zum Speichern", "Hold to save", "Tasten \u00e4ndern: Seite Tasten", "Change the keys: page Keys",
                   [](Config& c) -> double& { return c.memoryHoldSeconds; }, 0.5, 3, 0.5, secs),
            Button("Stationstasten neu belegen", "Store station buttons again", "Mit den 9 am besten empfangenen Sendern hier (Travelstore)",
                   "With the 9 best received stations here (travel store)", "Belegen", "Store", SettingsMenu::AutoStoreButtons),
        }, false});
        p.push_back({"Empfang", "Reception", "Sender und Empfang", "Stations and reception", {
            Slider("Sendereichweite", "Broadcast range", "Grundreichweite einer Stadt", "Base range of a city", [](Config& c) -> double& { return c.radius; }, 2000, 20000, 500,
                   [](double v, bool en) { return Num(v / 1000, 1, en) + L" km"; }),
            Slider("Wechselschwelle", "Switch threshold", "Wie viel besser eine andere Region sein muss", "How much better another region must be",
                   [](Config& c) -> double& { return c.threshold; }, 0, 1, 0.05, [](double v, bool en) { return Num(v, 2, en); }),
            Slider("Auto-Lautst\u00e4rke", "Auto volume", "Lauter bei h\u00f6herem Tempo (ab 30 km/h, voll bei 90 km/h)",
                   "Louder at higher speed (from 30 km/h, full at 90 km/h)", [](Config& c) -> double& { return c.autoVolume; }, 0, 1, 0.1,
                   [](double v, bool en) { return v < 0.05 ? std::wstring(en ? L"Off" : L"Aus") : L"+" + Num(20 * std::log10(1 + v), 1, en) + L" dB"; }),
            Choice("Region wechseln", "Change region", "Wann das Radio einen Sender einer anderen Region w\u00e4hlt",
                   "When the radio picks a station of another region", [](Config& c) -> std::string& { return c.switchMode; },
                   {"lost", "better"}, {{"Erst wenn der Sender weg ist", "Only when the station is lost"}, {"Bei besserem Empfang", "On better reception"}}),
            Slider("DAB+-Reichweite", "DAB+ range", "Im Vergleich zu FM", "Compared to FM", [](Config& c) -> double& { return c.dabRange; }, 1, 3, 0.1,
                   [](double v, bool en) { return Num(v, 1, en) + L" \u00d7"; }),
            Toggle("Rauschen bei schwachem Empfang", "Static on weak reception", "", "", [](Config& c) -> bool& { return c.whitenoise; }),
            Toggle("Kein Empfang im Tunnel", "No reception in tunnels", "Tunnel-Daten: ETS2 Tunnel Radio (bohbotoly) \u2022 USB / CD spielt weiter",
                   "Tunnel data: ETS2 Tunnel Radio (bohbotoly) \u2022 USB / CD keeps playing", [](Config& c) -> bool& { return c.tunnelSignal; }),
            KeyRow("Taste: eigenen Tunnel markieren", "Key: mark an own tunnel", "Am Anfang und am Ende dr\u00fccken (Map-Mods)",
                   "Press at the entry and at the exit (map mods)", Action::TunnelMark),
            Toggle("Umschaltgeräusch", "Tuning sound", "Beim Senderwechsel", "When the station changes", [](Config& c) -> bool& { return c.transitionWhitenoise; }),
        }, false});
        D page = Toggle("Radio-Seite dauerhaft zeigen", "Keep the radio page on", "Statt der Navigation", "Instead of the navigation",
                        [](Config& c) -> bool& { static bool dummy; return dummy; });
        page.get = [](const Config& c) { return c.screenPage == "radio" ? 1.0 : 0.0; };
        page.set = [](Config& c, double v) { c.screenPage = v > 0.5 ? "radio" : "navi"; };
        p.push_back({"Navi-Bildschirm", "Navigation screen", "Anzeige im Cockpit", "Display in the cab", {
            Toggle("Anzeige auf dem Navi", "Show on the navigation screen", "Sender und Lautstärke im LKW", "Station and volume in the truck",
                   [](Config& c) -> bool& { return c.screen.enabled; }, true),
            Slider("Anzeigedauer", "Display time", "Wie lange Sender / Lautstärke zu sehen sind", "How long station / volume are shown",
                   [](Config& c) -> double& { return c.screen.durationSeconds; }, 1, 15, 0.5, secs),
            Choice("Design", "Design", "Aussehen der Anzeige im LKW", "Look of the display in the truck", [](Config& c) -> std::string& { return c.screen.design; },
                   {"modern", "ocean", "sport", "light", "retro"},
                   {{"Modern", "Modern"}, {"Ozean", "Ocean"}, {"Sport", "Sport"}, {"Hell", "Light"}, {"Retro (LCD)", "Retro (LCD)"}}),
            Toggle("Start-Logo beim Einschalten", "Start logo at ignition on", "Hersteller des LKWs auf Schwarz (Bild w\u00e4hlen: Seite LKW)",
                   "The truck's manufacturer on black (choose the picture: page Trucks)", [](Config& c) -> bool& { return c.screen.startLogo; }),
            Slider("Dauer Start-Logo", "Start logo time", "", "", [](Config& c) -> double& { return c.screen.startLogoSeconds; }, 1.5, 10, 0.5, secs),
            Slider("Gr\u00f6\u00dfe Start-Logo", "Start logo size", "H\u00f6he des Logos im Anzeigebereich (Vorschau: Seite LKW)",
                   "Height of the logo in the display area (preview: page Trucks)", [](Config& c) -> double& { return c.screen.startLogoScale; }, 0.2, 1, 0.05, pct),
            Slider("Gr\u00f6\u00dfe Schrift und Bilder", "Size of text and pictures", "Inhalt gr\u00f6\u00dfer oder kleiner (h\u00f6chstens so hoch wie der Bereich)",
                   "Content larger or smaller (at most as high as the area)", [](Config& c) -> double& { return c.screen.scale; }, 0.6, 1.6, 0.05, pct),
            Slider("Gr\u00f6\u00dfe Senderlogo / Cover", "Size of station logo / cover", "", "",
                   [](Config& c) -> double& { return c.screen.logoSize; }, 0.5, 1.3, 0.05, pct),
            page,
            KeyRow("Taste: Navi \u2194 Radio-Seite", "Key: navigation \u2194 radio page", "", "", Action::ScreenPage),
            Heading("Position auf dem Bildschirm", "Position on the screen"),
            [] {
                D d = Choice("Zeichenfl\u00e4che", "Drawing surface", "", "", [](Config& c) -> std::string& { static std::string s; return s; },
                             {"0", "1", "2", "3", "4"}, {{"Alle", "All"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}});
                d.get = [](const Config& c) { return (double)std::clamp(c.screen.target, 0, 4); };
                d.set = [](Config& c, double v) {
                    int t = std::clamp((int)std::lround(v), 0, 4);
                    if (t != c.screen.target) c.screen.flipY = t >= 2;   // surface 2 is drawn upside down, surface 1 not (can be changed below)
                    c.screen.target = t;
                };
                d.hintFn = [](bool en) {
                    int n = D3D11Overlay::ScreenTargetsSeen();
                    std::wstring s = n > 0 ? (en ? L"Found: " : L"Gefunden: ") + std::to_wstring(n) + (en ? L" with this size" : L" mit dieser Gr\u00f6\u00dfe")
                                           : std::wstring(en ? L"Shows up twice? Draw into one only" : L"Doppelt zu sehen? Nur in eine zeichnen");
                    return s + (en ? L" \u2022 per truck in the preset" : L" \u2022 je LKW im Preset");
                };
                return d;
            }(),
            Toggle("Testmodus", "Test mode", "Gitter + Rahmen der Anzeige im LKW (Men\u00fc am Titel verschiebbar)",
                   "Grid + frame of the display in the truck (drag the menu by its title)", [](Config& c) -> bool& { return c.screen.grid; }),
            Button("Automatisch erkennen (Beta)", "Detect automatically (beta)", "Im LKW sitzen, Navigation sichtbar", "Sit in the truck with the navigation visible",
                   "Erkennen", "Detect", SettingsMenu::DetectScreen),
            Slider("Bereich links (X)", "Area left (X)", "", "", [](Config& c) -> double& { return c.screen.areaX; }, 0, 0.95, 0.01, two),
            Slider("Bereich oben (Y)", "Area top (Y)", "", "", [](Config& c) -> double& { return c.screen.areaY; }, 0, 0.95, 0.01, two),
            Slider("Breite", "Width", "", "", [](Config& c) -> double& { return c.screen.areaW; }, 0.05, 1, 0.01, two),
            Slider("H\u00f6he", "Height", "", "", [](Config& c) -> double& { return c.screen.areaH; }, 0.05, 1, 0.01, two),
            Toggle("Umdrehen (oben/unten)", "Flip (up/down)", "", "", [](Config& c) -> bool& { return c.screen.flipY; }),
            Toggle("Spiegeln (links/rechts)", "Mirror (left/right)", "", "", [](Config& c) -> bool& { return c.screen.flipX; }),
        }, false});
        p.push_back({"LKW", "Trucks", "Radio und Navi-Presets je LKW", "Radio and screen presets per truck", {}, false});   // rows: BuildPresetDefs
        D pos = Choice("Position", "Position", "", "", [](Config& c) -> std::string& { return c.overlay.style == "radio" ? c.overlay.radio.position : c.overlay.position; },
                       {"top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right"},
                       {{"Oben links", "Top left"}, {"Oben Mitte", "Top centre"}, {"Oben rechts", "Top right"},
                        {"Unten links", "Bottom left"}, {"Unten Mitte", "Bottom centre"}, {"Unten rechts", "Bottom right"}});
        p.push_back({"Popup-Overlay", "Pop-up overlay", "Einblendung im Spielbild", "Pop-up in the game picture", {
            Choice("Modus", "Mode", "", "", [](Config& c) -> std::string& { return c.overlay.mode; }, {"off", "auto", "d3d11", "window"},
                   {{"Aus", "Off"}, {"Automatisch", "Automatic"}, {"DirectX 11", "DirectX 11"}, {"Fenster", "Window"}}, true),
            Choice("Stil", "Style", "", "", [](Config& c) -> std::string& { return c.overlay.style; }, {"panel", "radio"},
                   {{"Info-Panel", "Info panel"}, {"Autoradio-Display", "Car radio display"}}),
            pos,
            Slider("Größe", "Size", "", "", [](Config& c) -> double& { return c.overlay.style == "radio" ? c.overlay.radio.scale : c.overlay.scale; }, 0.5, 2, 0.1,
                   [](double v, bool en) { return Num(v, 1, en) + L" ×"; }),
            Slider("Anzeigedauer", "Display time", "", "", [](Config& c) -> double& { return c.overlay.durationSeconds; }, 2, 20, 1, secs),
            Toggle("Senderlogos", "Station logos", "", "", [](Config& c) -> bool& { return c.overlay.showLogos; }),
        }, false});
        p.push_back({"USB / CD", "USB / CD", "Eigene Musik", "Your own music", {
            Toggle("USB / CD-Modus", "USB / CD mode", "Musik aus dem Ordner usb (Taste Pos1)", "Music from the usb folder (Home key)", [](Config& c) -> bool& { return c.media.enabled; }),
            Toggle("Jeder Ordner ein eigener USB", "Every folder is its own USB", "Aus = der ganze Ordner ist ein USB", "Off = the whole folder is one USB",
                   [](Config& c) -> bool& { return c.media.folderPerDisc; }),
            Choice("Standard-Typ", "Default type", "F\u00fcr Ordner ohne eigene Einstellung", "For folders without their own setting",
                   [](Config& c) -> std::string& { return c.media.label; }, {"USB", "CD", "SD", "Kassette", "Bluetooth", "AUX"},
                   {{"USB", "USB"}, {"CD", "CD"}, {"SD-Karte", "SD card"}, {"Kassette", "Cassette"}, {"Bluetooth", "Bluetooth"}, {"AUX", "AUX"}}),
            Toggle("Zufallswiedergabe", "Shuffle", "", "", [](Config& c) -> bool& { return c.media.shuffle; }),
            Choice("Wiederholen", "Repeat", "", "", [](Config& c) -> std::string& { return c.media.repeat; }, {"all", "one", "off"},
                   {{"Alle Titel", "All tracks"}, {"Einen Titel", "One track"}, {"Aus", "Off"}}),
            Button("Musikordner", "Music folder", "Hier Musik hinzuf\u00fcgen (\u00f6ffnet den Explorer)", "Add music here (opens Explorer)",
                   "\u00d6ffnen", "Open", SettingsMenu::OpenMusicFolder),
        }, false});
        p.push_back({"Speicher", "Storage", "Logos und Cover", "Logos and covers", {
            [] {
                D d = Slider("Logo-Speicher h\u00f6chstens", "Logo storage at most", "\u00c4lteste Logos werden automatisch gel\u00f6scht", "Oldest logos are deleted automatically",
                             [](Config& c) -> double& { static double v; return v; }, 5, 500, 5, [](double v, bool) { return std::to_wstring((int)std::lround(v)) + L" MB"; });
                d.get = [](const Config& c) { return (double)c.overlay.logoCacheMB; };
                d.set = [](Config& c, double v) { c.overlay.logoCacheMB = std::clamp((int)(std::round(v / 5) * 5), 5, 500); };
                return d;
            }(),
            Toggle("Senderlogos laden", "Load station logos", "Aus = keine Downloads, keine Logos", "Off = no downloads, no logos", [](Config& c) -> bool& { return c.overlay.showLogos; }),
            Button("Belegt", "In use", "Logos und USB-Cover im Ordner logos", "Logos and USB covers in the logos folder", "Leeren", "Clear", SettingsMenu::ClearLogos),
            Button("Logo-Ordner", "Logo folder", "", "", "\u00d6ffnen", "Open", SettingsMenu::OpenLogoFolder),
        }, false});
        p.push_back({"Tasten", "Keys", "Tastatur und Controller", "Keyboard and controller", {}, true});
        return p;
    }();
    return pages;
}

const Action kRows[] = {Action::Next, Action::Previous, Action::VolumeUp, Action::VolumeDown, Action::Mute, Action::Stop,
                        Action::Source, Action::NextDisc, Action::PreviousDisc, Action::ScreenPage, Action::MakeFavourite,
                        Action::GoToFavourite, Action::ShowOverlay, Action::Menu, Action::Band,
                        Action::Memory1, Action::Memory2, Action::Memory3, Action::Memory4, Action::Memory5,
                        Action::Memory6, Action::Memory7, Action::Memory8, Action::Memory9, Action::TuneUp, Action::TuneDown, Action::MenuAlt, Action::TunnelMark};
constexpr int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));
static_assert(kRowCount == (int)Action::Count, "every action needs a row in the key menu");

const char* LabelDe(Action a) {
    switch (a) {
    case Action::Next: return "Nächster Sender / Titel";
    case Action::Previous: return "Vorheriger Sender / Titel";
    case Action::VolumeUp: return "Lauter";
    case Action::VolumeDown: return "Leiser";
    case Action::Mute: return "Stumm";
    case Action::Stop: return "Radio aus/an • USB Pause";
    case Action::Source: return "Quelle: Radio / USB";
    case Action::NextDisc: return "Nächster USB / CD";
    case Action::PreviousDisc: return "Vorheriger USB / CD";
    case Action::ScreenPage: return "Navi ↔ Radio-Seite";
    case Action::MakeFavourite: return "Als Favorit speichern";
    case Action::GoToFavourite: return "Zum Favoriten";
    case Action::ShowOverlay: return "Overlay einblenden";
    case Action::Menu: return "Dieses Menü";
    case Action::MenuAlt: return "Dieses Menü (zweite Taste)";
    case Action::TunnelMark: return "Eigenen Tunnel markieren";
    case Action::Band: return "Band: FM / DAB+";
    case Action::Memory1: return "Stationstaste 1"; case Action::Memory2: return "Stationstaste 2"; case Action::Memory3: return "Stationstaste 3";
    case Action::Memory4: return "Stationstaste 4"; case Action::Memory5: return "Stationstaste 5"; case Action::Memory6: return "Stationstaste 6";
    case Action::Memory7: return "Stationstaste 7"; case Action::Memory8: return "Stationstaste 8"; case Action::Memory9: return "Stationstaste 9";
    case Action::TuneUp: return "Frequenz + 0,1 MHz"; case Action::TuneDown: return "Frequenz \u2212 0,1 MHz";
    default: return "?";
    }
}
const char* LabelEn(Action a) {
    switch (a) {
    case Action::Next: return "Next station / track";
    case Action::Previous: return "Previous station / track";
    case Action::VolumeUp: return "Volume up";
    case Action::VolumeDown: return "Volume down";
    case Action::Mute: return "Mute";
    case Action::Stop: return "Radio off/on • USB pause";
    case Action::Source: return "Source: radio / USB";
    case Action::NextDisc: return "Next USB / CD";
    case Action::PreviousDisc: return "Previous USB / CD";
    case Action::ScreenPage: return "Navigation ↔ radio page";
    case Action::MakeFavourite: return "Save as favourite";
    case Action::GoToFavourite: return "Go to favourite";
    case Action::ShowOverlay: return "Show overlay";
    case Action::Menu: return "This menu";
    case Action::MenuAlt: return "This menu (second key)";
    case Action::TunnelMark: return "Mark an own tunnel";
    case Action::Band: return "Band: FM / DAB+";
    case Action::Memory1: return "Station button 1"; case Action::Memory2: return "Station button 2"; case Action::Memory3: return "Station button 3";
    case Action::Memory4: return "Station button 4"; case Action::Memory5: return "Station button 5"; case Action::Memory6: return "Station button 6";
    case Action::Memory7: return "Station button 7"; case Action::Memory8: return "Station button 8"; case Action::Memory9: return "Station button 9";
    case Action::TuneUp: return "Frequency + 0.1 MHz"; case Action::TuneDown: return "Frequency \u2212 0.1 MHz";
    default: return "?";
    }
}
} // namespace

std::wstring SettingsMenu::T(const char* de, const char* en) const { return Utf8ToWide(edit_.menuLanguage == "en" ? en : de); }

std::wstring SettingsMenu::KeyText(const KeyBinding& b) const {
    if (!b.vk) return L"–";
    std::wstring n;
    switch (b.vk) {
    case VK_LEFT: n = L"←"; break;
    case VK_RIGHT: n = L"→"; break;
    case VK_UP: n = L"↑"; break;
    case VK_DOWN: n = L"↓"; break;
    case VK_HOME: n = T("Pos1", "Home"); break;
    case VK_END: n = T("Ende", "End"); break;
    case VK_INSERT: n = T("Einfg", "Insert"); break;
    case VK_DELETE: n = T("Entf", "Delete"); break;
    case VK_PRIOR: n = T("Bild ↑", "Page Up"); break;
    case VK_NEXT: n = T("Bild ↓", "Page Down"); break;
    case VK_PAUSE: n = L"Pause"; break;
    case VK_SPACE: n = T("Leertaste", "Space"); break;
    case VK_RETURN: n = L"Enter"; break;
    case VK_BACK: n = T("Rücktaste", "Backspace"); break;
    case VK_TAB: n = L"Tab"; break;
    default: {
        if (b.vk >= VK_F1 && b.vk <= VK_F24) { n = L"F" + std::to_wstring(b.vk - VK_F1 + 1); break; }
        UINT sc = MapVirtualKeyW(b.vk, MAPVK_VK_TO_VSC);
        wchar_t buf[64] = {};
        if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, 64) > 0) n = buf;   // localised name, e.g. "Ü", "Num 0"
        else n = Utf8ToWide(KeyBindingName(KeyBinding{b.vk}));
    }
    }
    std::wstring m;
    if (b.ctrl) m += T("Strg + ", "Ctrl + ");
    if (b.shift) m += T("Umschalt + ", "Shift + ");
    if (b.alt) m += L"Alt + ";
    return m + n;
}

// ------------------------------------------------------------------------------------------------
bool SettingsMenu::KeysPage() const { return Pages()[page_].keys; }

// Rows of the current page: the static settings, plus one media type row per music folder on the USB / CD page
bool SettingsMenu::PresetsPage() const { return std::string(Pages()[page_].de) == "LKW"; }

// Presets page: current truck, active preset (choice), automatic change by truck id, new / link / delete
void SettingsMenu::BuildPresetDefs() {
    std::wstring truck = truckName_.empty() ? Utf8ToWide(truckId_) : Utf8ToWide(truckName_);
    const TruckPreset* own = PresetForTruck(edit_, truckId_);
    D h = Button("", "", "", "", "Neues Preset", "New preset", PresetNew);
    h.wlabel = truckId_.empty() ? T("Kein LKW erkannt", "No truck detected") : T("Dieser LKW: ", "This truck: ") + truck;
    h.whint = truckId_.empty() ? T("Erst einsteigen \u2013 \u201eNeues Preset\u201c speichert die aktuellen Navi-Einstellungen",
                                   "Get into a truck first - \"New preset\" saves the current screen settings")
            : own ? T("Preset: ", "Preset: ") + Utf8ToWide(own->name) + L"  (" + Utf8ToWide(truckId_) + L")"
                  : T("Noch kein Preset  (", "No preset yet  (") + Utf8ToWide(truckId_) + L")";
    defs_.push_back(h);
    defs_.push_back(Toggle("Radio-Speicher je LKW", "Radio memory per truck",
                           "Sender, Favoriten, Lautst\u00e4rke, Quelle und USB-Titel merkt sich jeder LKW",
                           "Every truck keeps its own stations, favourites, volume, source and USB track",
                           [](Config& cfg) -> bool& { return cfg.truckMemory; }));
    // start logo of this truck (stored in the active preset)
    defs_.push_back(Heading("Start-Logo", "Start logo"));
    {
        std::vector<std::string> vals = {"", "text"};
        for (auto& f : logos_) vals.push_back(f);
        D l; l.kind = MenuItemKind::Choice; l.de = "Start-Logo"; l.en = "Start logo";
        l.whint = logos_.empty() ? T("Kein Bild f\u00fcr diesen LKW im Ordner truck-logos \u2013 der Name wird gezeigt",
                                     "No picture for this truck in the folder truck-logos - the name is shown")
                                 : std::to_wstring(logos_.size()) + T(" Bild(er) \u2022 im aktiven Preset gespeichert", " picture(s) \u2022 stored in the active preset");
        l.mx = (double)vals.size() - 1;
        l.names.push_back(T("Automatisch", "Automatic"));
        l.names.push_back(T("Nur Name", "Name only"));
        for (auto& f : logos_) { std::wstring w = Utf8ToWide(f); size_t d = w.rfind(L'.'); l.names.push_back(d == std::wstring::npos ? w : w.substr(0, d)); }
        l.get = [vals](const Config& cfg) {
            for (size_t i = 0; i < vals.size(); ++i) if (_stricmp(vals[i].c_str(), cfg.screen.startLogoFile.c_str()) == 0) return (double)i;
            return 0.0;
        };
        l.set = [vals](Config& cfg, double v) { cfg.screen.startLogoFile = vals[std::clamp((int)std::lround(v), 0, (int)vals.size() - 1)]; };
        defs_.push_back(l);
    }
    defs_.push_back(Button("Vorschau auf dem Navi", "Preview on the screen", "", "", "Zeigen", "Show", PreviewStartLogo));
    defs_.push_back(Button("Logo-Ordner", "Logo folder", "\u00d6ffnet den Ordner des Herstellers \u2013 Bilder einfach hineinlegen",
                           "Opens the folder of this brand - just put pictures in",
                           "\u00d6ffnen", "Open", OpenTruckLogoFolder));
    defs_.push_back(Heading("Navi-Presets", "Screen presets"));
    // active preset
    std::vector<std::string> names; names.push_back("");
    const auto all = AllPresets(edit_);
    for (auto* p : all) names.push_back(p->name);
    D c; c.kind = MenuItemKind::Choice; c.de = "Aktives Preset"; c.en = "Active preset";
    c.hde = "Wird geladen und merkt sich \u00c4nderungen der Navi-Seite"; c.hen = "Is loaded and keeps the changes of the screen page";
    c.mx = (double)names.size() - 1;
    for (auto& n : names) c.names.push_back(n.empty() ? T("(keins)", "(none)") : Utf8ToWide(n));
    c.get = [names](const Config& cfg) {
        for (size_t i = 1; i < names.size(); ++i) if (names[i] == cfg.activePreset) return (double)i;
        return 0.0;
    };
    c.set = [names](Config& cfg, double v) {
        int i = std::clamp((int)std::lround(v), 0, (int)names.size() - 1);
        cfg.activePreset = names[i];
        if (const TruckPreset* p = LookupPreset(cfg, cfg.activePreset)) ApplyPreset(cfg.screen, *p);
    };
    defs_.push_back(c);
    defs_.push_back(Toggle("Automatisch je LKW wechseln", "Change automatically per truck",
                           "Erkennt den LKW \u00fcber die LKW-ID der Telemetrie",
                           "Detects the truck by the truck id of the telemetry", [](Config& cfg) -> bool& { return cfg.presetAuto; }));
    defs_.push_back(Heading("Aktives Preset", "Active preset"));
    defs_.push_back(Button("Mit diesem LKW verkn\u00fcpfen", "Link to this truck", "Das aktive Preset l\u00e4dt dann automatisch in diesem LKW",
                           "The active preset then loads automatically in this truck", "Verkn\u00fcpfen", "Link", PresetLink));
    defs_.push_back(Button("Verkn\u00fcpfung l\u00f6sen", "Unlink", "Dieser LKW l\u00e4dt das aktive Preset nicht mehr automatisch",
                           "This truck no longer loads the active preset automatically", "L\u00f6sen", "Unlink", PresetUnlink));
    D del = Button("Aktives Preset l\u00f6schen", "Delete active preset", "Die Navi-Einstellungen bleiben", "The screen settings stay",
                   "L\u00f6schen", "Delete", PresetDelete);
    if (confirmDelete_) { del.btnDe = "Wirklich?"; del.btnEn = "Sure?"; }
    defs_.push_back(del);
    if (all.empty()) return;
    defs_.push_back(Heading("Gespeicherte Presets", "Saved presets"));
    for (size_t i = 0; i < all.size(); ++i) {
        const TruckPreset& p = *all[i];
        D r = Button("", "", "", "", "Laden", "Load", PresetLoad);
        r.index = (int)i;
        r.wlabel = (p.name == edit_.activePreset ? L"\u25b6 " : L"") + Utf8ToWide(p.name);
        if (p.trucks.empty()) r.whint = T("Kein LKW verkn\u00fcpft", "No truck linked");
        else for (auto& id : p.trucks) {
            if (!r.whint.empty()) r.whint += L", ";
            r.whint += Utf8ToWide(id == truckId_ && !truckName_.empty() ? truckName_ : id);
        }
        bool hasStd = false;
        for (auto& b : BuiltinPresets()) if (b.name == p.name) hasStd = true;
        if (hasStd) r.whint += IsStandardPreset(edit_, p.name) ? T("  \u2022  Standard", "  \u2022  standard") : T("  \u2022  angepasst", "  \u2022  customised");
        defs_.push_back(r);
    }
}

void SettingsMenu::PresetCommand(int cmd, int index) {
    auto unlinkEverywhere = [&](const std::string& id) {
        for (auto& p : edit_.presets) p.trucks.erase(std::remove(p.trucks.begin(), p.trucks.end(), id), p.trucks.end());
    };
    const std::string active = edit_.activePreset;
    const TruckPreset* act = LookupPreset(edit_, active);
    auto quoted = [&](const std::string& n) { return T("\u201e", "\"") + Utf8ToWide(n) + T("\u201c", "\""); };
    if (cmd != PresetDelete) confirmDelete_ = false;
    switch (cmd) {
    case PresetLoad: {
        auto all = AllPresets(edit_);
        if (index < 0 || index >= (int)all.size()) break;
        edit_.activePreset = all[index]->name;
        ApplyPreset(edit_.screen, *all[index]);
        status_ = T("Preset ", "Preset ") + quoted(edit_.activePreset) + T(" geladen", " loaded");
        break; }
    case PresetNew: {
        std::string base = !truckName_.empty() ? truckName_ : !truckId_.empty() ? truckId_ : std::string("Preset");
        std::string name = base;
        for (int n = 2; LookupPreset(edit_, name); ++n) name = base + " (" + std::to_string(n) + ")";
        TruckPreset np = PresetFromScreen(name, edit_.screen);
        if (!truckId_.empty()) { unlinkEverywhere(truckId_); np.trucks.push_back(truckId_); }
        edit_.presets.push_back(np);
        edit_.activePreset = name;
        status_ = T("Preset ", "Preset ") + quoted(name) + T(" angelegt", " created");
        break; }
    case PresetLink: {
        if (truckId_.empty()) { status_ = T("Kein LKW erkannt \u2013 erst einsteigen", "No truck detected - get into a truck first"); break; }
        if (!act) { status_ = T("Erst ein Preset w\u00e4hlen oder anlegen", "Choose or create a preset first"); break; }
        unlinkEverywhere(truckId_);
        TruckPreset* own = OwnPreset(edit_, active);   // a standard preset becomes an own copy
        if (std::find(own->trucks.begin(), own->trucks.end(), truckId_) == own->trucks.end()) own->trucks.push_back(truckId_);
        status_ = T("Verkn\u00fcpft mit ", "Linked to ") + Utf8ToWide(truckName_.empty() ? truckId_ : truckName_);
        break; }
    case PresetUnlink: {
        if (!act || truckId_.empty() || std::find(act->trucks.begin(), act->trucks.end(), truckId_) == act->trucks.end()) {
            status_ = T("Dieser LKW ist nicht mit dem aktiven Preset verkn\u00fcpft", "This truck is not linked to the active preset"); break; }
        OwnPreset(edit_, active);   // a standard preset is copied so the link can be removed
        unlinkEverywhere(truckId_);
        status_ = T("Verkn\u00fcpfung gel\u00f6st", "Link removed");
        break; }
    case PresetDelete: {
        if (!act) { status_ = T("Kein Preset aktiv", "No preset active"); confirmDelete_ = false; break; }
        if (IsStandardPreset(edit_, active)) {
            status_ = T("Standard-Presets lassen sich nicht l\u00f6schen \u2013 eigenes Preset anlegen oder Verkn\u00fcpfung l\u00f6sen",
                        "Standard presets cannot be deleted - create an own preset or unlink the truck");
            confirmDelete_ = false; break;
        }
        if (!confirmDelete_) { confirmDelete_ = true; status_ = T("Zum L\u00f6schen nochmal klicken", "Click again to delete"); break; }
        confirmDelete_ = false;
        TruckPreset* own = FindPreset(edit_, active);
        edit_.presets.erase(edit_.presets.begin() + (own - edit_.presets.data()));
        if (const TruckPreset* base = LookupPreset(edit_, active)) {   // was a changed standard preset: back to the standard values
            ApplyPreset(edit_.screen, *base);
            status_ = T("Preset ", "Preset ") + quoted(active) + T(" auf Standard zur\u00fcckgesetzt", " reset to the standard values");
        } else {
            edit_.activePreset.clear();
            status_ = T("Preset ", "Preset ") + quoted(active) + T(" gel\u00f6scht (Navi-Werte bleiben)", " deleted (screen values stay)");
        }
        break; }
    default: break;
    }
    int keep = sel_;
    Edited_();
    BuildDefs();
    sel_ = std::clamp(keep, 0, std::max(0, RowCount() - 1));
    EnsureVisible();
}

void SettingsMenu::SetLogos(const std::vector<std::wstring>& files) {
    std::vector<std::string> l;
    for (auto& f : files) l.push_back(WideToUtf8(f));
    if (l == logos_) return;
    logos_ = l;
    if (PresetsPage()) { int keep = sel_; BuildDefs(); sel_ = std::clamp(keep, 0, std::max(0, RowCount() - 1)); }
    dirty_ = true;
}

void SettingsMenu::SetTruck(const std::string& id, const std::string& name) {
    if (id == truckId_ && name == truckName_) return;
    truckId_ = id; truckName_ = name;
    if (edit_.presetAuto && AutoSelectPreset(edit_, id)) Edited_();
    if (PresetsPage()) BuildDefs();
    dirty_ = true;
}

void SettingsMenu::BuildDefs() {
    defs_ = Pages()[page_].defs;
    if (PresetsPage()) { BuildPresetDefs(); return; }
    if (std::string(Pages()[page_].de) != "USB / CD" || folders_.empty()) return;
    D h = Heading("Musikordner und ihr Typ", "Music folders and their type");
    defs_.push_back(h);
    std::vector<const char*> ids; std::vector<std::pair<const char*, const char*>> labels;
    ids.push_back("");   labels.push_back({"Standard", "Default"});
    for (auto& t : MediaTypes()) { ids.push_back(t.id); labels.push_back({t.de, t.en}); }
    for (const Folder& f : folders_) {
        std::string key = WideToUtf8(f.name);
        D d = Choice("", "", "", "", [](Config& c) -> std::string& { static std::string s; return s; }, ids, labels);
        d.get = [key, ids](const Config& c) {
            auto it = c.media.types.find(key);
            std::string id = it == c.media.types.end() ? "" : MediaTypeId(it->second);
            for (size_t i = 0; i < ids.size(); ++i) if (id == ids[i]) return (double)i;
            return 0.0;
        };
        d.set = [key, ids](Config& c, double v) {
            int i = std::clamp((int)std::lround(v), 0, (int)ids.size() - 1);
            if (i == 0) c.media.types.erase(key); else c.media.types[key] = ids[i];
        };
        d.restart = false;
        d.folder = f.name.empty() ? L"" : f.name;
        d.folderTracks = f.tracks;
        defs_.push_back(d);
    }
}
void SettingsMenu::SetFolders(const std::vector<Folder>& f) { folders_ = f; BuildDefs(); dirty_ = true; }
void SettingsMenu::SetScreenArea(const float r[4]) {
    auto in = [](double v) { return std::round(v * 100.0) / 100.0; };
    double x0 = std::ceil(r[0] * 100.0 - 1e-6) / 100.0, y0 = std::ceil(r[1] * 100.0 - 1e-6) / 100.0;     // round inwards
    double x1 = std::floor((r[0] + r[2]) * 100.0 + 1e-6) / 100.0, y1 = std::floor((r[1] + r[3]) * 100.0 + 1e-6) / 100.0;
    edit_.screen.areaX = in(x0); edit_.screen.areaY = in(y0);
    edit_.screen.areaW = in(std::max(0.05, x1 - x0)); edit_.screen.areaH = in(std::max(0.05, y1 - y0));
    edit_.screen.grid = true;   // show the frame right away so the result can be checked
    wchar_t b[160];
    swprintf(b, 160, edit_.menuLanguage == "en" ? L"Detected: X %.2f  Y %.2f  W %.2f  H %.2f - check the frame in the truck, adjust if needed"
                                                : L"Erkannt: X %.2f  Y %.2f  B %.2f  H %.2f \u2013 Rahmen im LKW pr\u00fcfen, bei Bedarf nachstellen",
             edit_.screen.areaX, edit_.screen.areaY, edit_.screen.areaW, edit_.screen.areaH);
    status_ = b;
    Edited_();
}
void SettingsMenu::SetCacheInfo(uint64_t bytes, int files) { cacheBytes_ = bytes; cacheFiles_ = files; dirty_ = true; }
int SettingsMenu::VisibleRows() const { return MenuVisibleRows(View(), renderH_); }
void SettingsMenu::EnsureVisible() {
    int vis = VisibleRows();
    if (sel_ < scroll_) scroll_ = sel_;
    else if (sel_ >= scroll_ + vis) scroll_ = sel_ - vis + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, RowCount() - vis));
}
bool SettingsMenu::IsKeyRow(int row) const {
    if (row < 0 || row >= RowCount()) return false;
    return KeysPage() || defs_[row].kind == MenuItemKind::Key;
}
Action SettingsMenu::RowAction(int row) const { return KeysPage() ? kRows[row] : defs_[row].action; }
int SettingsMenu::RowCount() const { return KeysPage() ? kRowCount : (int)defs_.size(); }

void SettingsMenu::Open(const Config& cfg) {
    if (open_) return;
    edit_ = cfg; saved_ = cfg; savedJson_ = cfg.MenuJson();
    page_ = 0; sel_ = 0; scroll_ = 0; hover_ = -1; hoverCtl_ = -1; capture_ = 0; drag_ = -1; cmd_ = None;
    changed_ = false; saveReq_ = false; dirty_ = true; status_.clear(); confirmDelete_ = false;
    BuildDefs();
    renderH_ = D3D11Overlay::ScreenHeight() >= 200 ? D3D11Overlay::ScreenHeight() : 1080;
    float cx = std::max(1, D3D11Overlay::ScreenWidth()) / 2.f, cy = renderH_ / 2.f;
    InputBlock::Install();                          // the game reads the mouse itself (raw input / DirectInput)
    InputBlock::AttachWindow(GetForegroundWindow());
    InputBlock::SetMouseBlocked(true);
    bool hooks = MenuInput::Start(cx, cy);
    mouse_ = hooks && MenuInput::MouseActive();
    D3D11Overlay::SetCursorImage(RenderMenuCursor(renderH_));
    D3D11Overlay::SetCursorVisible(mouse_);
    if (!hooks) status_ = T("Hinweis: Tasten erreichen während des Menüs auch das Spiel", "Note: keys also reach the game while the menu is open");
    open_ = true;
    Log::Write("Settings menu opened (%s)", mouse_ ? "keyboard + mouse" : "keyboard");
}

void SettingsMenu::Close() {
    if (!open_) return;
    MenuInput::Stop();
    InputBlock::SetMouseBlocked(false);
    D3D11Overlay::SetCursorVisible(false);
    open_ = false; capture_ = 0; drag_ = -1;
}

void SettingsMenu::MarkSaved() {
    saved_ = edit_; savedJson_ = edit_.MenuJson();
    status_ = T("Gespeichert", "Saved");
    dirty_ = true;
}

void SettingsMenu::Edited_() {
    StoreActivePreset(edit_);   // the active preset follows the screen settings
    if (PresetsPage()) { int keep = sel_; BuildDefs(); sel_ = std::clamp(keep, 0, std::max(0, RowCount() - 1)); }
    changed_ = true; dirty_ = true;
}

void SettingsMenu::Adjust(int row, int dir) {
    if (IsKeyRow(row) || row < 0 || row >= RowCount()) return;
    const D& d = defs_[row];
    if (d.kind == MenuItemKind::Button || d.kind == MenuItemKind::Heading) return;
    double v = d.get(edit_);
    switch (d.kind) {
    case MenuItemKind::Toggle: d.set(edit_, v > 0.5 ? 0 : 1); break;
    case MenuItemKind::Slider: d.set(edit_, std::clamp(v + dir * d.step, d.mn, d.mx)); break;
    case MenuItemKind::Choice: { int n = (int)d.mx + 1; d.set(edit_, (double)(((int)std::lround(v) + dir + n) % n)); break; }
    default: break;
    }
    status_.clear(); confirmDelete_ = false;
    Edited_();
}

void SettingsMenu::SetSlider(int row, float frac) {
    const D& d = defs_[row];
    double before = d.get(edit_);
    d.set(edit_, d.mn + frac * (d.mx - d.mn));
    if (d.get(edit_) != before) { status_.clear(); Edited_(); }
}

void SettingsMenu::Activate(int row) {
    if (row < 0 || row >= RowCount()) return;
    if (IsKeyRow(row)) {
        capture_ = 1;
        status_ = T("Neue Taste oder Controller-Knopf drücken …  (Esc = abbrechen)", "Press the new key or controller button …  (Esc = cancel)");
        dirty_ = true;
        return;
    }
    const D& d = defs_[row];
    if (d.kind == MenuItemKind::Toggle || d.kind == MenuItemKind::Choice) Adjust(row, 1);
    else if (d.kind == MenuItemKind::Button) {
        if (d.command >= PresetNew) PresetCommand(d.command, d.index);
        else { cmd_ = d.command; dirty_ = true; }
    }
}

void SettingsMenu::BindKey(int row, const KeyBinding& b) {
    Action a = RowAction(row);
    capture_ = 0; dirty_ = true;
    for (int i = 0; i < (int)Action::Count; ++i)
        if (i != (int)a && edit_.keys[i] == b && (Action)i == Action::Menu) {   // never take the menu's own key away
            status_ = T("Diese Taste öffnet das Menü – bitte eine andere wählen", "This key opens the menu - please choose another one");
            return;
        }
    std::wstring moved;
    for (int i = 0; i < (int)Action::Count; ++i)
        if (i != (int)a && edit_.keys[i] == b) { edit_.keys[i] = KeyBinding{}; moved = Utf8ToWide(edit_.menuLanguage == "en" ? LabelEn((Action)i) : LabelDe((Action)i)); }
    edit_.keys[(int)a] = b;
    status_ = moved.empty() ? L"" : T("Taste war vorher bei „", "Key was used by \"") + moved + T("“ – dort entfernt", "\" - removed there");
    Edited_();
}

void SettingsMenu::BindButton(int row, int button) {
    Action a = RowAction(row);
    capture_ = 0; dirty_ = true;
    std::wstring moved;
    for (int i = 0; i < (int)Action::Count; ++i)
        if (i != (int)a && edit_.buttons[i] == button) { edit_.buttons[i] = -1; moved = Utf8ToWide(edit_.menuLanguage == "en" ? LabelEn((Action)i) : LabelDe((Action)i)); }
    edit_.buttons[(int)a] = button;
    status_ = moved.empty() ? L"" : T("Knopf war vorher bei „", "Button was used by \"") + moved + T("“ – dort entfernt", "\" - removed there");
    Edited_();
}

void SettingsMenu::ClearBinding(int row) {
    Action a = RowAction(row);
    if (a == Action::Menu) { status_ = T("Das Menü braucht eine Taste", "The menu needs a key"); dirty_ = true; return; }
    edit_.keys[(int)a] = KeyBinding{}; edit_.buttons[(int)a] = -1;
    status_ = T("Belegung entfernt", "Binding removed");
    Edited_();
}

void SettingsMenu::Defaults() {   // default values of the current page
    Config def;
    if (KeysPage()) for (int i = 0; i < (int)Action::Count; ++i) { edit_.keys[i] = def.keys[i]; edit_.buttons[i] = def.buttons[i]; }
    else for (const D& d : defs_) {
        if (d.kind == MenuItemKind::Key) { edit_.keys[(int)d.action] = def.keys[(int)d.action]; edit_.buttons[(int)d.action] = def.buttons[(int)d.action]; }
        else d.set(edit_, d.get(def));
    }
    status_ = T("Standardwerte dieser Seite gesetzt", "Defaults of this page restored");
    Edited_();
}

void SettingsMenu::Revert() {
    CopyMenuSettings(edit_, saved_);
    status_ = T("Auf den gespeicherten Stand zurückgesetzt", "Reverted to the saved settings");
    Edited_();
}

void SettingsMenu::UpdateHover() {
    if (!mouse_) return;
    float cx, cy, ix, iy, iw, ih;
    D3D11Overlay::CursorPos(cx, cy);
    if (!D3D11Overlay::ImageRect(ix, iy, iw, ih)) return;
    MenuView v = View();
    MenuHit h = HitTestMenu(v, renderH_, cx - ix, cy - iy);
    int hov = (h.kind >= MenuHit::Row && h.kind <= MenuHit::ItemButton) ? h.index : -1;
    int ctl = h.kind == MenuHit::Page ? 100 + h.index : (h.kind >= MenuHit::ItemButton && h.kind <= MenuHit::Close) ? (int)h.kind : -1;
    if (drag_ >= 0 && !IsKeyRow(drag_)) SetSlider(drag_, MenuSliderFrac(v, renderH_, drag_, cx - ix));   // follows the mouse outside the track too
    if (hov != hover_ || ctl != hoverCtl_) { hover_ = hov; hoverCtl_ = ctl; dirty_ = true; }
}

SettingsMenu::Result SettingsMenu::Tick(InputManager& input) {
    if (!open_) return Result::Closed;
    auto close = [&]() {
        if (edit_.MenuJson() != savedJson_) saveReq_ = true;
        Close();
        Log::Write("Settings menu closed");
        return Result::Closed;
    };
    MenuInput::Event e;
    while (MenuInput::Pop(e)) {
        dirty_ = true;
        if (e.type == MenuInput::Event::Key) {
            if (capture_) {
                if (e.vk == VK_ESCAPE && !e.ctrl && !e.shift && !e.alt) { capture_ = 0; status_ = T("Abgebrochen", "Cancelled"); }
                else if (capture_ == 1) BindKey(sel_, KeyBinding{e.vk, e.ctrl, e.shift, e.alt});
                continue;
            }
            int n = RowCount();
            auto heading = [&](int r) { return !KeysPage() && r >= 0 && r < n && defs_[r].kind == MenuItemKind::Heading; };
            switch (e.vk) {
            case VK_UP: do sel_ = (sel_ + n - 1) % n; while (heading(sel_)); EnsureVisible(); break;
            case VK_DOWN: do sel_ = (sel_ + 1) % n; while (heading(sel_)); EnsureVisible(); break;
            case VK_LEFT: Adjust(sel_, -1); break;
            case VK_RIGHT: Adjust(sel_, 1); break;
            case VK_RETURN: case VK_SPACE: Activate(sel_); break;
            case VK_TAB: case VK_PRIOR: case VK_NEXT: {
                int pc = (int)Pages().size();
                bool back = e.vk == VK_PRIOR || (e.vk == VK_TAB && e.shift);
                page_ = (page_ + (back ? pc - 1 : 1)) % pc; sel_ = 0; scroll_ = 0; status_.clear(); BuildDefs();
                break; }
            case VK_DELETE: case VK_BACK: if (IsKeyRow(sel_)) ClearBinding(sel_); break;
            case VK_ESCAPE: return close();
            default: break;
            }
            continue;
        }
        float cx, cy, ix, iy, iw, ih;
        D3D11Overlay::CursorPos(cx, cy);
        if (!D3D11Overlay::ImageRect(ix, iy, iw, ih)) continue;
        MenuHit h = HitTestMenu(View(), renderH_, cx - ix, cy - iy);
        if (e.type == MenuInput::Event::MouseUp) { drag_ = -1; moving_ = false; continue; }
        if (e.type == MenuInput::Event::Wheel) {
            int vis = VisibleRows();
            if (RowCount() > vis) scroll_ = std::clamp(scroll_ - e.wheel * 2, 0, RowCount() - vis);   // long page: the wheel scrolls
            else if (h.index >= 0 && h.kind != MenuHit::Page && !IsKeyRow(h.index) &&
                     (defs_[h.index].kind == MenuItemKind::Slider || defs_[h.index].kind == MenuItemKind::Choice))
                Adjust(h.index, e.wheel);   // wheel over a slider or choice changes it
            continue;
        }
        if (e.button == 1) {   // right click: clear a key or button
            if (h.index >= 0 && IsKeyRow(h.index) && (h.kind == MenuHit::KeyCell || h.kind == MenuHit::ButtonCell || h.kind == MenuHit::Row)) { sel_ = h.index; ClearBinding(h.index); }
            continue;
        }
        if (e.button != 0) continue;
        if (capture_ && h.kind != MenuHit::ButtonCell) { capture_ = 0; status_.clear(); }   // a click ends waiting for a key
        switch (h.kind) {
        case MenuHit::Close: return close();
        case MenuHit::Header: moving_ = true; grabX_ = cx - ix; grabY_ = cy - iy; break;   // drag the menu by its title
        case MenuHit::Save: if (edit_.MenuJson() != savedJson_) saveReq_ = true; else status_ = T("Nichts zu speichern", "Nothing to save"); break;
        case MenuHit::Reset: Revert(); break;
        case MenuHit::Default: Defaults(); break;
        case MenuHit::Page: page_ = h.index; sel_ = 0; scroll_ = 0; status_.clear(); BuildDefs(); break;
        case MenuHit::ItemButton: sel_ = h.index; Activate(h.index); break;
        case MenuHit::Toggle: sel_ = h.index; Adjust(h.index, 1); break;
        case MenuHit::Slider: sel_ = h.index; drag_ = h.index; SetSlider(h.index, h.frac); break;
        case MenuHit::ChoicePrev: sel_ = h.index; Adjust(h.index, -1); break;
        case MenuHit::ChoiceNext: sel_ = h.index; Adjust(h.index, 1); break;
        case MenuHit::KeyCell: sel_ = h.index; Activate(h.index); break;
        case MenuHit::ButtonCell:
            sel_ = h.index; capture_ = 2; input.PollNewButton();
            status_ = T("Controller-Knopf drücken …  (Esc = abbrechen)", "Press a controller button …  (Esc = cancel)");
            break;
        case MenuHit::Row:
            sel_ = h.index;
            if (!IsKeyRow(h.index) && defs_[h.index].kind == MenuItemKind::Toggle) Adjust(h.index, 1);
            break;
        default: break;
        }
    }
    if (capture_) { int b = input.PollNewButton(); if (b >= 0) BindButton(sel_, b); }
    if (moving_) {
        float cx, cy, ix, iy, iw, ih;
        D3D11Overlay::CursorPos(cx, cy);
        if (D3D11Overlay::ImageRect(ix, iy, iw, ih)) {
            float sw = (float)std::max(1, D3D11Overlay::ScreenWidth()), sh = (float)std::max(1, D3D11Overlay::ScreenHeight());
            float nx = std::clamp(cx - grabX_, 0.f, std::max(0.f, sw - iw)), ny = std::clamp(cy - grabY_, 0.f, std::max(0.f, sh - ih));
            if (nx != posX_ || ny != posY_) { posX_ = nx; posY_ = ny; moved_ = true; }
        }
    }
    UpdateHover();
    return Result::Open;
}

MenuView SettingsMenu::View() const {
    const bool en = edit_.menuLanguage == "en";
    const Page& pg = Pages()[page_];
    MenuView v;
    v.title = L"Realistic Radio";
    v.subtitle = T("Einstellungen", "Settings") + L"  •  by ScufyKing  •  v" REALISTIC_RADIO_VERSION_W;
    for (const Page& p : Pages()) v.pages.push_back(Utf8ToWide(en ? p.en : p.de));
    v.page = page_;
    v.section = Utf8ToWide(en ? pg.sen : pg.sde);
    if (std::string(pg.de) == "Navi-Bildschirm" && !edit_.activePreset.empty())
        v.section += T("  \u2022  Preset: ", "  \u2022  preset: ") + Utf8ToWide(edit_.activePreset);
    v.keyHeader = T("Taste", "Key"); v.buttonHeader = L"Controller";
    v.restartText = T("NEUSTART", "RESTART");
    if (pg.keys) {
        for (int r = 0; r < kRowCount; ++r) {
            Action a = kRows[r];
            MenuItem it; it.kind = MenuItemKind::Key;
            it.label = Utf8ToWide(en ? LabelEn(a) : LabelDe(a));
            it.key = KeyText(edit_.keys[(int)a]);
            int b = edit_.buttons[(int)a];
            it.button = b >= 0 ? T("Knopf ", "Button ") + std::to_wstring(b + 1) : L"–";
            v.items.push_back(it);
        }
    } else {
        for (const D& d : defs_) {
            MenuItem it; it.kind = d.kind; it.restart = d.restart;
            it.label = Utf8ToWide(en ? d.en : d.de); it.hint = Utf8ToWide(en ? d.hen : d.hde);
            if (!d.wlabel.empty()) it.label = d.wlabel;
            if (!d.whint.empty()) it.hint = d.whint;
            if (d.hintFn) it.hint = d.hintFn(en);
            if (d.folderTracks >= 0) {   // media type of a music folder
                it.label = d.folder.empty() ? T("(Dateien direkt im Ordner usb)", "(files directly in the usb folder)") : d.folder;
                it.hint = std::to_wstring(d.folderTracks) + T(" Titel", d.folderTracks == 1 ? " track" : " tracks");
            }
            if (d.kind == MenuItemKind::Heading) { v.items.push_back(it); continue; }
            if (d.kind == MenuItemKind::Button) {
                it.button = Utf8ToWide(en ? d.btnEn : d.btnDe);
                if (d.command == ClearLogos)
                    it.value = Num(cacheBytes_ / 1048576.0, 1, en) + L" MB  \u2022  " + std::to_wstring(cacheFiles_) + T(" Dateien", " files");
                v.items.push_back(it);
                continue;
            }
            if (d.kind == MenuItemKind::Key) {
                it.key = KeyText(edit_.keys[(int)d.action]);
                int b = edit_.buttons[(int)d.action];
                it.button = b >= 0 ? T("Knopf ", "Button ") + std::to_wstring(b + 1) : L"\u2013";
                v.items.push_back(it);
                continue;
            }
            double val = d.get(edit_);
            if (d.kind == MenuItemKind::Toggle) it.on = val > 0.5;
            else if (d.kind == MenuItemKind::Slider) { it.frac = (float)((val - d.mn) / (d.mx - d.mn)); it.value = d.fmt ? d.fmt(val, en) : Num(val, 1, en); }
            else if (d.kind == MenuItemKind::Choice && !d.names.empty()) it.value = d.names[std::clamp((int)std::lround(val), 0, (int)d.names.size() - 1)];
            else if (d.kind == MenuItemKind::Choice) { auto& l = d.labels[std::clamp((int)std::lround(val), 0, (int)d.labels.size() - 1)]; it.value = Utf8ToWide(en ? l.second : l.first); }
            v.items.push_back(it);
        }
    }
    v.selected = sel_; v.hover = hover_; v.hoverControl = hoverCtl_; v.firstRow = scroll_;
    v.capture = capture_;
    v.captureText = capture_ == 2 ? T("Knopf drücken …", "Press a button …") : T("Taste drücken …", "Press a key …");
    v.unsaved = edit_.MenuJson() != savedJson_;
    v.status = !status_.empty() ? status_ : v.unsaved ? T("Nicht gespeicherte Änderungen", "Unsaved changes") : T("Alles gespeichert", "All saved");
    v.footer = T("\u00c4nderungen wirken sofort  \u2022  Esc speichert und schlie\u00dft  \u2022  Tab: Seite", "Changes apply at once  \u2022  Esc saves and closes  \u2022  Tab: page");
    v.btnSave = T("Speichern", "Save"); v.btnReset = T("Zurücksetzen", "Revert"); v.btnDefault = T("Standard", "Defaults");
    return v;
}

std::shared_ptr<OverlayImage> SettingsMenu::TakeImage() {
    if (!dirty_) return nullptr;
    dirty_ = false;
    return RenderMenu(View(), renderH_);
}
