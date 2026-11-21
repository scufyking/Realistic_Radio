// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ScufyOS.h"
#include "Addons.h"
#include "D3D11Overlay.h"
#include "InputBlock.h"
#include "L10n.h"
#include "Logger.h"
#include "MenuInput.h"
#include "json.hpp"
#include <cmath>

using nlohmann::json;

namespace {
const ULONGLONG kBootMs = 2200;
struct Accent { const char* id; float c[3]; const wchar_t *de, *en; };
const Accent kAccents[] = {
    {"blue", {0.24f, 0.55f, 1.0f}, L"Blau", L"Blue"},       {"green", {0.16f, 0.78f, 0.45f}, L"Grün", L"Green"},
    {"orange", {1.0f, 0.55f, 0.12f}, L"Orange", L"Orange"}, {"purple", {0.62f, 0.40f, 1.0f}, L"Lila", L"Purple"},
    {"red", {0.96f, 0.27f, 0.32f}, L"Rot", L"Red"},         {"teal", {0.10f, 0.76f, 0.80f}, L"Türkis", L"Teal"},
};
int AccentIndex(const std::string& id) { for (int i = 0; i < 6; ++i) if (id == kAccents[i].id) return i; return 0; }
const double kSpeeds[] = {0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
const char* ScreenName(OsScreen s) {
    switch (s) {
    case OsScreen::Navi: return "navi"; case OsScreen::Radio: return "radio"; case OsScreen::Music: return "music"; case OsScreen::Settings: return "settings";
    default: return "home";
    }
}
OsScreen ScreenOf(const std::string& n) {
    if (n == "navi") return OsScreen::Navi; if (n == "radio") return OsScreen::Radio; if (n == "music") return OsScreen::Music; if (n == "settings") return OsScreen::Settings;
    return OsScreen::Home;
}
// add-on picture into the app area (stretched, opaque)
void Blit(OverlayImage& dst, const RECT& r, const OverlayImage& src) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0 || src.width <= 0 || src.height <= 0) return;
    for (int y = 0; y < h; ++y) {
        int dy = r.top + y; if (dy < 0 || dy >= dst.height) continue;
        int sy = std::min(src.height - 1, (int)((y + 0.5f) * src.height / h));
        for (int x = 0; x < w; ++x) {
            int dx = r.left + x; if (dx < 0 || dx >= dst.width) continue;
            int sx = std::min(src.width - 1, (int)((x + 0.5f) * src.width / w));
            dst.pixels[(size_t)dy * dst.width + dx] = src.pixels[(size_t)sy * src.width + sx] | 0xFF000000u;
        }
    }
}
void Over(OverlayImage& dst, const OverlayImage& src, int ox, int oy) {   // premultiplied
    for (int y = 0; y < src.height; ++y) {
        int dy = oy + y; if (dy < 0 || dy >= dst.height) continue;
        for (int x = 0; x < src.width; ++x) {
            int dx = ox + x; if (dx < 0 || dx >= dst.width) continue;
            uint32_t s = src.pixels[(size_t)y * src.width + x], &d = dst.pixels[(size_t)dy * dst.width + dx];
            uint32_t a = s >> 24; if (!a) continue;
            uint32_t k = 255 - a, out = 0;
            for (int sh = 0; sh < 32; sh += 8) out |= std::min(255u, ((s >> sh) & 255) + (((d >> sh) & 255) * k + 127) / 255) << sh;
            d = out;
        }
    }
}
}

void ScufyOS::Init(ScufyHost host) {
    host_ = std::move(host);
    screen_ = ScreenOf(host_.cfg().app);
    if (screen_ == OsScreen::Settings) screen_ = OsScreen::Home;
}

void ScufyOS::SetActive(bool on, bool boot) {
    if (on == active_) return;
    active_ = on;
    if (!on) {
        SetControl(false);
        if (!appId_.empty() && !host_.phoneOpen()) Addons::SetOpen("");
        base_.reset(); out_.reset();
        Log::Write("Scufy OS: off");
        return;
    }
    booting_ = boot; bootAt_ = 0;
    dirty_ = true;
    if (screen_ == OsScreen::AddonList || screen_ == OsScreen::AddonCanvas) screen_ = OsScreen::Home;   // the add-on may not be there yet
    Log::Write("Scufy OS: on (%s)", ScreenName(screen_));
}

