// Realistic Radio Bus+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Add-on for Realistic Radio (1.7.0 or newer) and the Coaches DLC of ETS2, single player. Two things on the phone:
//  - Timetable: the bus job being driven - next stop, planned and expected arrival, delay, passengers, satisfaction.
//    The stages come from the newest save of the profile (the game saves after every stop and every few minutes while
//    driving); the expected arrival comes from the telemetry (game time + the route advisor's time to the target).
//  - Own line: start stop, stops (or a viewpoint for an excursion) and departure time chosen on the phone; then quick
//    save (F5): one line offer of the start stop in that quick save becomes the own line (distances and times from the
//    line offers of the save); quick load (F9) and it is in the contract jobs of the start stop for one game day.
//    Nothing else in the save changes; a copy of the original is kept in plugins\realistic-radio\backup. Saves of
//    Steam Cloud profiles are written through Steam (see common/SaveWriter.h).
//
// Uses the add-on interface of Realistic Radio (sdk/addon/rr_addon.h, "list" mode): the phone draws the screens.
#include <windows.h>
#include <shlobj.h>
#include "../../sdk/addon/rr_addon.h"
#include <scssdk_telemetry.h>
#include <common/scssdk_telemetry_common_channels.h>
#include <common/scssdk_telemetry_truck_common_channels.h>
#include "json.hpp"
#include "BusLogic.h"
#include "../common/SaveWriter.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#define BUS_VERSION "1.1.0"
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
rr_addon_api rr;
const char* kId = "bus";
std::atomic<bool> g_quit{false};
HANDLE g_thread = nullptr;
HMODULE g_module = nullptr;

// telemetry (written by the game thread)
std::atomic<unsigned> g_gameTime{0};
std::atomic<float> g_navDistance{0}, g_navTime{0};
std::atomic<bool> g_paused{true};
std::atomic<int> g_restStop{0};          // game minutes of driving left until a rest is needed
std::atomic<bool> g_restKnown{false};    // false: fatigue is off in the game (no value)

void Log(const std::string& s) { if (rr.log) rr.log(("Bus+: " + s).c_str()); }
std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}

// ---- texts (German / English, the radio's menu language decides)
bool g_de = true;
const char* T(const char* de, const char* en) { return g_de ? de : en; }
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Pretty(std::string t) {
    for (auto& c : t) if (c == '_') c = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 32);
    return t;
}

// ---- names and places of stops. Cities come from Realistic Radio ("berlin" -> "Berlin", map position).
struct CityInfo { std::string name; double x = 0, z = 0; bool pos = false; };
std::map<std::string, CityInfo> g_cityCache;
std::recursive_mutex g_cityMtx;
const CityInfo& City(const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(g_cityMtx);
    auto it = g_cityCache.find(token);
    if (it != g_cityCache.end()) return it->second;
    CityInfo c; c.name = Pretty(token);
    char buf[1024];
    if (rr.city && rr.city(token.c_str(), buf, sizeof buf) > 0) {
        json j = json::parse(buf, nullptr, false);
        if (j.is_object()) {
            if (j.contains("name") && j["name"].is_string() && !j["name"].get<std::string>().empty()) c.name = j["name"].get<std::string>();
            if (j.contains("x") && j.contains("z") && j["x"].is_number() && j["z"].is_number()) { c.x = j["x"].get<double>(); c.z = j["z"].get<double>(); c.pos = true; }
        }
    }
    return g_cityCache.emplace(token, c).first->second;
}
bool IsCity(const std::string& stop) { return stop.rfind("city.", 0) == 0; }
bool IsViewpoint(const std::string& stop) { return stop.rfind("viewpoints.", 0) == 0; }
// viewpoints: "viewpoints.deberlin" -> "Berlin (DE)"; the game's ids start with the region
std::string ViewpointName(const std::string& id) {
    static const std::pair<const char*, const char*> regions[] = {
        {"blkw", "West-Balkan|Western Balkans"}, {"blke", "Schwarzes Meer|Black Sea"}, {"balt", "Baltikum|Baltics"}, {"ibe", "Iberia|Iberia"},
        {"poc", "Nordkap|Nordkapp"}, {"de", "DE|DE"}, {"at", "AT|AT"}, {"it", "IT|IT"}, {"nl", "NL|NL"}, {"be", "BE|BE"}, {"fr", "FR|FR"},
        {"gr", "GR|GR"}, {"sc", "Skandinavien|Scandinavia"}, {"is", "Island|Iceland"}};
    std::string t = id;
    for (auto& r : regions) {
        size_t n = strlen(r.first);
        if (t.size() > n + 2 && t.compare(0, n, r.first) == 0) {
            std::string label = r.second;
            size_t bar = label.find('|');
            label = g_de ? label.substr(0, bar) : label.substr(bar + 1);
            std::string rest = t.substr(n);
            const CityInfo& c = City(rest);   // "denurnberg" -> the city Nürnberg
            return (c.pos ? c.name : Pretty(rest)) + " (" + label + ")";
        }
    }
    return Pretty(t);
}
std::string StopName(const std::string& stop) {
    if (IsCity(stop)) return City(stop.substr(5)).name;
    if (IsViewpoint(stop)) return ViewpointName(stop.substr(11));
    return Pretty(stop);
}
bool StopPos(const std::string& stop, double& x, double& z) {
    if (!IsCity(stop)) return false;
    const CityInfo& c = City(stop.substr(5));
    if (!c.pos) return false;
    x = c.x; z = c.z;
    return true;
}

// ---- times: game minutes -> "Do 14:05"
std::string Clock(unsigned m) { char b[16]; snprintf(b, sizeof b, "%02u:%02u", (m % 1440) / 60, m % 60); return b; }
std::string DayClock(unsigned m) {
    static const char* de[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
    static const char* en[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::string((g_de ? de : en)[(m / 1440) % 7]) + " " + Clock(m);
}
std::string Duration(unsigned m) { char b[16]; snprintf(b, sizeof b, "%u:%02u h", m / 60, m % 60); return b; }
std::string Money(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), r;
    for (size_t i = 0; i < s.size(); ++i) { if (i && (s.size() - i) % 3 == 0) r += g_de ? '.' : ','; r += s[i]; }
    return (v < 0 ? "-" : "") + (g_de ? r + " €" : "€" + r);
}
std::string Percent(double v) { return std::to_string((int)std::lround(std::max(0.0, std::min(1.0, v)) * 100)) + " %"; }

// ---- files
fs::path PluginDir() {   // ...\plugins\realistic-radio (next to Realistic Radio's own files)
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(g_module, p, MAX_PATH);
    return fs::path(p).parent_path() / L"realistic-radio";
}
bool ReadFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream s; s << f.rdbuf(); out = s.str();
    return true;
}
std::vector<fs::path> ProfileRoots() {
    std::vector<fs::path> r;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        r.push_back(fs::path(docs) / L"Euro Truck Simulator 2" / L"profiles");
        CoTaskMemFree(docs);
    }
    wchar_t steam[MAX_PATH]; DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &sz) == ERROR_SUCCESS) {
        std::error_code ec;
        for (auto& u : fs::directory_iterator(fs::path(steam) / L"userdata", ec)) r.push_back(u.path() / L"227300" / L"remote" / L"profiles");
    }
    return r;
}
struct SaveFile { fs::path path; fs::file_time_type time; uintmax_t size = 0; };
std::vector<SaveFile> AllSaves(const wchar_t* only = nullptr) {
    std::vector<SaveFile> r;
    std::error_code ec;
    for (auto& root : ProfileRoots())
        for (auto& prof : fs::directory_iterator(root, ec)) {
            if (!prof.is_directory(ec)) continue;
            for (auto& save : fs::directory_iterator(prof.path() / L"save", ec)) {
                if (only && save.path().filename() != only) continue;
                fs::path f = save.path() / L"game.sii";
                if (fs::is_regular_file(f, ec)) r.push_back({f, fs::last_write_time(f, ec), fs::file_size(f, ec)});
            }
        }
    return r;
}
std::string ProfileName(const fs::path& gameSii) {
    std::wstring hex = gameSii.parent_path().parent_path().parent_path().filename().wstring();
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out += (char)wcstol(hex.substr(i, 2).c_str(), nullptr, 16);
    return out.empty() ? "?" : out;
}

