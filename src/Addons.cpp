// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Addons.h"
#include "Common.h"
#include "Logger.h"
#include <json.hpp>
#include <algorithm>
#include <deque>
#include <cstring>
#include <map>
#include <mutex>

using json = nlohmann::json;

namespace {
struct Entry {
    Addons::App app;
    ULONGLONG heard = 0, order = 0;
    std::string ui; unsigned uiSerial = 0;
    std::shared_ptr<const OverlayImage> frame; unsigned frameSerial = 0;
    int w = 360, h = 720;
    std::deque<std::string> events;
};
std::mutex g_mtx;
std::map<std::string, Entry> g_apps;
std::string g_open, g_state = "{}";
unsigned g_serial = 0;
ULONGLONG g_order = 0;
std::deque<Addons::Notice> g_notices;
Addons::CityLookup g_city = nullptr;
constexpr ULONGLONG kAlive = 5000;

bool Alive(const Entry& e, ULONGLONG now) { return e.heard && now - e.heard < kAlive; }
void PushLocked(Entry& e, const std::string& ev) { if (e.events.size() < 1024) e.events.push_back(ev); }
std::wstring W(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? Utf8ToWide(j[k].get<std::string>()) : std::wstring(); }
}

namespace Addons {
bool Register(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) return false;
    std::string id = j["id"].get<std::string>();
    if (id.empty() || id.size() > 64) return false;
    App a;
    a.id = id;
    a.name = W(j, "name"); a.nameEn = W(j, "name_en");
    if (a.name.empty()) a.name = a.nameEn.empty() ? Utf8ToWide(id) : a.nameEn;
    std::string mode = j.value("mode", std::string("list"));
    a.mode = mode == "canvas" || mode == "tablet" ? mode : "list";
    std::string col = j.value("color", std::string());
    if (col.size() == 7 && col[0] == '#') {
        unsigned v = (unsigned)strtoul(col.c_str() + 1, nullptr, 16);
        a.color[0] = ((v >> 16) & 255) / 255.f; a.color[1] = ((v >> 8) & 255) / 255.f; a.color[2] = (v & 255) / 255.f;
    }
    std::wstring iconPath = W(j, "icon");
    std::lock_guard<std::mutex> l(g_mtx);
    Entry& e = g_apps[id];
    const ULONGLONG now = GetTickCount64();
    bool changed = !Alive(e, now) || e.app.name != a.name || e.app.nameEn != a.nameEn || e.app.mode != a.mode ||
                   memcmp(e.app.color, a.color, sizeof a.color) != 0;
    if (!Alive(e, now)) { e.order = ++g_order; Log::Write("Phone: add-on app '%s' (%s, %s) connected", id.c_str(), WideToUtf8(a.name).c_str(), a.mode.c_str()); }
    std::shared_ptr<const OverlayImage> icon = e.app.icon;
    if (!iconPath.empty() && (!icon || changed)) icon = ShrinkImage(LoadImageFile(iconPath), 256);
    e.app = a; e.app.icon = icon;
    e.heard = now;
    if (changed) ++g_serial;
    return true;
}
std::vector<App> Apps() {
    std::lock_guard<std::mutex> l(g_mtx);
    const ULONGLONG now = GetTickCount64();
    std::vector<std::pair<ULONGLONG, App>> v;
    for (auto& kv : g_apps) if (Alive(kv.second, now)) v.push_back({kv.second.order, kv.second.app});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<App> r;
    for (auto& p : v) r.push_back(p.second);
    return r;
}
bool Find(const std::string& id, App& out) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end() || !Alive(it->second, GetTickCount64())) return false;
    out = it->second.app;
    return true;
}
unsigned Serial() {
    std::lock_guard<std::mutex> l(g_mtx);
    const ULONGLONG now = GetTickCount64();
    unsigned alive = 0;
    for (auto& kv : g_apps) if (Alive(kv.second, now)) alive = alive * 31 + (unsigned)kv.second.order;
    return g_serial * 7919u + alive;
}
void Heard(const std::string& id) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it != g_apps.end() && it->second.heard) it->second.heard = GetTickCount64();
}
void SetUi(const std::string& id, const std::string& text) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) return;
    it->second.heard = GetTickCount64();
    if (it->second.ui != text) { it->second.ui = text; ++it->second.uiSerial; }
}
std::string Ui(const std::string& id, unsigned* serial) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) { if (serial) *serial = 0; return {}; }
    if (serial) *serial = it->second.uiSerial;
    return it->second.ui;
}
void SetFrame(const std::string& id, std::shared_ptr<OverlayImage> img) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) return;
    it->second.heard = GetTickCount64();
    it->second.frame = std::move(img); ++it->second.frameSerial;
}
std::shared_ptr<const OverlayImage> Frame(const std::string& id, unsigned* serial) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) { if (serial) *serial = 0; return nullptr; }
    if (serial) *serial = it->second.frameSerial;
    return it->second.frame;
}
void SetArea(const std::string& id, int w, int h) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it != g_apps.end() && w > 16 && h > 16) { it->second.w = w; it->second.h = h; }
}
bool Area(const std::string& id, int& w, int& h) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) { w = 360; h = 720; return false; }
    it->second.heard = GetTickCount64();
    w = it->second.w; h = it->second.h;
    return g_open == id;
}
void SetOpen(const std::string& id) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (id == g_open) return;
    if (!g_open.empty()) { auto it = g_apps.find(g_open); if (it != g_apps.end()) PushLocked(it->second, R"({"type":"close"})"); }
    g_open = id;
    if (!id.empty()) { auto it = g_apps.find(id); if (it != g_apps.end()) PushLocked(it->second, R"({"type":"open"})"); }
}
void Push(const std::string& id, const std::string& ev) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) return;
    auto& q = it->second.events;
    // only the last mouse position
    if (ev.rfind(R"({"type":"move")", 0) == 0 && !q.empty() && q.back().rfind(R"({"type":"move")", 0) == 0) { q.back() = ev; return; }
    PushLocked(it->second, ev);
}
int TakeEvents(const std::string& id, char* buf, int size) {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_apps.find(id);
    if (it == g_apps.end()) return 0;
    it->second.heard = GetTickCount64();
    auto& q = it->second.events;
    if (q.empty()) return 0;
    std::string out = "[";
    size_t n = 0;
    for (auto& e : q) {
        if (out.size() + e.size() + 2 > (size_t)std::max(0, size) && n > 0) break;
        if (n) out += ",";
        out += e; ++n;
    }
    out += "]";
    if (!buf || (int)out.size() + 1 > size) return -(int)(out.size() + 1);
    memcpy(buf, out.c_str(), out.size() + 1);
    for (size_t i = 0; i < n; ++i) q.pop_front();
    return (int)out.size();
}
void SetState(const std::string& s) { std::lock_guard<std::mutex> l(g_mtx); g_state = s; }
std::string State() { std::lock_guard<std::mutex> l(g_mtx); return g_state; }
void SetCityLookup(CityLookup f) { std::lock_guard<std::mutex> l(g_mtx); g_city = f; }
bool City(const std::string& token, std::string& out) {
    CityLookup f;
    { std::lock_guard<std::mutex> l(g_mtx); f = g_city; }
    return f && f(token, out);
}
void Notify(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    Notice n;
    n.app = j.value("app", std::string()); n.icon = j.value("icon", std::string("message"));
    n.title = W(j, "title"); n.text = W(j, "text"); n.from = W(j, "from");
    n.message = j.value("message", false);
    if (n.title.empty() && n.text.empty()) return;
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_notices.size() < 32) g_notices.push_back(n);
}
bool TakeNotice(Notice& n) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_notices.empty()) return false;
    n = g_notices.front(); g_notices.pop_front();
    return true;
}
}
