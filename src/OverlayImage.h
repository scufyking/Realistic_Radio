// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include <cstdint>
#include <memory>

// Premultiplied BGRA image (top-down). Used for the D3D11 texture and for UpdateLayeredWindow.
struct OverlayImage {
    int width = 0, height = 0;
    int renderedForScreenHeight = 1080;
    std::vector<uint32_t> pixels;
};

struct OverlayInfo {
    std::wstring headline;   // "Jetzt läuft:" or a status text
    std::wstring title;      // station name (orange, like the original)
    std::wstring subtitle;   // country / volume / state
    int bars = -1;           // 0..5 reception, -1 = hidden
    bool favourite = false;
    std::wstring lcd;        // one-line text for the car-radio style display (empty = title)
    int volume = -1;         // >= 0: this is a volume message (0..100)
    bool muted = false;
    std::wstring volumeLabel;   // label above the volume bar on the in-cab screen
    // in-cab screen card
    std::wstring source;     // source chip: "RADIO", "USB", "CD"
    std::wstring detail;     // small third line (e.g. "Sender 3 / 12", album)
    bool media = false;      // USB/CD track: progress bar instead of signal bars
    std::string mediaType;   // usb | cd | sd | cassette | bluetooth | aux: picture when there is no cover
    bool paused = false;
    int posSec = -1, lenSec = -1;   // track position / length in seconds (-1 = unknown)
    std::wstring song;       // live song title of the station (scrolls on the in-cab screen when it is too long)
    // 1.5.0 designs: station buttons and position
    int button = 0;              // station button (1..9) of the current station, 0 = none
    std::wstring buttonText;     // "Taste 3" / "Button 3"
    std::wstring position;       // "3 / 41" (place in the station list)
    struct Tile {                // station buttons 1..9 for the station-list design
        int button = 0; std::wstring name; std::string logo; bool active = false;
        bool operator==(const Tile& o) const { return button == o.button && name == o.name && logo == o.logo && active == o.active; }
    };
    std::vector<Tile> tiles;
    std::vector<std::shared_ptr<OverlayImage>> tileLogos;   // pictures for `tiles` (filled by the overlay, not compared)
    // 1.6.0 virtual phone: a notification like on a smartphone (banner at the top of the navigation screen)
    bool notice = false;
    std::string noticeIcon;      // phone (connected) | message
    std::wstring noticeApp, noticeTitle, noticeText, noticeTime;
    bool operator==(const OverlayInfo& o) const { return headline == o.headline && title == o.title && subtitle == o.subtitle && bars == o.bars && favourite == o.favourite && lcd == o.lcd && volume == o.volume && muted == o.muted &&
                                                         source == o.source && mediaType == o.mediaType && detail == o.detail && media == o.media && paused == o.paused && posSec == o.posSec && lenSec == o.lenSec && song == o.song &&
                                                         button == o.button && buttonText == o.buttonText && position == o.position && tiles == o.tiles &&
                                                         notice == o.notice && noticeIcon == o.noticeIcon && noticeApp == o.noticeApp && noticeTitle == o.noticeTitle && noticeText == o.noticeText; }
};

// Look of the in-cab card (screen.design): a layout plus colours. Built in: modern, ocean, sport, light, retro (layout
// "standard"), classic (old truck radio), smart (modern dashboard), presets (station list). Own styles: JSON files in
// realistic-radio\styles\ (design "style:<file name>"), see CardStyles.cpp.
struct CardTheme {
    std::string layout = "standard";   // standard | classic | smart | presets
    float bgTop[3], bgBottom[3], accent[3], chipText[3], text[3], sub[3], muted[3], tile[3], inactive[3], art[3];
    float glow = 0;
    std::wstring font = L"Segoe UI";
    float logoTile[3] = {0.95f, 0.95f, 0.96f};                                    // light tile behind station logos
    float lcd[3] = {0.71f, 0.82f, 0.53f}, lcdGlow[3] = {0.93f, 0.62f, 0.30f}, lcdText[3] = {0.09f, 0.13f, 0.06f};   // classic: LCD
    std::shared_ptr<OverlayImage> background;   // own style: picture behind everything (fills the area)
    float backgroundOpacity = 1.f;
};
std::shared_ptr<const CardTheme> CardThemeFor(const std::string& design);   // unknown -> modern
std::shared_ptr<const CardTheme> CustomCardTheme(const std::string& design); // "style:<name>" from the styles folder (nullptr if missing)