// ---- legs learned from earlier saves (realistic-radio\bus-legs.json): the more line offers were seen, the more
// distances are known instead of estimated
std::mutex g_legMtx;
std::map<std::pair<std::string, std::string>, BusLeg> g_legCache;
bool g_legCacheRead = false;
void MergeLegs(const std::map<std::pair<std::string, std::string>, BusLeg>& legs) {
    std::lock_guard<std::mutex> l(g_legMtx);
    const fs::path file = PluginDir() / L"bus-legs.json";
    if (!g_legCacheRead) {
        g_legCacheRead = true;
        std::string s;
        if (ReadFile(file, s)) {
            json j = json::parse(s, nullptr, false);
            if (j.is_object())
                for (auto& kv : j.items()) {
                    size_t bar = kv.key().find('|');
                    if (bar == std::string::npos || !kv.value().is_array() || kv.value().size() < 4) continue;
                    BusLeg b; b.km = kv.value()[0].get<unsigned>(); b.time = kv.value()[1].get<unsigned>();
                    b.ferry = kv.value()[2].get<unsigned>(); b.base = kv.value()[3].get<unsigned>();
                    g_legCache[{kv.key().substr(0, bar), kv.key().substr(bar + 1)}] = b;
                }
        }
    }
    size_t before = g_legCache.size();
    for (auto& kv : legs) g_legCache[kv.first] = kv.second;
    if (g_legCache.size() == before) return;
    json j = json::object();
    for (auto& kv : g_legCache) j[kv.first.first + "|" + kv.first.second] = {kv.second.km, kv.second.time, kv.second.ferry, kv.second.base};
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream(file, std::ios::binary) << j.dump();
}
std::map<std::pair<std::string, std::string>, BusLeg> AllLegs(const std::map<std::pair<std::string, std::string>, BusLeg>& fromSave) {
    std::lock_guard<std::mutex> l(g_legMtx);
    auto r = g_legCache;
    for (auto& kv : fromSave) r[kv.first] = kv.second;
    return r;
}

// ---- settings (realistic-radio\bus.json)
bool g_notify = true;
void ReadSettings() {
    std::string s;
    if (!ReadFile(PluginDir() / L"bus.json", s)) return;
    json j = json::parse(s, nullptr, false);
    if (j.is_object()) g_notify = j.value("notifications", true);
}
void WriteSettings() {
    std::error_code ec;
    fs::create_directories(PluginDir(), ec);
    std::ofstream(PluginDir() / L"bus.json", std::ios::binary) << json{{"notifications", g_notify}}.dump(2);
}

// ---- what the newest save says
struct State {
    bool ok = false;
    std::string error, profile, saveName;
    fs::path file;
    fs::file_time_type time{};
    BusData data;
    LegEstimator est;
    std::vector<std::string> startStops;    // stops with line offers (can be the start of an own line), sorted by name
    std::vector<std::string> cityStops;     // all city stops, sorted by name
};
std::mutex g_stateMtx;
std::shared_ptr<State> g_state;
std::atomic<bool> g_loading{false};
fs::path g_loadingFile;
fs::file_time_type g_loadingTime{};

std::shared_ptr<State> Current() { std::lock_guard<std::mutex> l(g_stateMtx); return g_state; }

void LoadSave(SaveFile save) {
    if (g_loading.exchange(true)) return;
    g_loadingFile = save.path; g_loadingTime = save.time;
    std::thread([save] {
        auto s = std::make_shared<State>();
        s->file = save.path; s->time = save.time;
        s->profile = ProfileName(save.path);
        s->saveName = U8(save.path.parent_path().filename().wstring());
        std::string bytes, err;
        SiiFile f;
        if (!ReadFile(save.path, bytes) || !f.Load(std::move(bytes), err)) s->error = std::string(T("Spielstand nicht lesbar: ", "Save not readable: ")) + err;
        else if (!ReadBus(f, s->data)) s->error = T("In diesem Spielstand gibt es keine Bushaltestellen – ist das DLC „Coaches“ (Reisebusse) aktiv?",
                                                    "There are no bus stops in this save - is the Coaches DLC active?");
        else {
            MergeLegs(s->data.legs);
            s->est.Init(AllLegs(s->data.legs), StopPos);
            for (auto& kv : s->data.stops) {
                if (!IsCity(kv.first)) continue;
                s->cityStops.push_back(kv.first);
                if (kv.second > 0) s->startStops.push_back(kv.first);
            }
            auto byName = [](const std::string& a, const std::string& b) { return Lower(StopName(a)) < Lower(StopName(b)); };
            std::sort(s->cityStops.begin(), s->cityStops.end(), byName);
            std::sort(s->startStops.begin(), s->startStops.end(), byName);
            s->ok = true;
            Log("read " + s->profile + " / " + s->saveName + ": " + std::to_string(s->data.stops.size()) + " stops, " + std::to_string(s->data.offers) +
                " line offers, " + std::to_string(s->data.legs.size()) + " legs, bus job " + (s->data.job.active ? "active" : "none"));
        }
        { std::lock_guard<std::mutex> l(g_stateMtx); g_state = s; }
        g_loading = false;
    }).detach();
}

// the newest save of all profiles; it is read again when a newer one is there and the game has finished writing it
void WatchSaves(bool force) {
    static DWORD lastScan = 0, stableSince = 0;
    static fs::path cand; static fs::file_time_type candTime{}; static uintmax_t candSize = 0;
    DWORD now = GetTickCount();
    if (!force && now - lastScan < 3000) return;
    lastScan = now;
    if (g_loading) return;
    auto saves = AllSaves();
    if (saves.empty()) {
        if (!Current()) { auto s = std::make_shared<State>(); s->error = T("Kein Spielstand gefunden.", "No save found."); std::lock_guard<std::mutex> l(g_stateMtx); g_state = s; }
        return;
    }
    auto newest = *std::max_element(saves.begin(), saves.end(), [](auto& a, auto& b) { return a.time < b.time; });
    auto cur = Current();
    if (cur && cur->file == newest.path && cur->time == newest.time) return;
    if (newest.path != cand || newest.time != candTime || newest.size != candSize) { cand = newest.path; candTime = newest.time; candSize = newest.size; stableSince = now; if (!force) return; }
    if (!force && now - stableSince < 1500) return;
    LoadSave(newest);
}

// ---- the app
struct Ui {
    std::string screen = "main";
    std::string search;
    int editIdx = -1;
    LinePlan plan;
    std::string status;
    fs::file_time_type requestedAt{};
    bool dirty = true, open = false;
} ui;

std::string Nearest() {
    char buf[4096];
    if (rr.state && rr.state(buf, sizeof buf) > 0) {
        json j = json::parse(buf, nullptr, false);
        if (j.is_object()) {
            g_de = j.value("language", std::string("de")) == "de";
            if (!g_gameTime && j.value("gameTime", 0u)) g_gameTime = j.value("gameTime", 0u);
            return j.value("nearestCity", std::string());
        }
    }
    return {};
}

json Row(const std::string& id, const std::string& title, const std::string& value = "", const std::string& text = "", bool arrow = true) {
    json r{{"type", "row"}, {"id", id}, {"title", title}, {"arrow", arrow}};
    if (!value.empty()) r["value"] = value;
    if (!text.empty()) r["text"] = text;
    return r;
}
json Text(const std::string& text, const std::string& title = "") { json r{{"type", "text"}, {"text", text}}; if (!title.empty()) r["title"] = title; return r; }
json Section(const std::string& t) { return json{{"type", "section"}, {"title", t}}; }

// expected arrival at the next stop (game minutes), from the route advisor; kNil = unknown
unsigned ExpectedArrival() {
    const float d = g_navDistance, t = g_navTime;
    const unsigned now = g_gameTime;
    if (d <= 1.0f || t <= 0.0f || !now) return kNil;
    return now + (unsigned)std::lround(t / 60.0f);
}
std::string DelayText(int delay) {
    if (delay >= 3) return std::string("+") + std::to_string(delay) + T(" min Verspätung", " min late");
    if (delay <= -3) return std::to_string(-delay) + T(" min zu früh", " min early");
    return T("pünktlich", "on time");
}

