// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Scufy OS (1.8.0): an infotainment system on the truck's navigation screen, like CarPlay / Android Auto in a real car.
// A dock at the side (clock, route, Home / Navigation / Radio / Music, what plays) and apps: Navigation (the game's map
// stays visible), Radio, Music (USB / CD folders), Settings and the apps of add-ons (Bus+, Jobs+, Web-App ...).
// Control: keys / steering wheel buttons (up, down, left, right, OK, back, home), or the control key: then the mouse
// moves a cursor on the screen and the keyboard works there, until the key is pressed again.
// The picture is drawn at the size of the screen area and goes to the overlay as the "page" of the in-cab screen.
#include "Common.h"
#include "Config.h"
#include "OverlayImage.h"
#include <functional>

struct ScufyHost {   // what Scufy OS needs from the radio
    std::function<bool()> english;
    std::function<unsigned()> gameTime;                                // absolute game minutes (0 = unknown)
    std::function<OverlayInfo()> now;                                  // what plays
    std::function<std::shared_ptr<const OverlayImage>()> logo;         // its logo / cover
    std::function<bool()> playing;                                     // radio on / USB not paused
    std::function<void(Action)> action;                                // radio actions: Next, Stop, Source, Band, VolumeUp, Memory1 ...
    std::function<std::vector<std::pair<std::wstring, int>>()> discs;  // USB / CD folders: name, tracks
    std::function<int()> discPlaying;                                  // -1 = none
    std::function<void(int)> playDisc;
    std::function<void(float&, float&)> route;                         // metres and seconds to the destination (0 = no route)
    std::function<ScufyOsConfig&()> cfg;                               // settings ...
    std::function<void()> saveCfg;                                     // ... changed in the OS's own settings app
    std::function<bool()> phoneOpen;                                   // the phone has the add-on apps while it is open
    std::function<KeyBinding()> controlKey;                            // the control key (ends the control mode again)
    std::function<bool(const KeyBinding&)> otherKey;                   // another radio key while in control mode (phone, menu): true = done
};

class ScufyOS {
public:
    void Init(ScufyHost host);
    void SetActive(bool on, bool boot);       // on the screen (enabled and powered); boot: start animation first
    bool Active() const { return active_; }
    // radio thread: w, h = pixels of the screen area; splash = the truck's start logo is on the screen (boot waits)
    void Tick(int w, int h, bool splash);
    std::shared_ptr<OverlayImage> TakeImage();   // a new picture for the screen, nullptr = unchanged
    bool Control() const { return control_; }
    void SetControl(bool on);                 // mouse and keyboard on the screen (control key)
    void UiTick();                            // in control mode: mouse and keyboard
    void OnAction(Action a);                  // Os* actions (steering wheel buttons)
    void ToggleNavi();                        // navigation key: Navigation <-> the app before
    void Shutdown();
private:
    void SetScreen(OsScreen s);
    ScufyView View();
    void Render();
    void Compose();
    void Activate(int act, int idx);
    void Back();
    void Move(int dx, int dy);
    bool DefaultFocus();                      // keys / steering wheel: something sensible highlighted on a new screen
    void ScrollTo(int row);
    bool ListScreen() const { return screen_ == OsScreen::Music || screen_ == OsScreen::Settings || screen_ == OsScreen::AddonList; }
    int RowCount() const;
    bool RowSelectable(int i) const;
    void OpenAddon(int index);
    void ParseUi();
    void PushEvent(const std::string& json) const;
    bool AppPoint(int& x, int& y) const;      // cursor -> pixel of the add-on's picture
    std::vector<OsRow> SettingsRows() const;
    void Setting(int index);
    const OsSpot* SpotAt(float x, float y) const;
    const OsSpot* FocusSpot() const;

    ScufyHost host_;
    bool active_ = false, control_ = false, booting_ = false, mouse_ = false;
    ULONGLONG bootAt_ = 0;
    OsScreen screen_ = OsScreen::Home, beforeNavi_ = OsScreen::Home;
    int w_ = 0, h_ = 0;
    // what was drawn last (a change means: draw again)
    std::wstring clock_, routeText_;
    OverlayInfo nowInfo_; const OverlayImage* nowLogo_ = nullptr; bool nowPlaying_ = false;
    std::string cfgStamp_;
    unsigned appSerial_ = 0, uiSerial_ = 0, frameSerial_ = 0;
    int discStamp_ = -2;
    bool dirty_ = true, composeDirty_ = true;
    std::shared_ptr<OverlayImage> base_, out_, cursorImg_;
    bool outNew_ = false;
    std::vector<OsSpot> spots_;
    RECT appRect_{};
    int visibleRows_ = 0, scroll_ = 0;
    int hoverAct_ = OA_None, hoverIdx_ = -1;
    float cx_ = 0, cy_ = 0;                   // cursor in picture pixels
    ULONGLONG lastMoveAt_ = 0;
    bool keyNav_ = false;                     // last used: keys / steering wheel (not the mouse)
    int lastAppX_ = -1, lastAppY_ = -1;
    // add-on apps
    std::string appId_, appMode_;
    struct UiItem { std::string type, id; std::wstring title, text, value; bool arrow = false, enabled = true; };
    std::vector<UiItem> uiItems_;
    std::wstring uiTitle_, uiHint_, uiSearch_;
    std::string uiScreen_;
    bool uiBack_ = false;
};