void ScufyOS::SetScreen(OsScreen s) {
    if (s != OsScreen::Navi && screen_ != OsScreen::Navi) beforeNavi_ = s;
    if (s == OsScreen::Navi && screen_ != OsScreen::Navi) beforeNavi_ = screen_;
    const bool wasAddon = screen_ == OsScreen::AddonList || screen_ == OsScreen::AddonCanvas;
    screen_ = s; scroll_ = 0; dirty_ = true;
    hoverAct_ = OA_None; hoverIdx_ = -1;
    const bool addon = s == OsScreen::AddonList || s == OsScreen::AddonCanvas;
    if (wasAddon && !addon && !host_.phoneOpen()) Addons::SetOpen("");
    if (!addon) {
        std::string n = ScreenName(s);
        if (host_.cfg().app != n) { host_.cfg().app = n; }   // remembered (saved with the volume when the game ends)
    }
}

void ScufyOS::ToggleNavi() {
    if (!active_) return;
    SetScreen(screen_ == OsScreen::Navi ? (beforeNavi_ == OsScreen::Navi ? OsScreen::Home : beforeNavi_) : OsScreen::Navi);
}

// ---- add-on apps (like on the phone) --------------------------------------------------------------------------------
void ScufyOS::OpenAddon(int index) {
    auto apps = Addons::Apps();
    if (index < 0 || index >= (int)apps.size()) return;
    appId_ = apps[index].id; appMode_ = apps[index].mode;
    uiSearch_.clear(); uiScreen_.clear(); uiItems_.clear(); uiTitle_.clear(); uiHint_.clear(); uiSerial_ = 0; frameSerial_ = 0;
    SetScreen(appMode_ == "list" ? OsScreen::AddonList : OsScreen::AddonCanvas);
    Addons::SetOpen(appId_);
    if (screen_ == OsScreen::AddonList) { Addons::Ui(appId_, &uiSerial_); ParseUi(); }
    Log::Write("Scufy OS: app '%s' opened", appId_.c_str());
}
void ScufyOS::PushEvent(const std::string& ev) const { if (!appId_.empty()) Addons::Push(appId_, ev); }
void ScufyOS::ParseUi() {
    json j = json::parse(Addons::Ui(appId_), nullptr, false);
    uiItems_.clear(); uiTitle_.clear(); uiHint_.clear(); uiBack_ = false;
    if (!j.is_object()) return;
    auto str = [](const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? Utf8ToWide(o[k].get<std::string>()) : std::wstring(); };
    uiTitle_ = str(j, "title"); uiHint_ = str(j, "hint"); uiBack_ = j.value("back", false);
    std::string screen = j.value("screen", std::string());
    if (j.contains("items") && j["items"].is_array())
        for (auto& o : j["items"]) {
            if (!o.is_object() || uiItems_.size() >= 400) continue;
            UiItem it;
            it.type = o.value("type", std::string("row"));
            if (it.type != "row" && it.type != "button" && it.type != "search" && it.type != "section" && it.type != "text") it.type = "row";
            it.id = o.value("id", std::string());
            it.title = str(o, "title"); it.text = str(o, "text"); it.value = it.type == "search" ? str(o, "placeholder") : str(o, "value");
            it.arrow = o.value("arrow", false); it.enabled = o.value("enabled", true);
            if (it.type == "search" && screen != uiScreen_) uiSearch_ = str(o, "text");
            uiItems_.push_back(it);
        }
    if (screen != uiScreen_) {   // a new screen of the add-on: from the top
        uiScreen_ = screen; scroll_ = 0;
        if (hoverAct_ == OA_Item || hoverAct_ == OA_Search) { hoverAct_ = OA_None; hoverIdx_ = -1; }
    }
    dirty_ = true;
}
bool ScufyOS::AppPoint(int& x, int& y) const {
    x = y = 0;
    const int rw = appRect_.right - appRect_.left, rh = appRect_.bottom - appRect_.top;
    if (rw <= 0 || rh <= 0) return false;
    int fw = rw, fh = rh;
    if (auto f = Addons::Frame(appId_)) { fw = f->width; fh = f->height; }
    float rx = (cx_ - appRect_.left) / rw, ry = (cy_ - appRect_.top) / rh;
    bool in = rx >= 0 && rx < 1 && ry >= 0 && ry < 1;
    x = (int)(std::clamp(rx, 0.f, 0.9999f) * fw); y = (int)(std::clamp(ry, 0.f, 0.9999f) * fh);
    return in;
}