// ---- line plan: legs and totals
struct PlanLeg { std::string from, to; BusLeg leg; int quality = 0; };
std::vector<PlanLeg> PlanLegs(const State& s) {
    std::vector<PlanLeg> r;
    std::string prev = ui.plan.source;
    for (auto& t : ui.plan.targets) { PlanLeg l; l.from = prev; l.to = t; l.leg = s.est.Get(prev, t, l.quality); r.push_back(l); prev = t; }
    return r;
}
std::string PlanProblem(const State& s, const std::vector<PlanLeg>& legs) {
    if (ui.plan.source.empty()) return T("Start wählen", "Choose the start");
    if (ui.plan.targets.empty()) return ui.plan.tourist ? T("Ziel wählen", "Choose the destination") : T("Mindestens einen Halt hinzufügen", "Add at least one stop");
    if (!std::count(s.startStops.begin(), s.startStops.end(), ui.plan.source)) return T("Am Start gibt es gerade keine Linien – andere Haltestelle wählen.", "The start has no lines right now - choose another stop.");
    for (auto& l : legs) if (l.quality >= 4) return std::string(T("Keine Verbindung bekannt: ", "No connection known: ")) + StopName(l.from) + " → " + StopName(l.to);
    return "";
}
void Notice(const std::string& title, const std::string& text) {
    if (!g_notify || !rr.notify) return;
    rr.notify(json{{"app", "Bus+"}, {"icon", "message"}, {"title", title}, {"text", text}}.dump().c_str());
}

// ---- trip reports (realistic-radio\bus-trips.json): the timetable of every bus job as planned at its start, what
// really happened at each stop, and the result from the bus job log of the save
struct TripStop { std::string stop; unsigned plan = kNil, actual = kNil, on = kNil, off = kNil; };
struct Trip {
    std::string key, profile, type, bus;
    std::vector<TripStop> stops;           // [0] = start (departure), then the arrivals
    unsigned start = kNil, end = kNil, km = 0, passengers = 0;
    long long reward = 0;
    double satisfaction = -1;
    bool done = false;
};
std::mutex g_tripMtx;
std::vector<Trip> g_trips;                 // oldest first
bool g_tripsRead = false;
const unsigned kOnTimeEarly = 5, kOnTimeLate = 3;   // a stop is on time from 5 minutes early to 3 minutes late

fs::path TripFile() { return PluginDir() / L"bus-trips.json"; }
void ReadTrips() {
    if (g_tripsRead) return;
    g_tripsRead = true;
    std::string text;
    if (!ReadFile(TripFile(), text)) return;
    json j = json::parse(text, nullptr, false);
    if (!j.is_array()) return;
    for (auto& t : j) {
        if (!t.is_object()) continue;
        Trip x;
        x.key = t.value("key", ""); x.profile = t.value("profile", ""); x.type = t.value("type", ""); x.bus = t.value("bus", "");
        x.start = t.value("start", kNil); x.end = t.value("end", kNil); x.km = t.value("km", 0u); x.passengers = t.value("passengers", 0u);
        x.reward = t.value("reward", 0LL); x.satisfaction = t.value("satisfaction", -1.0); x.done = t.value("done", false);
        if (t.contains("stops") && t["stops"].is_array())
            for (auto& st : t["stops"]) {
                TripStop y; y.stop = st.value("stop", ""); y.plan = st.value("plan", kNil); y.actual = st.value("actual", kNil);
                y.on = st.value("on", kNil); y.off = st.value("off", kNil);
                x.stops.push_back(y);
            }
        if (!x.key.empty() && x.stops.size() >= 2) g_trips.push_back(x);
    }
}
void WriteTrips() {
    while (g_trips.size() > 200) g_trips.erase(g_trips.begin());
    json j = json::array();
    for (auto& t : g_trips) {
        json st = json::array();
        for (auto& y : t.stops) st.push_back({{"stop", y.stop}, {"plan", y.plan}, {"actual", y.actual}, {"on", y.on}, {"off", y.off}});
        j.push_back({{"key", t.key}, {"profile", t.profile}, {"type", t.type}, {"bus", t.bus}, {"start", t.start}, {"end", t.end}, {"km", t.km},
                     {"passengers", t.passengers}, {"reward", t.reward}, {"satisfaction", t.satisfaction}, {"done", t.done}, {"stops", st}});
    }
    std::error_code ec;
    fs::create_directories(TripFile().parent_path(), ec);
    std::ofstream(TripFile(), std::ios::binary) << j.dump(1);
}
int Delay(const TripStop& y) { return y.plan == kNil || y.actual == kNil ? 0 : (int)y.actual - (int)y.plan; }
bool OnTime(const TripStop& y) { int d = Delay(y); return d >= -(int)kOnTimeEarly && d <= (int)kOnTimeLate; }
// "2 von 3 Halten pünktlich • Ø +4 min"
std::string Punctuality(const Trip& t) {
    int known = 0, ok = 0, sum = 0;
    for (size_t i = 1; i < t.stops.size(); ++i) {
        if (t.stops[i].plan == kNil || t.stops[i].actual == kNil) continue;
        ++known; if (OnTime(t.stops[i])) ++ok; sum += Delay(t.stops[i]);
    }
    if (!known) return "";
    int avg = (int)std::lround((double)sum / known);
    return std::to_string(ok) + T(" von ", " of ") + std::to_string(known) + T(" Halten pünktlich • Ø ", " stops on time • avg ") + (avg > 0 ? "+" : "") + std::to_string(avg) + " min";
}

// follows the bus job in every newly read save; when it is gone and the log has it, the report is finished
void TrackTrip(const State& s) {
    std::lock_guard<std::mutex> l(g_tripMtx);
    ReadTrips();
    const BusJob& job = s.data.job;
    bool changed = false;
    if (job.active) {
        std::string key = s.profile + "|" + job.stages.front().from;
        for (auto& st : job.stages) key += ">" + st.to;
        const unsigned dep0 = job.stages[0].plannedDeparture != kNil ? job.stages[0].plannedDeparture : job.stages[0].departure;
        key += "@" + std::to_string(dep0);
        Trip* t = nullptr;
        for (auto& x : g_trips) if (x.key == key) t = &x;
        if (!t) {
            Trip x; x.key = key; x.profile = s.profile; x.type = job.type;
            // the timetable as it is at the start: departure + stage times + 15 minutes at every stop
            TripStop a; a.stop = job.stages.front().from; a.plan = dep0;
            x.stops.push_back(a);
            unsigned t0 = dep0;
            for (size_t i = 0; i < job.stages.size(); ++i) {
                TripStop y; y.stop = job.stages[i].to;
                if (t0 != kNil) { t0 += job.stages[i].stageTime + job.stages[i].ferryTime; y.plan = t0; t0 += 15; }
                x.stops.push_back(y);
            }
            g_trips.push_back(x);
            t = &g_trips.back();
            changed = true;
            Log("trip started: " + key);
        }
        if (t->done) return;
        auto set = [&](unsigned& field, unsigned v) { if (v != kNil && field != v) { field = v; changed = true; } };
        set(t->stops[0].actual, job.stages[0].departure);
        set(t->stops[0].on, job.stages[0].entered);
        set(t->start, job.stages[0].departure);
        for (size_t i = 0; i < job.stages.size() && i + 1 < t->stops.size(); ++i) {
            set(t->stops[i + 1].actual, job.stages[i].arrival);
            set(t->stops[i + 1].off, job.stages[i].left);
            if (i + 1 < job.stages.size()) set(t->stops[i + 1].on, job.stages[i + 1].entered);
        }
    } else {
        // finished? the log of the save has the job (same stops, started at the same time)
        for (auto& t : g_trips) {
            if (t.done || t.profile != s.profile || t.start == kNil) continue;
            for (auto& e : s.data.log) {
                if (e.from != t.stops.front().stop || e.to != t.stops.back().stop || e.start != t.start) continue;
                t.done = true; t.end = e.end; t.km = e.km; t.passengers = e.passengers; t.reward = e.reward; t.satisfaction = e.satisfaction; t.bus = e.bus;
                if (t.stops.back().actual == kNil) t.stops.back().actual = e.end;
                changed = true;
                Log("trip finished: " + t.key + ", " + std::to_string(t.reward));
                std::string line = StopName(t.stops.front().stop) + " → " + StopName(t.stops.back().stop);
                std::string text = Money(t.reward) + (t.satisfaction >= 0 ? " • " + Percent(t.satisfaction) + T(" zufrieden", " satisfied") : "");
                std::string p = Punctuality(t);
                if (!p.empty()) text += "\n" + p;
                Notice(std::string(T("Fahrt beendet: ", "Job finished: ")) + line, text);
                break;
            }
        }
    }
    if (changed) WriteTrips();
}