// Draws the panel with GDI (text, full Unicode incl. Cyrillic/Greek/Arabic via font fallback) + optional logo.
std::shared_ptr<OverlayImage> RenderOverlay(const OverlayInfo& info, int screenHeight, double scale, const OverlayImage* logo);
// State shown on the truck's infotainment screen (via PrismTextureStreamer window capture)
struct DisplayState {
    bool powered = true, playing = true, muted = false, favourite = false;
    std::wstring headline, title, country, status, prevName, nextName, footer;
    int bars = -1, volume = 0;
    bool operator==(const DisplayState& o) const {
        return powered == o.powered && playing == o.playing && muted == o.muted && favourite == o.favourite && headline == o.headline &&
               title == o.title && country == o.country && status == o.status && prevName == o.prevName && nextName == o.nextName &&
               footer == o.footer && bars == o.bars && volume == o.volume;
    }
};
// Full-screen "infotainment" picture (opaque BGRA) for the radio display window.
std::shared_ptr<OverlayImage> RenderDisplay(const DisplayState& st, int width, int height, const OverlayImage* logo);

// Car-radio style dot-matrix display: one line of `columns` characters starting at character `scroll`
// of `text` (wrapping with a gap, for marquee), plus a 5-step signal indicator. color = 0xRRGGBB.
std::shared_ptr<OverlayImage> RenderRadioLcd(const std::wstring& text, int scroll, int columns, int bars, bool favourite,
                                             int screenHeight, double scale, uint32_t color);

// In-cab screen card (drawn over the navigation for a few seconds): station or volume
// design: modern (default) | ocean | sport | light | retro | classic | smart | presets | style:<own style>
// Live song title line of the card: where it is and the title as a strip (premultiplied, transparent background), so it
// can scroll without rendering the whole card again (ComposeSongLine).
struct SongLine { int x = 0, y = 0, w = 0, h = 0; std::shared_ptr<OverlayImage> strip; };
std::shared_ptr<OverlayImage> RenderScreenCard(const OverlayInfo& info, const OverlayImage* logo, int width, int height, const std::string& design = "modern",
                                               float scale = 1.f, float logoSize = 1.f,   // scale: text and pictures, logoSize: logo / cover tile
                                               SongLine* song = nullptr);                  // set: the song line is left empty and returned here
// Volume as a slim bar at the top of the screen (modern infotainment): "20 %" and a thin level line. The rest stays
// transparent (the navigation shows through) or shows `under` (the radio page while it is pinned).
std::shared_ptr<OverlayImage> RenderScreenVolumeBar(const OverlayInfo& info, int width, int height, const std::string& design, float scale,
                                                    const OverlayImage* under);
// Phone notification (OverlayInfo::notice) as a banner at the top, like on a smartphone: over the navigation
// (transparent elsewhere) or over `under` (the pinned radio page)
std::shared_ptr<OverlayImage> RenderScreenNotice(const OverlayInfo& info, int width, int height, const std::string& design, float scale,
                                                 const OverlayImage* under);
// Card + song strip, shifted left by `offset` pixels (wraps around with a gap when the title is wider than its line)
std::shared_ptr<OverlayImage> ComposeSongLine(const OverlayImage& card, const SongLine& song, int offset);
// ---- virtual phone (1.6.0): a smartphone in the corner of the screen ----
enum class PhoneScreen { Home, Messages, Thread, Calls, CallDetail, Incoming, InCall, Job, Music };
enum PhoneAct { PA_None, PA_Back, PA_App, PA_Item, PA_Accept, PA_Decline, PA_Hangup, PA_Replay, PA_Prev, PA_PlayPause, PA_Next, PA_Close, PA_Folder };
struct PhoneSpot { RECT r; int action = PA_None; int index = -1; };
struct PhoneListItem { std::wstring title, text, time; int kind = 0; bool unread = false; };   // calls: kind 0 missed, 1 answered, 2 declined
struct PhoneMapPoint { float x = 0, y = 0; std::wstring name; int kind = 0; };                // 0..1 in the map; kind 0 city, 1 destination, 2 start
struct PhoneView {
    bool en = false;
    std::string design = "modern";
    std::wstring clock, day, device;            // status bar: game time, weekday, phone name
    PhoneScreen screen = PhoneScreen::Home;
    int sel = -1;                               // keyboard selection
    int unreadMessages = 0, missedCalls = 0;
    std::wstring title;                         // app header
    std::vector<PhoneListItem> items; int scroll = 0;
    std::vector<std::wstring> bubbles, bubbleTimes;   // conversation
    std::wstring caller, callText, callState;          // incoming call / call / call detail
    // job
    bool jobActive = false, late = false;
    std::wstring cargo, from, to, fromCompany, toCompany, remaining, distance, eta, income;
    std::vector<PhoneMapPoint> map; bool haveMap = false; float truckX = 0.5f, truckY = 0.5f, truckHeading = 0; std::wstring mapScale;
    // music (folders marked as Bluetooth)
    bool musicActive = false, musicPaused = false; std::wstring musicTitle, musicArtist, musicFolder; int musicPos = -1, musicLen = -1;
    std::vector<std::wstring> musicFolders; int musicFolderPlaying = -1;
    std::wstring hint;                          // small help line at the bottom
};
// The phone picture (portrait, about 60 % of the screen height); spots: where to click (image pixels)
std::shared_ptr<OverlayImage> RenderPhone(const PhoneView& v, int screenHeight, std::vector<PhoneSpot>* spots);