// ---- settings app ------------------------------------------------------------------------------------------------
std::vector<OsRow> ScufyOS::SettingsRows() const {
    const bool en = host_.english();
    const ScufyOsConfig& c = host_.cfg();
    std::vector<OsRow> r;
    auto row = [&](const wchar_t* de, const wchar_t* e, std::wstring value) { OsRow o; o.title = L10n::Pick(de, e, en); o.value = std::move(value); o.arrow = true; r.push_back(o); };
    const Accent& a = kAccents[AccentIndex(c.accent)];
    row(L"Akzentfarbe", L"Accent colour", L10n::Pick(a.de, a.en, en));
    row(L"Leiste", L"Dock", c.dock == "right" ? L10n::Pick(L"Rechts", L"Right", en) : L10n::Pick(L"Links", L"Left", en));
    row(L"Startanimation", L"Start animation", c.boot ? L10n::Pick(L"An", L"On", en) : L10n::Pick(L"Aus", L"Off", en));
    wchar_t b[16]; swprintf(b, 16, L"%.2g ×", c.mouseSpeed);
    row(L"Mausgeschwindigkeit", L"Mouse speed", b);
    row(L"Scufy OS ausschalten", L"Switch Scufy OS off", L10n::Pick(L"zurück zur Radio-Seite", L"back to the radio page", en));
    return r;
}
void ScufyOS::Setting(int i) {
    ScufyOsConfig& c = host_.cfg();
    switch (i) {
    case 0: c.accent = kAccents[(AccentIndex(c.accent) + 1) % 6].id; break;
    case 1: c.dock = c.dock == "right" ? "left" : "right"; break;
    case 2: c.boot = !c.boot; break;
    case 3: {
        int k = 0; for (int j = 0; j < 6; ++j) if (std::abs(kSpeeds[j] - c.mouseSpeed) < 0.01) k = j;
        c.mouseSpeed = kSpeeds[(k + 1) % 6];
        break; }
    case 4: c.enabled = false; Log::Write("Scufy OS: switched off in its settings"); break;
    default: return;
    }
    host_.saveCfg();
    dirty_ = true;
}

// ---- lists -------------------------------------------------------------------------------------------------------
int ScufyOS::RowCount() const {
    switch (screen_) {
    case OsScreen::Music: return (int)host_.discs().size();
    case OsScreen::Settings: return 5;
    case OsScreen::AddonList: return (int)uiItems_.size();
    default: return 0;
    }
}
bool ScufyOS::RowSelectable(int i) const {
    if (i < 0 || i >= RowCount()) return false;
    if (screen_ != OsScreen::AddonList) return true;
    const UiItem& it = uiItems_[i];
    return it.enabled && (it.type == "row" || it.type == "button" || it.type == "search");
}
void ScufyOS::ScrollTo(int row) {
    int vis = std::max(1, visibleRows_);
    if (row < scroll_) scroll_ = row;
    else if (row >= scroll_ + vis) scroll_ = row - vis + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, RowCount() - 1));
    dirty_ = true;
}