// minutes of driving until the next stop / the end of the line (from the route advisor and the timetable)
void DrivingAhead(const BusJob& job, unsigned& toNext, unsigned& toEnd) {
    toNext = toEnd = kNil;
    const size_t i = std::min<size_t>(job.stageIdx, job.stages.size() - 1);
    if (g_navDistance <= 1.0f || g_navTime <= 0.0f) return;
    toNext = (unsigned)std::lround(g_navTime / 60.0f);
    toEnd = toNext;
    for (size_t k = i + 1; k < job.stages.size(); ++k) toEnd += job.stages[k].baseTime ? job.stages[k].baseTime : job.stages[k].stageTime;
}

void ShowMain(json& u, json& items, const State* s) {
    u["title"] = "Bus+";
    if (!s || g_loading && !s) { items.push_back(Text(T("Spielstand wird gelesen …", "Reading the save …"))); return; }
    if (!s->ok) {
        items.push_back(Text(s->error, T("Geht gerade nicht", "Not possible right now")));
        items.push_back(json{{"type", "button"}, {"id", "reload"}, {"title", T("Nochmal versuchen", "Try again")}});
        return;
    }
    const BusJob& job = s->data.job;
    if (job.active) {
        const size_t i = std::min<size_t>(job.stageIdx, job.stages.size() - 1);
        const BusStage& st = job.stages[i];
        const bool started = job.stages[0].departure != kNil;
        items.push_back(Section(job.type == "tourist" ? T("AUSFLUG", "EXCURSION") : T("BUSFAHRT", "BUS JOB")));
        std::string line = StopName(job.stages.front().from);
        for (auto& x : job.stages) line += " → " + StopName(x.to);
        unsigned km = 0; for (auto& x : job.stages) km += x.distance;
        items.push_back(Row("stops", T("Linie", "Line"), "", line + "\n" + std::to_string(job.stages.size()) + T(" Halte • ", " stops • ") + std::to_string(km) + " km"));
        const unsigned expect = ExpectedArrival();
        if (!started) {
            const unsigned dep = job.PlannedDeparture(0);
            items.push_back(Row("next", T("Zum Start", "To the start"), StopName(st.from), dep != kNil ? std::string(T("Abfahrt ", "Departure ")) + DayClock(dep) : "", false));
            if (expect != kNil && dep != kNil)
                items.push_back(Row("eta", T("Ankunft dort", "Arrival there"), Clock(expect), expect <= dep ? T("rechtzeitig", "in time") : std::string("+") + std::to_string(expect - dep) + T(" min zu spät", " min late"), false));
        } else {
            const unsigned plan = job.PlannedArrival(i);
            items.push_back(Row("next", T("Nächster Halt", "Next stop"), StopName(st.to), plan != kNil ? std::string(T("Plan: ", "Planned: ")) + DayClock(plan) : "", false));
            if (expect != kNil) items.push_back(Row("eta", T("Ankunft (Navi)", "Arrival (route advisor)"), DayClock(expect), plan != kNil ? DelayText((int)expect - (int)plan) : "", false));
        }
        if (g_navDistance > 1.0f) {
            unsigned m = (unsigned)std::lround(g_navTime / 60.0f);
            items.push_back(Row("dist", T("Noch", "Remaining"), std::to_string((int)std::lround(g_navDistance / 1000.0f)) + " km", Duration(m), false));
        }
        std::string pax = std::to_string(job.passengers) + T(" an Bord", " on board");
        std::string paxText;
        if (started) {
            paxText = std::to_string(st.leavePlanned) + T(" steigen aus", " get off");
            if (i + 1 < job.stages.size()) paxText += ", " + std::to_string(job.stages[i + 1].enterPlanned) + T(" steigen ein", " get on");
        } else paxText = std::to_string(st.enterPlanned) + T(" warten am Start", " waiting at the start");
        items.push_back(Row("pax", T("Fahrgäste", "Passengers"), pax, paxText, false));
        if (job.satisfaction >= 0) items.push_back(Row("sat", T("Zufriedenheit", "Satisfaction"), Percent(job.satisfaction), "", false));
        items.push_back(Section(T("LENKZEIT", "DRIVING TIME")));
        if (!g_restKnown) items.push_back(Row("rest", T("Bis zur Pause", "Until the next rest"), "–", T("Müdigkeit ist im Spiel ausgeschaltet", "Fatigue is turned off in the game"), false));
        else {
            const int rest = std::max(0, (int)g_restStop);
            unsigned toNext, toEnd;
            DrivingAhead(job, toNext, toEnd);
            const std::string next = StopName(st.to), end = StopName(job.stages.back().to);
            std::string text;
            if (toNext == kNil) text = T("Ziel im Navi fehlt – Restzeit unbekannt", "No route in the advisor - time ahead unknown");
            else if ((unsigned)rest >= toEnd) text = std::string(T("reicht bis zum Ziel ", "enough to the end ")) + end;
            else if ((unsigned)rest >= toNext) text = std::string(T("reicht bis ", "enough to ")) + next + T(", danach Pause einplanen", ", plan a rest after it");
            else text = std::string(T("Pause vor ", "Rest needed before ")) + next + T(" nötig – Rastplatz einplanen", " - plan a rest area");
            items.push_back(Row("rest", T("Bis zur Pause", "Until the next rest"), Duration((unsigned)rest), text, false));
        }
        items.push_back(Text(std::string(T("Stand: Spielstand ", "As of save ")) + s->saveName + " (" + Clock(s->data.gameTime) + T(") – wird nach jedem Halt neu gelesen", ") - read again after every stop")));
    } else {
        items.push_back(Section(T("BUSFAHRT", "BUS JOB")));
        items.push_back(Text(T("Keine Busfahrt aktiv. Nimm im Spiel eine Vertragsfahrt an – hier steht dann der Fahrplan mit Verspätung und Fahrgästen.",
                               "No bus job right now. Take a contract job in the game - the timetable with delay and passengers shows here.")));
    }
    items.push_back(Section(T("EIGENE LINIE", "OWN LINE")));
    items.push_back(Row("plan", T("Linie planen", "Plan a line"), "", T("Start, Halte und Abfahrt selbst wählen", "Choose start, stops and departure")));
    items.push_back(Section(T("FAHRTENBUCH", "LOGBOOK")));
    {
        std::string last;
        if (!s->data.log.empty()) {
            const BusLogEntry& e = s->data.log.back();
            last = std::string(T("Zuletzt: ", "Last: ")) + StopName(e.from) + " → " + StopName(e.to) + " • " + Money(e.reward);
        }
        items.push_back(Row("book", T("Fahrten und Fahrtberichte", "Jobs and reports"), std::to_string(s->data.log.size()), last));
    }
    items.push_back(Section(T("EINSTELLUNGEN", "SETTINGS")));
    items.push_back(Row("notify", T("Hinweise auf dem Navi", "Notices on the navigation"), g_notify ? T("an", "on") : T("aus", "off"), T("nächster Halt, Verspätung, Pause, Fahrtbericht", "next stop, delay, rest, report"), false));
}