// Coordinate grid (10 x 10 cells labelled with their x/y position in tenths) for finding the visible area
std::shared_ptr<OverlayImage> RenderScreenGrid(int width, int height, bool flipX = false, bool flipY = false);
// Test mode: grid plus the area the card is drawn into (target coordinates 0..1), with its values in the corners
std::shared_ptr<OverlayImage> RenderScreenTest(int width, int height, bool flipX, bool flipY, float ax, float ay, float aw, float ah);
// start screen at ignition on: logo (or the brand name) on black
std::shared_ptr<OverlayImage> PrepareLogoImage(const OverlayImage& src);   // dark background -> transparent, cut to the logo
std::shared_ptr<OverlayImage> RenderScreenSplash(const OverlayImage* logo, const std::wstring& brand, const std::wstring& model, int width, int height,
                                                 float logoScale = 0.55f, float scale = 1.f);
// Test pattern for finding the right render target
std::shared_ptr<OverlayImage> RenderScreenProbe(const std::wstring& label, int width, int height);

// Box-filtered copy that fits into maxSide x maxSide (returns the input if it is already small enough)
std::shared_ptr<OverlayImage> ShrinkImage(const std::shared_ptr<OverlayImage>& img, int maxSide);
// Settings menu (Ctrl+0): pages on the left, settings or key bindings on the right, buttons at the bottom
enum class MenuItemKind { Toggle, Slider, Choice, Key, Button, Heading, Text };
struct MenuItem {
    MenuItemKind kind = MenuItemKind::Toggle;
    std::wstring label, hint;
    bool on = false;            // toggle
    float frac = 0;             // slider position 0..1
    std::wstring value;         // slider / choice text
    std::wstring key, button;   // key binding row; Button: `button` = text on the button, `value` = info left of it
    bool restart = false;       // takes effect after a game restart
};
struct MenuView {
    std::wstring title, subtitle, section, keyHeader, buttonHeader, restartText;
    std::vector<std::wstring> pages;
    int page = 0;
    std::vector<MenuItem> items;
    int selected = -1;          // keyboard selection
    int firstRow = 0;           // scrolling: first visible row
    int hover = -1;             // row under the mouse
    int capture = 0;            // selected key row waits for: 1 = key, 2 = controller button; 3 = text row is being typed (captureText = the text)
    std::wstring captureText;
    std::wstring status;        // left of the buttons
    bool unsaved = false;
    std::wstring btnSave, btnReset, btnDefault, footer;
    int hoverControl = -1;      // MenuHit kind under the mouse for buttons (Save/Reset/Default/Close) or page index + 100
};
struct MenuHit {
    enum Kind { None, Page, Row, Slider, ChoicePrev, ChoiceNext, Toggle, KeyCell, ButtonCell, ItemButton, Save, Reset, Default, Close, Header, Panel, TextField } kind = None;
    int index = -1;             // page or row
    float frac = 0;             // slider position under the mouse
};
std::shared_ptr<OverlayImage> RenderMenu(const MenuView& v, int screenHeight);
MenuHit HitTestMenu(const MenuView& v, int screenHeight, float x, float y);   // x, y in image pixels
std::shared_ptr<OverlayImage> RenderMenuCursor(int screenHeight);
float MenuSliderFrac(const MenuView& v, int screenHeight, int row, float x);
int MenuVisibleRows(const MenuView& v, int screenHeight);   // rows that fit; more rows scroll   // slider value at image x (dragging)

// Decodes PNG/JPG/BMP via WIC to premultiplied BGRA (nullptr on failure). COM must be initialised.
std::shared_ptr<OverlayImage> LoadImageFile(const std::wstring& path);