// ---- what is drawn -------------------------------------------------------------------------------------------------
ScufyView ScufyOS::View() {
    ScufyView v;
    const bool en = host_.english();
    const ScufyOsConfig& c = host_.cfg();
    v.screen = screen_; v.en = en; v.dockRight = c.dock == "right";
    const Accent& a = kAccents[AccentIndex(c.accent)];
    memcpy(v.accent, a.c, sizeof v.accent);
    unsigned t = host_.gameTime();
    if (t) {
        wchar_t b[16]; swprintf(b, 16, L"%02u:%02u", (t / 60) % 24, t % 60); v.clock = b;
        static const wchar_t* de[] = {L"Montag", L"Dienstag", L"Mittwoch", L"Donnerstag", L"Freitag", L"Samstag", L"Sonntag"};
        static const wchar_t* e[] = {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"};
        v.day = L10n::Pick(de[(t / 1440) % 7], e[(t / 1440) % 7], en);
    } else v.clock = L"--:--";
    v.hoverAct = hoverAct_; v.hoverIdx = hoverIdx_;
    v.now = nowInfo_; v.logo = host_.logo(); v.playing = nowPlaying_;
    float m = 0, s = 0; host_.route(m, s);
    if (m > 50) {
        wchar_t b[32];
        if (m >= 10000) swprintf(b, 32, L"%d km", (int)std::lround(m / 1000)); else swprintf(b, 32, L"%.1f km", m / 1000.0);
        v.navDist = b;
        for (auto& ch : v.navDist) if (ch == L'.' && L10n::Decimal() != L'.') ch = L10n::Decimal();
        int mins = (int)std::lround(s / 60);
        if (mins >= 60) swprintf(b, 32, L"%d h %02d min", mins / 60, mins % 60); else swprintf(b, 32, L"%d min", mins);
        v.navTime = b;
    }
    if (screen_ == OsScreen::Home) {
        auto tile = [&](const wchar_t* de, const wchar_t* e, int icon, const float* col) { OsTile t; t.label = L10n::Pick(de, e, en); t.icon = icon; memcpy(t.color, col, sizeof t.color); v.apps.push_back(t); };
        const float cNavi[3] = {0.11f, 0.72f, 0.52f}, cRadio[3] = {1.0f, 0.52f, 0.14f}, cMusic[3] = {0.97f, 0.26f, 0.44f}, cSet[3] = {0.42f, 0.46f, 0.54f};
        tile(L"Navigation", L"Navigation", 1, cNavi); tile(L"Radio", L"Radio", 2, cRadio); tile(L"Musik", L"Music", 3, cMusic); tile(L"Einstellungen", L"Settings", 4, cSet);
        for (auto& ap : Addons::Apps()) {
            if (v.apps.size() >= 10) break;
            OsTile t; t.label = en && !ap.nameEn.empty() ? ap.nameEn : ap.name; t.icon = 0; memcpy(t.color, ap.color, sizeof t.color); t.img = ap.icon;
            v.apps.push_back(t);
        }
    } else if (screen_ == OsScreen::Music) {
        v.title = L10n::Pick(L"Musik", L"Music", en);
        auto d = host_.discs();
        int playing = host_.discPlaying();
        for (int i = 0; i < (int)d.size(); ++i) {
            OsRow r; r.title = d[i].first; r.value = std::to_wstring(d[i].second) + L10n::Pick(L" Titel", L" tracks", en); r.active = i == playing && nowInfo_.media;
            v.rows.push_back(r);
        }
        if (d.empty()) v.hint = L10n::Pick(L"Keine Musik: Einstellungsmenü → USB / CD → Musikordner", L"No music: settings menu → USB / CD → music folder", en);
    } else if (screen_ == OsScreen::Settings) {
        v.title = L10n::Pick(L"Einstellungen", L"Settings", en);
        v.rows = SettingsRows();
        v.hint = L10n::Pick(L"Alles andere: Einstellungsmenü (Strg + 0), Seite Scufy OS", L"Everything else: settings menu (Ctrl + 0), page Scufy OS", en);
    } else if (screen_ == OsScreen::AddonList) {
        Addons::App ap; Addons::Find(appId_, ap);
        v.title = !uiTitle_.empty() ? uiTitle_ : (en && !ap.nameEn.empty() ? ap.nameEn : ap.name);
        v.hint = uiHint_; v.search = uiSearch_;
        for (auto& it : uiItems_) {
            OsRow r; r.type = it.type; r.title = it.title; r.text = it.text; r.value = it.value; r.arrow = it.arrow; r.enabled = it.enabled;
            v.rows.push_back(r);
        }
        if (uiItems_.empty()) { OsRow r; r.type = "text"; r.text = L10n::Pick(L"Wird geladen …", L"Loading …", en); v.rows.push_back(r); }
    } else if (screen_ == OsScreen::AddonCanvas) {
        Addons::App ap; Addons::Find(appId_, ap);
        v.title = en && !ap.nameEn.empty() ? ap.nameEn : ap.name;
        v.appHasFrame = Addons::Frame(appId_) != nullptr;
        v.hint = L10n::Fill(L10n::Pick(L"{app} wird geladen …", L"Loading {app} …", en), {{L"{app}", v.title}});
    }
    v.scroll = scroll_;
    return v;
}

void ScufyOS::Render() {
    ScufyView v = View();
    std::vector<OsSpot> sp; RECT ar{}; int vis = 0;
    base_ = RenderScufyOS(v, w_, h_, &sp, &ar, &vis);
    spots_ = std::move(sp);
    visibleRows_ = vis;
    if (screen_ == OsScreen::AddonCanvas && (ar.right - ar.left != appRect_.right - appRect_.left || ar.bottom - ar.top != appRect_.bottom - appRect_.top))
        Addons::SetArea(appId_, ar.right - ar.left, ar.bottom - ar.top);
    appRect_ = ar;
    dirty_ = false; composeDirty_ = true;
    if (keyNav_ && hoverAct_ == OA_None && DefaultFocus()) Render();   // a new screen opened with the keys: highlight at once
}

void ScufyOS::Compose() {
    if (!base_) return;
    auto out = std::make_shared<OverlayImage>(*base_);
    if (screen_ == OsScreen::AddonCanvas) if (auto f = Addons::Frame(appId_)) Blit(*out, appRect_, *f);
    if (control_) {
        // control mode: a thin frame in the accent colour, and the cursor
        const Accent& a = kAccents[AccentIndex(host_.cfg().accent)];
        auto px = [](float c) { return (uint32_t)std::clamp((int)(c * 255), 0, 255); };
        uint32_t col = 0xFF000000u | (px(a.c[0]) << 16) | (px(a.c[1]) << 8) | px(a.c[2]);
        const int t = std::max(2, out->height / 160);
        for (int y = 0; y < out->height; ++y) for (int x = 0; x < out->width; ++x)
            if (y < t || y >= out->height - t || x < t || x >= out->width - t) out->pixels[(size_t)y * out->width + x] = col;
        if (!cursorImg_ || cursorImg_->renderedForScreenHeight != (int)(out->height * 1.6f)) cursorImg_ = RenderMenuCursor((int)(out->height * 1.6f));
        if (cursorImg_ && mouse_) Over(*out, *cursorImg_, (int)cx_, (int)cy_);
    }
    out_ = out; outNew_ = true; composeDirty_ = false;
}

void ScufyOS::Tick(int w, int h, bool splash) {
    if (!active_ || w <= 0 || h <= 0) return;
    ULONGLONG now = GetTickCount64();
    if (w != w_ || h != h_) { w_ = w; h_ = h; cx_ = w / 2.f; cy_ = h / 2.f; dirty_ = true; }
    if (booting_) {   // start animation after the truck's start logo
        if (splash) { bootAt_ = 0; return; }
        if (!bootAt_) bootAt_ = now;
        float t = (now - bootAt_) / (float)kBootMs;
        if (t < 1.f) {
            const Accent& a = kAccents[AccentIndex(host_.cfg().accent)];
            out_ = RenderScufyBoot(w_, h_, t, a.c); outNew_ = true;
            return;
        }
        booting_ = false; dirty_ = true;
    }
    // what changed since the last picture
    unsigned t = host_.gameTime();
    wchar_t b[16]; swprintf(b, 16, L"%u", t);
    if (clock_ != b) { clock_ = b; dirty_ = true; }
    OverlayInfo n = host_.now();
    auto logo = host_.logo();
    bool playing = host_.playing();
    if (!(n == nowInfo_) || logo.get() != nowLogo_ || playing != nowPlaying_ || n.posSec != nowInfo_.posSec) { nowInfo_ = n; nowLogo_ = logo.get(); nowPlaying_ = playing; dirty_ = true; }
    float m = 0, s = 0; host_.route(m, s);
    wchar_t r[32]; swprintf(r, 32, L"%d/%d", (int)(m / 100), (int)(s / 60));
    if (routeText_ != r) { routeText_ = r; dirty_ = true; }
    const ScufyOsConfig& c = host_.cfg();
    std::string stamp = c.accent + c.dock + (c.boot ? "1" : "0") + std::to_string(c.mouseSpeed) + (host_.english() ? "e" : "d");
    if (stamp != cfgStamp_) { cfgStamp_ = stamp; dirty_ = true; }
    if (unsigned as = Addons::Serial(); as != appSerial_) {   // an add-on came or went
        appSerial_ = as; dirty_ = true;
        Addons::App tmp;
        if ((screen_ == OsScreen::AddonList || screen_ == OsScreen::AddonCanvas) && !Addons::Find(appId_, tmp)) SetScreen(OsScreen::Home);
    }
    if (screen_ == OsScreen::Music) { int d = host_.discPlaying() * 1000 + (int)host_.discs().size(); if (d != discStamp_) { discStamp_ = d; dirty_ = true; } }
    if (screen_ == OsScreen::AddonList || screen_ == OsScreen::AddonCanvas) {
        if (!host_.phoneOpen()) Addons::SetOpen(appId_);   // after the phone was open, the app is ours again
        if (screen_ == OsScreen::AddonList) { unsigned us = 0; Addons::Ui(appId_, &us); if (us != uiSerial_) { uiSerial_ = us; ParseUi(); } }
        else {
            unsigned fs = 0; Addons::Frame(appId_, &fs);
            if (fs != frameSerial_) { frameSerial_ = fs; composeDirty_ = true; if (!base_ || frameSerial_ == 1) dirty_ = true; }
        }
    }
    if (dirty_) Render();
    if (composeDirty_) Compose();
}

std::shared_ptr<OverlayImage> ScufyOS::TakeImage() {
    if (!active_ || !outNew_) return nullptr;
    outNew_ = false;
    return out_;
}

// ---- control mode ----------------------------------------------------------------------------------------------------
void ScufyOS::SetControl(bool on) {
    if (on == control_) return;
    if (on && !active_) return;
    control_ = on;
    if (on) {
        InputBlock::Install();
        InputBlock::AttachWindow(InputBlock::GameWindow());
        InputBlock::SetMouseBlocked(true);
        bool hooks = MenuInput::Start(std::max(1, D3D11Overlay::ScreenWidth()) / 2.f, std::max(1, D3D11Overlay::ScreenHeight()) / 2.f);
        mouse_ = hooks && MenuInput::MouseActive();
        D3D11Overlay::SetCursorVisible(false);   // the cursor is drawn on the navigation screen instead
        float dx, dy; MenuInput::TakeMouseDelta(dx, dy);
        lastMoveAt_ = GetTickCount64();
        if (hoverAct_ == OA_None) { hoverAct_ = OA_Dock; hoverIdx_ = 0; if (screen_ == OsScreen::Home) { hoverAct_ = OA_App; hoverIdx_ = 0; } }
        Log::Write("Scufy OS: control mode on (%s)", mouse_ ? "mouse + keyboard" : "keyboard");
    } else {
        MenuInput::Stop();
        InputBlock::SetMouseBlocked(false);
        Log::Write("Scufy OS: control mode off");
    }
    dirty_ = true; composeDirty_ = true;
}

const OsSpot* ScufyOS::SpotAt(float x, float y) const {
    for (auto& s : spots_) if (x >= s.r.left && x < s.r.right && y >= s.r.top && y < s.r.bottom) return &s;
    return nullptr;
}
const OsSpot* ScufyOS::FocusSpot() const {
    for (auto& s : spots_) if (s.action == hoverAct_ && s.index == hoverIdx_) return &s;
    return nullptr;
}

// keys / steering wheel: move the highlight to the nearest element in that direction (lists: row by row)
void ScufyOS::Move(int dx, int dy) {
    const OsSpot* cur = FocusSpot();
    if (!cur) { DefaultFocus(); return; }   // nothing highlighted yet: start somewhere sensible
    const bool rowAct = cur->action == OA_Disc || cur->action == OA_Setting || cur->action == OA_Item || cur->action == OA_Search;
    if (dy != 0 && rowAct && ListScreen()) {   // lists: next selectable row, scrolling along
        int n = RowCount();
        for (int i = cur->index + dy; i >= 0 && i < n; i += dy)
            if (RowSelectable(i)) {
                hoverAct_ = screen_ == OsScreen::Music ? OA_Disc : screen_ == OsScreen::Settings ? OA_Setting
                          : (uiItems_[i].type == "search" ? OA_Search : OA_Item);
                hoverIdx_ = i; ScrollTo(i); return;
            }
        if (dy < 0) {   // above the first row: the controls / back button
            for (auto& s : spots_) if (s.action == OA_PlayPause || s.action == OA_Back) { hoverAct_ = s.action; hoverIdx_ = s.index; dirty_ = true; return; }
        }
        return;
    }
    if (dy > 0 && (cur->action == OA_Back || cur->action == OA_PlayPause || cur->action == OA_Prev || cur->action == OA_Next) && ListScreen() && RowCount() > 0) {
        for (int i = scroll_; i < RowCount(); ++i) if (RowSelectable(i)) { hoverAct_ = screen_ == OsScreen::Music ? OA_Disc : screen_ == OsScreen::Settings ? OA_Setting : (uiItems_[i].type == "search" ? OA_Search : OA_Item); hoverIdx_ = i; dirty_ = true; return; }
    }
    const float x0 = (cur->r.left + cur->r.right) / 2.f, y0 = (cur->r.top + cur->r.bottom) / 2.f;
    const OsSpot* best = nullptr; float bestScore = 1e9f;
    for (auto& s : spots_) {
        if (&s == cur || s.action == OA_AppArea) continue;
        float x = (s.r.left + s.r.right) / 2.f, y = (s.r.top + s.r.bottom) / 2.f;
        float along = dx ? (x - x0) * dx : (y - y0) * dy, across = dx ? std::abs(y - y0) : std::abs(x - x0);
        if (along <= 4) continue;
        float score = along + across * 2.f;
        if (score < bestScore) { bestScore = score; best = &s; }
    }
    if (best) { hoverAct_ = best->action; hoverIdx_ = best->index; dirty_ = true; }
}

bool ScufyOS::DefaultFocus() {
    auto set = [&](int a, int i) { hoverAct_ = a; hoverIdx_ = i; dirty_ = true; return true; };
    if (ListScreen()) for (int i = scroll_; i < RowCount(); ++i) if (RowSelectable(i))
        return set(screen_ == OsScreen::Music ? OA_Disc : screen_ == OsScreen::Settings ? OA_Setting : (uiItems_[i].type == "search" ? OA_Search : OA_Item), i);
    if (screen_ == OsScreen::Navi) return set(OA_Dock, 1);
    for (auto& s : spots_) if (s.action == OA_PlayPause) return set(s.action, s.index);
    for (auto& s : spots_) if (s.action != OA_Dock && s.action != OA_AppArea && s.action != OA_Back) return set(s.action, s.index);
    if (!spots_.empty()) return set(spots_[0].action, spots_[0].index);
    return false;
}

void ScufyOS::Back() {
    switch (screen_) {
    case OsScreen::AddonList:
        if (uiBack_) { PushEvent("{\"type\":\"back\"}"); return; }   // the add-on goes back within its own screens
        SetScreen(OsScreen::Home); break;
    case OsScreen::Home: break;
    default: SetScreen(OsScreen::Home); break;
    }
}

void ScufyOS::Activate(int act, int idx) {
    switch (act) {
    case OA_Dock:
        if (idx == 0) SetScreen(OsScreen::Home);
        else if (idx == 1) SetScreen(OsScreen::Navi);
        else if (idx == 2 || idx == 4) SetScreen(OsScreen::Radio);
        else if (idx == 3) SetScreen(OsScreen::Music);
        break;
    case OA_App:
        if (idx == 0) SetScreen(OsScreen::Navi);
        else if (idx == 1) SetScreen(OsScreen::Radio);
        else if (idx == 2) SetScreen(OsScreen::Music);
        else if (idx == 3) SetScreen(OsScreen::Settings);
        else OpenAddon(idx - 4);
        break;
    case OA_Prev: host_.action(Action::Previous); break;
    case OA_Next: host_.action(Action::Next); break;
    case OA_PlayPause: host_.action(Action::Stop); break;
    case OA_Source: host_.action(Action::Source); break;
    case OA_Band: host_.action(Action::Band); break;
    case OA_VolUp: host_.action(Action::VolumeUp); break;
    case OA_VolDown: host_.action(Action::VolumeDown); break;
    case OA_Station: if (idx >= 1 && idx <= 9) host_.action((Action)((int)Action::Memory1 + idx - 1)); break;
    case OA_Disc: host_.playDisc(idx); break;
    case OA_Setting: Setting(idx); break;
    case OA_Item:
        if (idx >= 0 && idx < (int)uiItems_.size() && uiItems_[idx].type != "search" && uiItems_[idx].enabled && !uiItems_[idx].id.empty())
            PushEvent(json{{"type", "click"}, {"id", uiItems_[idx].id}}.dump());
        break;
    case OA_Back: Back(); break;
    default: break;
    }
    dirty_ = true;
}

void ScufyOS::OnAction(Action a) {
    if (!active_ || booting_) return;
    keyNav_ = true;
    switch (a) {
    case Action::OsHome: SetScreen(OsScreen::Home); break;
    case Action::OsUp: Move(0, -1); break;
    case Action::OsDown: Move(0, 1); break;
    case Action::OsLeft: Move(-1, 0); break;
    case Action::OsRight: Move(1, 0); break;
    case Action::OsOk: if (const OsSpot* s = FocusSpot()) Activate(s->action, s->index); else Move(0, 0); break;
    case Action::OsBack: Back(); break;
    default: break;
    }
}

void ScufyOS::UiTick() {
    if (!control_) return;
    if (!active_) { SetControl(false); return; }
    // mouse: the cursor on the screen (the movement scaled from the game picture to the screen)
    float dx = 0, dy = 0;
    MenuInput::TakeMouseDelta(dx, dy);
    if ((dx != 0 || dy != 0) && w_ > 0) {
        float k = std::max(0.2f, w_ / (float)std::max(640, D3D11Overlay::ScreenWidth())) * 1.6f * (float)host_.cfg().mouseSpeed;
        cx_ = std::clamp(cx_ + dx * k, 0.f, w_ - 1.f); cy_ = std::clamp(cy_ + dy * k, 0.f, h_ - 1.f);
        lastMoveAt_ = GetTickCount64(); composeDirty_ = true; keyNav_ = false;
        if (const OsSpot* s = SpotAt(cx_, cy_)) { if (s->action != OA_AppArea && (s->action != hoverAct_ || s->index != hoverIdx_)) { hoverAct_ = s->action; hoverIdx_ = s->index; dirty_ = true; } }
        else if (hoverAct_ != OA_None && hoverAct_ != OA_Search) { hoverAct_ = OA_None; hoverIdx_ = -1; dirty_ = true; }
        int x, y;
        if (screen_ == OsScreen::AddonCanvas && AppPoint(x, y) && (x != lastAppX_ || y != lastAppY_)) {
            lastAppX_ = x; lastAppY_ = y;
            PushEvent("{\"type\":\"move\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");
        }
    }
    MenuInput::Event e;
    const KeyBinding ck = host_.controlKey();
    while (control_ && MenuInput::Pop(e)) {
        if (e.type == MenuInput::Event::Key) {
            keyNav_ = true;
            KeyBinding kb; kb.vk = e.vk; kb.ctrl = e.ctrl; kb.shift = e.shift; kb.alt = e.alt;
            if (kb == ck || e.vk == VK_ESCAPE) { SetControl(false); break; }
            if (host_.otherKey && host_.otherKey(kb)) { if (!control_) break; continue; }   // phone / menu key: the radio takes over
            if (screen_ == OsScreen::AddonCanvas) {   // the add-on's app gets the keys
                PushEvent(json{{"type", "key"}, {"vk", e.vk}, {"ctrl", e.ctrl}, {"shift", e.shift}, {"alt", e.alt}}.dump());
                if (e.ch >= 32) PushEvent(json{{"type", "char"}, {"text", WideToUtf8(std::wstring(1, e.ch))}}.dump());
                continue;
            }
            int search = -1;
            if (screen_ == OsScreen::AddonList) for (int i = 0; i < (int)uiItems_.size(); ++i) if (uiItems_[i].type == "search") { search = i; break; }
            const bool typing = search >= 0 && hoverAct_ == OA_Search;
            auto sendText = [&] { PushEvent(json{{"type", "text"}, {"id", uiItems_[search].id}, {"text", WideToUtf8(uiSearch_)}}.dump()); dirty_ = true; };
            switch (e.vk) {
            case VK_UP: Move(0, -1); break;
            case VK_DOWN: Move(0, 1); break;
            case VK_LEFT: if (!typing) Move(-1, 0); break;
            case VK_RIGHT: if (!typing) Move(1, 0); break;
            case VK_RETURN: if (const OsSpot* s = FocusSpot()) Activate(s->action, s->index); break;
            case VK_SPACE: if (typing) { uiSearch_ += L' '; sendText(); } else if (const OsSpot* s = FocusSpot()) Activate(s->action, s->index); break;
            case VK_BACK: if (typing && !uiSearch_.empty()) { uiSearch_.pop_back(); sendText(); } else Back(); break;
            case VK_HOME: SetScreen(OsScreen::Home); break;
            default:
                if (typing && e.ctrl && e.vk == 'V') {
                    if (OpenClipboard(nullptr)) {
                        if (HANDLE hc = GetClipboardData(CF_UNICODETEXT)) if (auto* p = (const wchar_t*)GlobalLock(hc)) { uiSearch_ += p; GlobalUnlock(hc); }
                        CloseClipboard();
                    }
                    if (uiSearch_.size() > 60) uiSearch_.resize(60);
                    sendText();
                } else if (typing && e.ch >= 32 && uiSearch_.size() < 60) { uiSearch_ += e.ch; sendText(); }
                break;
            }
            continue;
        }
        // mouse buttons and wheel
        if (screen_ == OsScreen::AddonCanvas && !(e.type == MenuInput::Event::MouseDown && e.button == 1)) {
            int x, y;
            bool in = AppPoint(x, y);
            std::string pos = ",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y);
            if (e.type == MenuInput::Event::MouseUp) { PushEvent("{\"type\":\"up\",\"button\":" + std::to_string(e.button) + pos + "}"); continue; }
            if (in && e.type == MenuInput::Event::MouseDown) { PushEvent("{\"type\":\"down\",\"button\":" + std::to_string(e.button) + pos + "}"); continue; }
            if (in && e.type == MenuInput::Event::Wheel) { PushEvent("{\"type\":\"wheel\",\"delta\":" + std::to_string(e.wheel * 120) + pos + "}"); continue; }
        }
        if (e.type == MenuInput::Event::MouseDown && e.button == 1) { Back(); continue; }   // right click: back
        if (e.type == MenuInput::Event::Wheel) {
            if (ListScreen()) { scroll_ = std::clamp(scroll_ - e.wheel, 0, std::max(0, RowCount() - std::max(1, visibleRows_))); dirty_ = true; }
            continue;
        }
        if (e.type == MenuInput::Event::MouseDown && e.button == 0) {
            if (const OsSpot* s = SpotAt(cx_, cy_)) {
                if (s->action == OA_Search) { hoverAct_ = OA_Search; hoverIdx_ = s->index; dirty_ = true; }
                else if (s->action != OA_AppArea) { int a = s->action, i = s->index; Activate(a, i); }
            }
        }
    }
    if (dirty_) Render();
    if (composeDirty_) Compose();
}

void ScufyOS::Shutdown() {
    SetControl(false);
    if (!appId_.empty() && (screen_ == OsScreen::AddonList || screen_ == OsScreen::AddonCanvas) && !host_.phoneOpen()) Addons::SetOpen("");
}