void ShowStops(json& u, json& items, const State* s) {
    u["back"] = true; u["title"] = T("Halte", "Stops");
    if (!s || !s->ok || !s->data.job.active) { items.push_back(Text(T("Keine Busfahrt aktiv.", "No bus job right now."))); return; }
    const BusJob& job = s->data.job;
    items.push_back(Row("st", StopName(job.stages[0].from), job.PlannedDeparture(0) != kNil ? std::string(T("ab ", "dep ")) + DayClock(job.PlannedDeparture(0)) : "",
                        job.stages[0].departure != kNil ? std::string(T("abgefahren ", "left ")) + Clock(job.stages[0].departure) : T("Start", "Start"), false));
    for (size_t i = 0; i < job.stages.size(); ++i) {
        const BusStage& st = job.stages[i];
        const unsigned plan = job.PlannedArrival(i);
        std::string text = std::to_string(st.distance) + " km • ";
        if (st.arrival != kNil) text += std::string(T("angekommen ", "arrived ")) + Clock(st.arrival) + (plan != kNil ? " (" + DelayText((int)st.arrival - (int)plan) + ")" : "");
        else if (i == job.stageIdx && st.departure != kNil) text += T("jetzt unterwegs", "on the way now");
        else text += T("später", "later");
        text += "\n" + std::to_string(st.leavePlanned) + T(" aus", " off");
        if (i + 1 < job.stages.size()) text += ", " + std::to_string(job.stages[i + 1].enterPlanned) + T(" ein", " on");
        items.push_back(Row("st", std::to_string(i + 1) + ". " + StopName(st.to), plan != kNil ? std::string(T("an ", "arr ")) + DayClock(plan) : "", text, false));
    }
}

// the logbook: the bus job log of the save (newest first), with a report where Bus+ followed the job
struct BookEntry { BusLogEntry e; int trip = -1; };
std::vector<BookEntry> Book(const State& s) {
    std::vector<BookEntry> r;
    std::lock_guard<std::mutex> l(g_tripMtx);
    ReadTrips();
    for (auto it = s.data.log.rbegin(); it != s.data.log.rend(); ++it) {
        BookEntry b; b.e = *it;
        for (size_t k = 0; k < g_trips.size(); ++k) {
            const Trip& t = g_trips[k];
            if (t.done && t.profile == s.profile && t.start == it->start && t.stops.front().stop == it->from && t.stops.back().stop == it->to) b.trip = (int)k;
        }
        r.push_back(b);
    }
    return r;
}
void ShowBook(json& u, json& items, const State* s) {
    u["back"] = true; u["title"] = T("Fahrtenbuch", "Logbook");
    if (!s || !s->ok) return;
    auto book = Book(*s);
    if (book.empty()) { items.push_back(Text(T("Noch keine Busfahrten.", "No bus jobs yet."))); return; }
    long long sum = 0; double sat = 0; int satN = 0;
    for (auto& b : book) { sum += b.e.reward; if (b.e.satisfaction >= 0) { sat += b.e.satisfaction; ++satN; } }
    items.push_back(Text(std::to_string(book.size()) + T(" Fahrten • ", " jobs • ") + Money(sum) + (satN ? " • Ø " + Percent(sat / satN) + T(" zufrieden", " satisfied") : ""), T("Gesamt", "Total")));
    for (size_t i = 0; i < book.size(); ++i) {
        const BusLogEntry& e = book[i].e;
        std::string text = DayClock(e.end) + " • " + std::to_string(e.stages) + T(" Halte • ", " stops • ") + std::to_string(e.km) + " km";
        if (e.satisfaction >= 0) text += " • " + Percent(e.satisfaction);
        if (book[i].trip >= 0) { std::lock_guard<std::mutex> l(g_tripMtx); std::string p = Punctuality(g_trips[book[i].trip]); if (!p.empty()) text += "\n" + p; }
        else text += std::string("\n") + T("ohne Fahrtbericht (gefahren ohne Bus+)", "no report (driven without Bus+)");
        items.push_back(Row(book[i].trip >= 0 ? "r:" + std::to_string(book[i].trip) : "nr", StopName(e.from) + " → " + StopName(e.to), Money(e.reward), text, book[i].trip >= 0));
    }
}
void ShowReport(json& u, json& items) {
    u["back"] = true; u["title"] = T("Fahrtbericht", "Trip report");
    std::lock_guard<std::mutex> l(g_tripMtx);
    if (ui.editIdx < 0 || ui.editIdx >= (int)g_trips.size()) return;
    const Trip& t = g_trips[ui.editIdx];
    std::string line = StopName(t.stops.front().stop);
    for (size_t i = 1; i < t.stops.size(); ++i) line += " → " + StopName(t.stops[i].stop);
    std::string when = t.start != kNil && t.end != kNil ? DayClock(t.start) + " – " + DayClock(t.end) + " (" + Duration(t.end - t.start) + ")" : "";
    items.push_back(Text(line + (when.empty() ? "" : "\n" + when), t.type == "tourist" ? T("Ausflug", "Excursion") : T("Linie", "Line")));
    items.push_back(Section(T("ERGEBNIS", "RESULT")));
    items.push_back(Row("x", T("Lohn", "Pay"), Money(t.reward), "", false));
    if (t.satisfaction >= 0) items.push_back(Row("x", T("Zufriedenheit", "Satisfaction"), Percent(t.satisfaction), "", false));
    items.push_back(Row("x", T("Fahrgäste", "Passengers"), std::to_string(t.passengers), std::to_string(t.km) + " km", false));
    std::string p = Punctuality(t);
    if (!p.empty()) items.push_back(Row("x", T("Pünktlichkeit", "Punctuality"), "", p, false));
    items.push_back(Section(T("HALTE", "STOPS")));
    for (size_t i = 0; i < t.stops.size(); ++i) {
        const TripStop& y = t.stops[i];
        const bool first = i == 0;
        std::string value = y.actual != kNil ? std::string(first ? T("ab ", "dep ") : T("an ", "arr ")) + Clock(y.actual) : "–";
        std::string text = y.plan != kNil ? std::string(T("Plan ", "Plan ")) + Clock(y.plan) : "";
        if (y.plan != kNil && y.actual != kNil) {
            int d = Delay(y);
            text += " • " + (OnTime(y) ? std::string(T("pünktlich", "on time")) + (d ? " (" + std::string(d > 0 ? "+" : "") + std::to_string(d) + " min)" : "") : DelayText(d));
        }
        std::string pax;
        if (y.off != kNil) pax += std::to_string(y.off) + T(" aus", " off");
        if (y.on != kNil) pax += (pax.empty() ? "" : ", ") + std::to_string(y.on) + T(" ein", " on");
        if (!pax.empty()) text += "\n" + pax;
        items.push_back(Row("x", (first ? std::string("") : std::to_string(i) + ". ") + StopName(y.stop), value, text, false));
    }
}

