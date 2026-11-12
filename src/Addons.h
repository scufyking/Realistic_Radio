// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Apps of add-ons on the virtual phone (1.7.0). An add-on is its own DLL in the plugins folder (an SCS telemetry plugin)
// that finds realistic-radio.dll and calls the rr_* functions of PluginApi.cpp (documented in sdk/addon/rr_addon.h).
// Every app has an id and one of three modes:
//   "list"   - the add-on describes its screens as JSON (title, rows, buttons, search field, text); the phone draws them
//              in its own look and sends clicks / typed text back as events. The easy way.
//   "canvas" - the add-on draws the screen itself (pixels, portrait phone screen) and gets mouse and keyboard events.
//   "tablet" - like canvas, but the phone turns into a large landscape tablet (e.g. a web page).
// Thread-safe: the add-ons call from their own threads, the phone reads on the radio thread.
#include "OverlayImage.h"
#include <memory>
#include <string>
#include <vector>

namespace Addons {
struct App {
    std::string id, mode = "list";            // mode: list | canvas | tablet
    std::wstring name, nameEn;                // name under the icon (German / English; one of them may be empty)
    float color[3] = {0.52f, 0.34f, 0.96f};   // icon colour
    std::shared_ptr<const OverlayImage> icon; // own icon picture (PNG), else a letter on the colour
};
bool Register(const std::string& json);        // {"id": "...", "name": "...", "name_en": "...", "mode": "list", "color": "#RRGGBB", "icon": "C:\\...\\icon.png"}
std::vector<App> Apps();                       // the apps heard from in the last seconds, in the order they came
bool Find(const std::string& id, App& out);
unsigned Serial();                             // changes when an app comes, goes or changes its name
void Heard(const std::string& id);             // any call of the add-on keeps its app alive

// list mode: the screen as JSON (see sdk/addon/README)
void SetUi(const std::string& id, const std::string& json);
std::string Ui(const std::string& id, unsigned* serial = nullptr);
// canvas / tablet mode
void SetFrame(const std::string& id, std::shared_ptr<OverlayImage> img);
std::shared_ptr<const OverlayImage> Frame(const std::string& id, unsigned* serial = nullptr);
void SetArea(const std::string& id, int w, int h);
bool Area(const std::string& id, int& w, int& h);   // true while this app is open on the phone
void SetOpen(const std::string& id);                // the app open on the phone ("" = none); sends open / close events

// events to the add-on: JSON objects, taken as one JSON array
void Push(const std::string& id, const std::string& eventJson);
int TakeEvents(const std::string& id, char* buf, int size);   // bytes written (0 = none); -needed when buf is too small

// from the radio to the add-ons
void SetState(const std::string& json);        // rr_state
std::string State();
using CityLookup = bool (*)(const std::string& token, std::string& json);
void SetCityLookup(CityLookup f);
bool City(const std::string& token, std::string& json);

// notifications from the add-ons (taken by the radio thread)
struct Notice { std::string app, icon; std::wstring title, text, from; bool message = false; };
void Notify(const std::string& json);          // {"app": "...", "title": "...", "text": "...", "icon": "message", "message": true, "from": "..."}
bool TakeNotice(Notice& n);
}