void ShowPlan(json& u, json& items, const State* s) {
    u["back"] = true; u["title"] = T("Linie planen", "Plan a line");
    if (!s || !s->ok) { items.push_back(Text(s ? s->error : T("Spielstand wird gelesen …", "Reading the save …"))); return; }
    auto legs = PlanLegs(*s);
    items.push_back(Section("START"));
    items.push_back(Row("src", T("Haltestelle", "Stop"), ui.plan.source.empty() ? T("wählen", "choose") : StopName(ui.plan.source)));
    items.push_back(Row("kind", T("Art", "Kind"), ui.plan.tourist ? T("Ausflug", "Excursion") : T("Linie", "Line"),
                        ui.plan.tourist ? T("zu einem Ausflugsziel und zurück", "to a viewpoint and back") : T("über bis zu 6 Halte", "over up to 6 stops"), false));
    items.push_back(Section(ui.plan.tourist ? T("ZIEL", "DESTINATION") : T("HALTE", "STOPS")));
    unsigned km = 0, minutes = 0;
    bool estimated = false;
    if (ui.plan.tourist) {
        std::string vp = ui.plan.targets.empty() ? "" : ui.plan.targets[0];
        items.push_back(Row("vp", T("Ausflugsziel", "Viewpoint"), vp.empty() ? T("wählen", "choose") : StopName(vp)));
    }
    for (size_t i = 0; i < legs.size(); ++i) {
        const PlanLeg& l = legs[i];
        km += l.leg.km; minutes += l.leg.time + l.leg.ferry;
        if (l.quality >= 2) estimated = true;
        std::string val = std::string(l.quality >= 2 ? T("ca. ", "about ") : "") + std::to_string(l.leg.km) + " km";
        std::string text = Duration(l.leg.time) + (l.leg.ferry ? T(" + Fähre", " + ferry") : "") + (l.quality >= 4 ? T(" • keine Verbindung bekannt", " • no connection known") : "");
        if (ui.plan.tourist) items.push_back(Row("leg", (i == 0 ? std::string(T("Hin: ", "There: ")) : std::string(T("Zurück: ", "Back: "))) + StopName(l.to), val, text, false));
        else items.push_back(Row("s:" + std::to_string(i), std::to_string(i + 1) + ". " + StopName(l.to), val, text));
    }
    if (!ui.plan.tourist && ui.plan.targets.size() < 6) items.push_back(Row("add", T("+ Halt hinzufügen", "+ Add a stop")));
    items.push_back(Section(T("ABFAHRT", "DEPARTURE")));
    items.push_back(Row("dep", T("Uhrzeit", "Time"), Clock(ui.plan.departure), T("jeden Tag – der Auftrag gilt einen Spieltag", "every day - the offer is valid for one game day")));
    if (!legs.empty()) {
        unsigned total = minutes + 15 * (unsigned)(legs.size() - 1);
        items.push_back(Text(std::string(estimated ? T("ca. ", "about ") : "") + std::to_string(km) + " km • " + Duration(total) + T(" mit Halten", " with stops"), T("Gesamt", "Total")));
    }
    std::string problem = PlanProblem(*s, legs);
    if (!problem.empty() && !ui.plan.source.empty()) items.push_back(Text(problem));
    else if (estimated) items.push_back(Text(T("„ca.“: Diese Strecke kam in keinem Angebot vor – Entfernung und Zeit sind geschätzt.",
                                               "\"about\": no offer had this leg - distance and time are estimated.")));
    items.push_back(json{{"type", "button"}, {"id", "go"}, {"title", T("Linie anlegen", "Create the line")}, {"enabled", problem.empty()}});
    u["hint"] = T("Danach: F5 drücken, dann F9", "Afterwards: press F5, then F9");
}

void ShowPick(json& u, json& items, const State* s, const std::string& nearest) {
    u["back"] = true;
    if (!s || !s->ok) return;
    auto match = [&](const std::string& stop) { return ui.search.empty() || Lower(StopName(stop)).find(Lower(ui.search)) != std::string::npos; };
    if (ui.screen == "src") {
        u["title"] = T("Start", "Start");
        items.push_back(json{{"type", "search"}, {"id", "q"}, {"placeholder", T("Haltestelle suchen …", "Search a stop …")}, {"text", ui.search}});
        const std::string here = "city." + nearest;
        if (ui.search.empty() && std::count(s->startStops.begin(), s->startStops.end(), here))
            items.push_back(Row("p:" + here, std::string(T("Hier: ", "Here: ")) + StopName(here), "", T("die Haltestelle, an der du gerade bist", "the stop you are at now"), false));
        int n = 0;
        for (auto& st : s->startStops) if (match(st) && n++ < 150) items.push_back(Row("p:" + st, StopName(st), std::to_string(s->data.stops.at(st)) + T(" Linien", " lines"), "", false));
        if (!n) items.push_back(Text(T("Nichts gefunden", "Nothing found")));
        items.push_back(Text(T("Nur Haltestellen mit Linien im Angebot. Am besten die, an der du gerade bist – dort nimmst du die Linie an.",
                               "Only stops with lines on offer. Best the one you are at - you take the line there.")));
    } else if (ui.screen == "addStop") {
        u["title"] = T("Halt hinzufügen", "Add a stop");
        items.push_back(json{{"type", "search"}, {"id", "q"}, {"placeholder", T("Stadt suchen …", "Search a city …")}, {"text", ui.search}});
        const std::string prev = ui.plan.targets.empty() ? ui.plan.source : ui.plan.targets.back();
        // known connections from the previous stop first, nearest first
        std::vector<std::pair<unsigned, std::string>> known, other;
        for (auto& st : s->cityStops) {
            if (st == prev || !match(st)) continue;
            int q; BusLeg l = s->est.Get(prev, st, q);
            (q <= 1 ? known : other).push_back({q >= 4 ? 99999u : l.km, st});
        }
        std::sort(known.begin(), known.end()); std::sort(other.begin(), other.end());
        int n = 0;
        if (!known.empty()) items.push_back(Section(T("BEKANNTE STRECKEN", "KNOWN LEGS")));
        for (auto& k : known) if (n++ < 60) items.push_back(Row("t:" + k.second, StopName(k.second), std::to_string(k.first) + " km", "", false));
        if (!other.empty()) items.push_back(Section(T("WEITERE (GESCHÄTZT)", "MORE (ESTIMATED)")));
        for (auto& k : other) if (n++ < 160) items.push_back(Row("t:" + k.second, StopName(k.second), k.first == 99999u ? "?" : std::string(T("ca. ", "about ")) + std::to_string(k.first) + " km", "", false));
        if (!n) items.push_back(Text(T("Nichts gefunden", "Nothing found")));
    } else if (ui.screen == "vp") {
        u["title"] = T("Ausflugsziel", "Viewpoint");
        items.push_back(json{{"type", "search"}, {"id", "q"}, {"placeholder", T("Ziel suchen …", "Search …")}, {"text", ui.search}});
        std::vector<std::pair<unsigned, std::string>> list;
        for (auto& kv : s->data.stops) {
            if (!IsViewpoint(kv.first) || !match(kv.first)) continue;
            int q1, q2; BusLeg a = s->est.Get(ui.plan.source, kv.first, q1), b = s->est.Get(kv.first, ui.plan.source, q2);
            if (q1 >= 4 || q2 >= 4) continue;
            list.push_back({a.km + b.km, kv.first});
        }
        std::sort(list.begin(), list.end());
        for (auto& l : list) items.push_back(Row("v:" + l.second, StopName(l.second), std::to_string(l.first) + T(" km hin und zurück", " km there and back"), "", false));
        if (list.empty()) items.push_back(Text(T("Von hier ist kein Ausflugsziel bekannt.", "No viewpoint known from here.")));
    } else if (ui.screen == "dep") {
        u["title"] = T("Abfahrt", "Departure");
        for (unsigned m = 0; m < 1440; m += 15) items.push_back(Row("d:" + std::to_string(m), Clock(m), m == ui.plan.departure ? "✓" : "", "", false));
        u["select"] = ui.plan.departure / 15;
    } else if (ui.screen == "stopEdit") {
        u["title"] = T("Halt", "Stop");
        if (ui.editIdx < 0 || ui.editIdx >= (int)ui.plan.targets.size()) return;
        items.push_back(Text(std::to_string(ui.editIdx + 1) + ". " + StopName(ui.plan.targets[ui.editIdx])));
        items.push_back(json{{"type", "button"}, {"id", "remove"}, {"title", T("Entfernen", "Remove")}});
        if (ui.editIdx > 0) items.push_back(json{{"type", "button"}, {"id", "up"}, {"title", T("Nach vorne", "Move up")}});
        if (ui.editIdx + 1 < (int)ui.plan.targets.size()) items.push_back(json{{"type", "button"}, {"id", "down"}, {"title", T("Nach hinten", "Move down")}});
    }
}

void Show() {
    auto s = Current();
    const std::string nearest = Nearest();
    json u{{"screen", ui.screen}, {"title", "Bus+"}, {"items", json::array()}};
    json& items = u["items"];
    if (ui.screen == "main") ShowMain(u, items, s.get());
    else if (ui.screen == "stops") ShowStops(u, items, s.get());
    else if (ui.screen == "plan") ShowPlan(u, items, s.get());
    else if (ui.screen == "book") ShowBook(u, items, s.get());
    else if (ui.screen == "report") ShowReport(u, items);
    else if (ui.screen == "wait") {
        u["title"] = T("Linie anlegen", "Create the line");
        items.push_back(Text(T("Schnellspeichern – das Handy trägt die Linie dann in diesen Spielstand ein.", "Quick save - the phone then puts the line into that save."), T("Jetzt F5 drücken", "Now press F5")));
        std::string line = StopName(ui.plan.source);
        for (auto& t : ui.plan.targets) line += " → " + StopName(t);
        items.push_back(Text(line + "\n" + T("Abfahrt ", "Departure ") + Clock(ui.plan.departure)));
        items.push_back(json{{"type", "button"}, {"id", "cancel"}, {"title", T("Abbrechen", "Cancel")}});
    } else if (ui.screen == "done") {
        u["title"] = T("Linie bereit", "Line ready");
        items.push_back(Text(ui.status, T("Jetzt F9 drücken", "Now press F9")));
        items.push_back(json{{"type", "button"}, {"id", "again"}, {"title", T("Fertig", "Done")}});
    } else if (ui.screen == "error") {
        u["title"] = T("Hat nicht geklappt", "That did not work");
        items.push_back(Text(ui.status));
        items.push_back(json{{"type", "button"}, {"id", "again"}, {"title", T("Zurück", "Back")}});
    } else ShowPick(u, items, s.get(), nearest);
    rr.app_ui(kId, u.dump().c_str());
}

void Go(const std::string& screen) { ui.screen = screen; ui.search.clear(); ui.dirty = true; }

// the quick save is there: put the line into it
void Apply(const fs::path& file) {
    std::string bytes, err, summary;
    SiiFile f;
    if (!ReadFile(file, bytes) || !f.Load(bytes, err)) { ui.status = std::string(T("Schnellspeicherstand nicht lesbar: ", "Quick save not readable: ")) + err; Go("error"); return; }
    BusData d;
    if (!ReadBus(f, d)) { ui.status = T("In diesem Spielstand gibt es keine Bushaltestellen.", "There are no bus stops in this save."); Go("error"); return; }
    if (!d.stops.count(ui.plan.source) || d.stops[ui.plan.source] == 0) { ui.status = T("Am Start gibt es in diesem Spielstand keine Linien. Andere Haltestelle wählen.", "The start has no lines in this save. Choose another stop."); Go("error"); return; }
    MergeLegs(d.legs);
    LegEstimator est;
    est.Init(AllLegs(d.legs), StopPos);
    if (!ApplyLine(f, ui.plan, est, summary, err)) { ui.status = std::string(T("Linie konnte nicht eingetragen werden: ", "Could not put the line in: ")) + err; Go("error"); return; }
    std::string how;
    if (!SaveWriter::Write(file, f.Bytes(), PluginDir() / L"backup", how, err)) {
        Log("could not write " + U8(file.wstring()) + ": " + err);
        ui.status = SaveWriter::CloudName(file).empty()
            ? std::string(T("Spielstand konnte nicht geschrieben werden: ", "Could not write the save: ")) + err
            : std::string(T("Dein Profil liegt in der Steam Cloud, und Steam hat den geänderten Spielstand nicht angenommen (", "Your profile is in the Steam Cloud and Steam did not take the changed save (")) + err +
              T("). Abhilfe: im Spiel Profil bearbeiten → „Steam Cloud verwenden“ ausschalten, dann nochmal.", "). Fix: edit the profile in the game → turn off \"Use Steam Cloud\", then try again.");
        Go("error");
        return;
    }
    Log("line written into " + U8(file.wstring()) + " via " + how + ": " + summary);
    std::string line = StopName(ui.plan.source);
    for (auto& t : ui.plan.targets) line += " → " + StopName(t);
    ui.status = std::string(T("Schnellladen (F9) – dann steht die Linie in den Vertragsfahrten von ", "Quick load (F9) - then the line is in the contract jobs at ")) +
                StopName(ui.plan.source) + ":\n" + line + "\n" + T("Abfahrt ", "Departure ") + Clock(ui.plan.departure) + T(" • einen Spieltag lang im Angebot", " • on offer for one game day");
    Go("done");
    if (rr.notify) rr.notify(json{{"app", "Bus+"}, {"icon", "message"}, {"title", T("Linie bereit", "Line ready")}, {"text", T("Jetzt F9 drücken (Schnellladen)", "Now press F9 (quick load)")}}.dump().c_str());
}

void WaitForQuickSave() {
    static fs::path candidate; static uintmax_t lastSize = 0; static fs::file_time_type lastTime{}; static DWORD stableSince = 0;
    for (auto& s : AllSaves(L"quicksave")) {
        if (s.time <= ui.requestedAt) continue;
        if (s.path != candidate || s.size != lastSize || s.time != lastTime) { candidate = s.path; lastSize = s.size; lastTime = s.time; stableSince = GetTickCount(); return; }
        if (GetTickCount() - stableSince >= 1500) { candidate.clear(); Apply(s.path); }
        return;
    }
}

void OnEvent(const json& e) {
    const std::string type = e.value("type", std::string()), id = e.value("id", std::string());
    auto s = Current();
    if (type == "open") { ui.open = true; WatchSaves(true); ui.dirty = true; return; }
    if (type == "close") { ui.open = false; return; }
    if (type == "text") { ui.search = e.value("text", std::string()); ui.dirty = true; return; }
    if (type == "back") {
        if (ui.screen == "src" || ui.screen == "addStop" || ui.screen == "vp" || ui.screen == "dep" || ui.screen == "stopEdit") Go("plan");
        else if (ui.screen == "report") Go("book");
        else Go("main");
        return;
    }
    if (type != "click") return;
    if (id == "reload") { { std::lock_guard<std::mutex> l(g_stateMtx); g_state.reset(); } WatchSaves(true); ui.dirty = true; }
    else if (id == "stops") Go("stops");
    else if (id == "book") Go("book");
    else if (id.rfind("r:", 0) == 0) { ui.editIdx = atoi(id.c_str() + 2); Go("report"); }
    else if (id == "plan") {
        if (ui.plan.source.empty() && s && s->ok) {   // start where the bus is, if there are lines
            std::string here = "city." + Nearest();
            if (std::count(s->startStops.begin(), s->startStops.end(), here)) ui.plan.source = here;
        }
        Go("plan");
    }
    else if (id == "notify") { g_notify = !g_notify; WriteSettings(); ui.dirty = true; }
    else if (id == "src") Go("src");
    else if (id == "kind") {
        ui.plan.tourist = !ui.plan.tourist;
        ui.plan.targets.clear();
        ui.dirty = true;
    }
    else if (id == "vp") Go("vp");
    else if (id == "add") Go("addStop");
    else if (id == "dep") Go("dep");
    else if (id.rfind("p:", 0) == 0) {
        ui.plan.source = id.substr(2);
        if (ui.plan.tourist) ui.plan.targets.clear();
        else if (!ui.plan.targets.empty() && ui.plan.targets[0] == ui.plan.source) ui.plan.targets.erase(ui.plan.targets.begin());
        Go("plan");
    }
    else if (id.rfind("t:", 0) == 0) { ui.plan.targets.push_back(id.substr(2)); Go("plan"); }
    else if (id.rfind("v:", 0) == 0) { ui.plan.targets = {id.substr(2), ui.plan.source}; Go("plan"); }
    else if (id.rfind("d:", 0) == 0) { ui.plan.departure = (unsigned)atoi(id.c_str() + 2); Go("plan"); }
    else if (id.rfind("s:", 0) == 0) { ui.editIdx = atoi(id.c_str() + 2); Go("stopEdit"); }
    else if (id == "remove" || id == "up" || id == "down") {
        auto& t = ui.plan.targets;
        int i = ui.editIdx;
        if (i >= 0 && i < (int)t.size()) {
            if (id == "remove") t.erase(t.begin() + i);
            else if (id == "up" && i > 0) std::swap(t[i], t[i - 1]);
            else if (id == "down" && i + 1 < (int)t.size()) std::swap(t[i], t[i + 1]);
            // the same stop twice in a row is not possible
            for (size_t k = 1; k < t.size();) { if (t[k] == t[k - 1]) t.erase(t.begin() + k); else ++k; }
            if (!t.empty() && t[0] == ui.plan.source) t.erase(t.begin());
        }
        Go("plan");
    }
    else if (id == "go") {
        if (!s || !s->ok || !PlanProblem(*s, PlanLegs(*s)).empty()) { ui.dirty = true; return; }
        ui.requestedAt = fs::file_time_type::clock::now() - std::chrono::seconds(2);
        for (auto& q : AllSaves(L"quicksave")) if (q.time > ui.requestedAt) ui.requestedAt = q.time;
        Go("wait");
        std::string line = ui.plan.source;
        for (auto& t : ui.plan.targets) line += " -> " + t;
        Log("waiting for a quick save (F5): " + line);
    }
    else if (id == "cancel" || id == "again") Go("main");
}

// notices while driving: a new job, the next stop, a delay
void CheckNotices() {
    static std::string lastJobKey; static size_t lastStage = (size_t)-1; static bool warned = false; static DWORD lastCheck = 0;
    auto s = Current();
    if (!s || !s->ok) return;
    const BusJob& job = s->data.job;
    if (!job.active) { lastJobKey.clear(); lastStage = (size_t)-1; return; }
    std::string key = job.stages.front().from;
    for (auto& st : job.stages) key += ">" + st.to;
    key += "@" + std::to_string(job.PlannedDeparture(0));
    const size_t i = std::min<size_t>(job.stageIdx, job.stages.size() - 1);
    if (key != lastJobKey) {
        bool first = lastJobKey.empty() && job.stages[0].departure != kNil;   // the plugin started during a job: no notice
        lastJobKey = key; lastStage = i; warned = false;
        if (!first && job.stages[0].departure == kNil) {
            std::string line = StopName(job.stages.front().from);
            for (auto& st : job.stages) line += " → " + StopName(st.to);
            Notice(T("Busfahrt angenommen", "Bus job taken"), line + "\n" + T("Abfahrt ", "Departure ") + DayClock(job.PlannedDeparture(0)));
        }
        return;
    }
    if (i != lastStage) {
        lastStage = i; warned = false;
        const unsigned plan = job.PlannedArrival(i);
        Notice(std::string(T("Nächster Halt: ", "Next stop: ")) + StopName(job.stages[i].to),
               plan != kNil ? std::string(T("Ankunft laut Plan ", "Planned arrival ")) + DayClock(plan) : "");
        return;
    }
    // rest: warn when the driving time left is shorter than the way to the next stop (60 and 15 minutes before)
    static int restLevel = 0;
    if (g_restKnown && !g_paused) {
        unsigned toNext, toEnd;
        DrivingAhead(job, toNext, toEnd);
        const int rest = g_restStop;
        int level = rest <= 15 ? 2 : rest <= 60 ? 1 : 0;
        if (level == 0) restLevel = 0;
        else if (level > restLevel && toNext != kNil && (unsigned)rest < toNext) {
            restLevel = level;
            Notice(std::string(T("Pause einplanen: noch ", "Plan a rest: ")) + Duration((unsigned)std::max(0, rest)) + T(" Lenkzeit", " of driving left"),
                   StopName(job.stages[i].to) + T(" erst in ", " only in ") + Duration(toNext));
        }
    }
    DWORD now = GetTickCount();
    if (now - lastCheck < 10000 || job.stages[i].departure == kNil || g_paused) return;
    lastCheck = now;
    const unsigned plan = job.PlannedArrival(i), expect = ExpectedArrival();
    if (plan == kNil || expect == kNil) return;
    const int delay = (int)expect - (int)plan;
    if (!warned && delay >= 10) {
        warned = true;
        Notice(std::string(T("Verspätung bis ", "Delay to ")) + StopName(job.stages[i].to), DelayText(delay));
    } else if (warned && delay <= 2) warned = false;
}

DWORD WINAPI Worker(LPVOID) {
    bool found = false;
    for (int i = 0; i < 600 && !g_quit && !found; ++i) { found = rr_addon_load(&rr) != 0; if (!found) Sleep(500); }
    if (!found) return 0;   // Realistic Radio older than 1.7.0 or missing
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ReadSettings();
    Log("add-on " BUS_VERSION " started");
    DWORD lastReg = 0, lastRefresh = 0;
    std::vector<char> buf(64 * 1024);
    bool wasLoading = false;
    std::shared_ptr<State> shown;
    while (!g_quit) {
        DWORD now = GetTickCount();
        if (now - lastReg >= 1000) {
            lastReg = now;
            Nearest();   // language
            rr.app_register(json{{"id", kId}, {"name", "Bus+"}, {"name_en", "Bus+"}, {"mode", "list"}, {"color", "#30B0C7"}}.dump().c_str());
        }
        int n = rr.app_events(kId, buf.data(), (int)buf.size());
        if (n < 0) { buf.resize((size_t)-n + 1024); n = rr.app_events(kId, buf.data(), (int)buf.size()); }
        if (n > 0) {
            json evs = json::parse(std::string(buf.data(), n), nullptr, false);
            if (evs.is_array()) for (auto& e : evs) OnEvent(e);
        }
        WatchSaves(false);
        CheckNotices();
        if (ui.screen == "wait") WaitForQuickSave();
        bool loading = g_loading;
        if (loading != wasLoading) { wasLoading = loading; ui.dirty = true; }
        auto cur = Current();
        if (cur != shown) { shown = cur; ui.dirty = true; if (cur && cur->ok) TrackTrip(*cur); }
        if (ui.open && ui.screen == "main" && now - lastRefresh >= 2000) { lastRefresh = now; ui.dirty = true; }   // arrival, distance
        if (ui.dirty) { ui.dirty = false; Show(); }
        Sleep(ui.screen == "wait" ? 250 : 50);
    }
    CoUninitialize();
    return 0;
}

SCSAPI_VOID OnGameTime(const scs_string_t, const scs_u32_t, const scs_value_t* const v, const scs_context_t) { if (v && v->type == SCS_VALUE_TYPE_u32) g_gameTime = v->value_u32.value; }
SCSAPI_VOID OnNav(const scs_string_t, const scs_u32_t, const scs_value_t* const v, const scs_context_t ctx) {
    if (!v || v->type != SCS_VALUE_TYPE_float) return;
    (ctx ? g_navTime : g_navDistance) = v->value_float.value;
}
SCSAPI_VOID OnRest(const scs_string_t, const scs_u32_t, const scs_value_t* const v, const scs_context_t) {
    if (v && v->type == SCS_VALUE_TYPE_s32) { g_restStop = v->value_s32.value; g_restKnown = true; }
    else g_restKnown = false;
}
SCSAPI_VOID OnPause(const scs_event_t ev, const void* const, const scs_context_t) { g_paused = ev == SCS_TELEMETRY_EVENT_paused; }
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if (!p->common.game_id || strcmp(p->common.game_id, "eut2") != 0) {   // the Coaches DLC is ETS2 only
        p->common.log(SCS_LOG_TYPE_message, "[realistic-radio-bus] Bus+ is for Euro Truck Simulator 2 only");
        return SCS_RESULT_unsupported;
    }
    p->register_for_channel(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnGameTime, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnNav, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnNav, (scs_context_t)1);
    p->register_for_channel(SCS_TELEMETRY_CHANNEL_next_rest_stop, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnRest, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_paused, OnPause, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_started, OnPause, nullptr);
    if (g_thread) return SCS_RESULT_ok;
    g_quit = false;
    g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    p->common.log(SCS_LOG_TYPE_message, "[realistic-radio-bus] Realistic Radio Bus+ " BUS_VERSION " loaded (needs Realistic Radio 1.7.0 or newer)");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    g_quit = true;
    if (g_thread) { WaitForSingleObject(g_thread, 5000); CloseHandle(g_thread); g_thread = nullptr; }
}

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) g_module = m; return TRUE; }
