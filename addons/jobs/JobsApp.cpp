// Realistic Radio Jobs+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Add-on for Realistic Radio (1.7.0 or newer): own jobs from the phone, in single player. The player picks cargo,
// start company and destination on the phone; then quick save (F5): the add-on replaces one of the start company's job
// offers in that quick save by the own job (cargo, trailer and amount taken from an offer of that cargo in the save);
// quick load (F9) and the job is in the freight market of the start company - a normal job with pay and experience.
// Nothing else in the save changes; a copy of the original is kept as game.sii.rr-backup.
//
// Uses the add-on interface of Realistic Radio (sdk/addon/rr_addon.h, "list" mode): the phone draws the screens.
#include <windows.h>
#include <shlobj.h>
#include "../../sdk/addon/rr_addon.h"
#include <scssdk_telemetry.h>
#include "json.hpp"
#include "SaveGame.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#define JOBS_VERSION "1.0.0"
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
rr_addon_api rr;
const char* kId = "jobs";
std::atomic<bool> g_quit{false};
HANDLE g_thread = nullptr;
bool g_ats = false;

void Log(const std::string& s) { if (rr.log) rr.log(("Jobs+: " + s).c_str()); }
std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}

// ---- texts (German / English, the radio's menu language decides)
bool g_de = true;
const char* T(const char* de, const char* en) { return g_de ? de : en; }

// ---- names: "furniture" -> "Furniture", cities from Realistic Radio ("berlin" -> "Berlin")
std::string Pretty(std::string t) {
    for (auto& c : t) if (c == '_') c = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 32);
    return t;
}
struct CityInfo { std::string name; double x = 0, z = 0; bool pos = false; };
std::map<std::string, CityInfo> g_cityCache;   // entries are never removed: references stay valid
std::recursive_mutex g_cityMtx;                 // the loader thread and the app thread both ask
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
            if (j.contains("x") && j.contains("z")) { c.x = j["x"].get<double>(); c.z = j["z"].get<double>(); c.pos = true; }
        }
    }
    return g_cityCache.emplace(token, c).first->second;
}
std::string CityOf(const std::string& company) { size_t d = company.rfind('.'); return d == std::string::npos ? company : company.substr(d + 1); }
std::string FirmOf(const std::string& company) { size_t d = company.rfind('.'); return d == std::string::npos ? company : company.substr(0, d); }
std::string CargoName(const std::string& cargo) { return Pretty(cargo.rfind("cargo.", 0) == 0 ? cargo.substr(6) : cargo); }
std::string CompanyName(const std::string& company) { return Pretty(FirmOf(company)) + " (" + City(CityOf(company)).name + ")"; }
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

// ---- where the saves are: Documents\<game>\profiles and the Steam cloud (userdata\*\<appid>\remote\profiles)
std::vector<fs::path> ProfileRoots() {
    std::vector<fs::path> r;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        r.push_back(fs::path(docs) / (g_ats ? L"American Truck Simulator" : L"Euro Truck Simulator 2") / L"profiles");
        CoTaskMemFree(docs);
    }
    wchar_t steam[MAX_PATH]; DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &sz) == ERROR_SUCCESS) {
        std::error_code ec;
        for (auto& u : fs::directory_iterator(fs::path(steam) / L"userdata", ec))
            r.push_back(u.path() / (g_ats ? L"270880" : L"227300") / L"remote" / L"profiles");
    }
    return r;
}
struct SaveFile { fs::path path; fs::file_time_type time; uintmax_t size = 0; };
std::vector<SaveFile> AllSaves(const wchar_t* only = nullptr) {   // profiles\*\save\*\game.sii (or only one save folder name)
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
std::string ProfileName(const fs::path& gameSii) {   // ...\profiles\<hex of the name>\save\<save>\game.sii
    std::wstring hex = gameSii.parent_path().parent_path().parent_path().filename().wstring();
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out += (char)wcstol(hex.substr(i, 2).c_str(), nullptr, 16);
    return out.empty() ? "?" : out;
}
bool ReadFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream s; s << f.rdbuf(); out = s.str();
    return true;
}

// ---- what the save offers
struct Data {
    bool ok = false;
    std::string error, profile, saveName;
    std::vector<std::string> cargos;                          // sorted by name
    std::map<std::string, std::vector<std::string>> cityCompanies;   // city -> companies (all)
    std::set<std::string> withOffers;                         // companies that have job offers (can be the start)
    std::vector<std::string> cities;                          // sorted by name
    std::map<std::pair<std::string, std::string>, std::vector<std::pair<unsigned, std::pair<unsigned, unsigned>>>> pairs;   // (from city, to city) -> distance, ferry time / price
    double kmPerUnit = 0;                                     // calibrated: km of a job per map unit of straight distance
};
std::mutex g_dataMtx;
std::shared_ptr<Data> g_data;
std::atomic<bool> g_loading{false};

std::shared_ptr<Data> Analyse(const SaveGame& g) {
    auto d = std::make_shared<Data>();
    std::set<std::string> cargos;
    for (auto& o : g.offers) if (!o.cargo.empty() && !o.company.empty()) cargos.insert(o.cargo);
    d->cargos.assign(cargos.begin(), cargos.end());
    std::sort(d->cargos.begin(), d->cargos.end(), [](auto& a, auto& b) { return Lower(CargoName(a)) < Lower(CargoName(b)); });
    for (auto& c : g.companies) {
        d->cityCompanies[CityOf(c.name)].push_back(c.name);
        if (!c.offers.empty()) d->withOffers.insert(c.name);
    }
    for (auto& kv : d->cityCompanies) {
        d->cities.push_back(kv.first);
        std::sort(kv.second.begin(), kv.second.end(), [](auto& a, auto& b) { return Lower(FirmOf(a)) < Lower(FirmOf(b)); });
    }
    std::sort(d->cities.begin(), d->cities.end(), [](auto& a, auto& b) { return Lower(City(a).name) < Lower(City(b).name); });
    std::vector<double> ratios;
    for (auto& o : g.offers) {
        if (o.company.empty() || o.target.empty() || !o.distance) continue;
        std::string a = CityOf(o.company), b = CityOf(o.target);
        d->pairs[{a, b}].push_back({o.distance, {o.ferryTime, o.ferryPrice}});
        if (ratios.size() < 4000 && !o.ferryTime) {
            const CityInfo &ca = City(a), &cb = City(b);
            double e = std::hypot(ca.x - cb.x, ca.z - cb.z);
            if (ca.pos && cb.pos && e > 2000) ratios.push_back(o.distance / e);
        }
    }
    if (!ratios.empty()) { std::nth_element(ratios.begin(), ratios.begin() + ratios.size() / 2, ratios.end()); d->kmPerUnit = ratios[ratios.size() / 2]; }
    d->ok = true;
    return d;
}

// distance and ferry from A to B: from offers between the two cities, else estimated from the map
bool Distance(const Data& d, const std::string& from, const std::string& to, unsigned& km, unsigned& ft, unsigned& fp, bool& estimated) {
    auto it = d.pairs.find({CityOf(from), CityOf(to)});
    if (it != d.pairs.end() && !it->second.empty()) {
        auto v = it->second;
        std::sort(v.begin(), v.end());
        auto& m = v[v.size() / 2];
        km = m.first; ft = m.second.first; fp = m.second.second; estimated = false;
        return true;
    }
    const CityInfo &a = City(CityOf(from)), &b = City(CityOf(to));
    ft = fp = 0; estimated = true;
    if (CityOf(from) == CityOf(to)) { km = 15; return true; }
    if (!a.pos || !b.pos || d.kmPerUnit <= 0) { km = 400; return false; }
    km = (unsigned)std::max(10.0, std::round(std::hypot(a.x - b.x, a.z - b.z) * d.kmPerUnit));
    return true;
}

void LoadNewest() {
    if (g_loading.exchange(true)) return;
    std::thread([] {
        auto saves = AllSaves();
        auto d = std::make_shared<Data>();
        if (saves.empty()) d->error = T("Kein Spielstand gefunden – einmal speichern (F5) und die App neu öffnen.", "No save found - save once (F5) and open the app again.");
        else {
            auto newest = *std::max_element(saves.begin(), saves.end(), [](auto& a, auto& b) { return a.time < b.time; });
            std::string bytes, err;
            SaveGame g;
            if (!ReadFile(newest.path, bytes) || !g.Load(bytes, err)) d->error = std::string(T("Spielstand nicht lesbar: ", "Save not readable: ")) + err;
            else {
                d = Analyse(g);
                d->profile = ProfileName(newest.path);
                d->saveName = U8(newest.path.parent_path().filename().wstring());
                Log("read " + d->profile + " / " + d->saveName + ": " + std::to_string(g.companies.size()) + " companies, " + std::to_string(g.offers.size()) +
                    " offers, " + std::to_string(d->cargos.size()) + " cargos" + (g.Binary() ? " (binary)" : " (text)"));
            }
        }
        { std::lock_guard<std::mutex> l(g_dataMtx); g_data = d; }
        g_loading = false;
    }).detach();
}

// ---- the app
struct Ui {
    std::string screen = "main";
    std::string cargo, from, to, cityPick;   // chosen cargo / start company / destination company; city of the company list
    std::string search;
    std::string status;                       // wait / done / error text
    fs::file_time_type requestedAt{};         // F5 must come after this
    DWORD requestTick = 0;
    bool dirty = true;
} ui;

std::string Nearest() {
    char buf[4096];
    if (rr.state && rr.state(buf, sizeof buf) > 0) {
        json j = json::parse(buf, nullptr, false);
        if (j.is_object()) {
            g_de = j.value("language", std::string("de")) == "de";
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

void Show() {
    std::shared_ptr<Data> d;
    { std::lock_guard<std::mutex> l(g_dataMtx); d = g_data; }
    const std::string nearest = Nearest();
    json u{{"screen", ui.screen}, {"title", T("Eigener Auftrag", "Own job")}, {"items", json::array()}};
    json& items = u["items"];
    auto match = [&](const std::string& name) { return ui.search.empty() || Lower(name).find(Lower(ui.search)) != std::string::npos; };
    if (!d || g_loading) {
        items.push_back({{"type", "text"}, {"text", T("Spielstand wird gelesen …", "Reading the save …")}});
    } else if (!d->ok) {
        items.push_back({{"type", "text"}, {"title", T("Geht gerade nicht", "Not possible right now")}, {"text", d->error}});
        items.push_back({{"type", "button"}, {"id", "reload"}, {"title", T("Nochmal versuchen", "Try again")}});
    } else if (ui.screen == "main") {
        unsigned km = 0, ft = 0, fp = 0; bool est = true;
        bool all = !ui.cargo.empty() && !ui.from.empty() && !ui.to.empty();
        items.push_back({{"type", "section"}, {"title", T("AUFTRAG", "JOB")}});
        items.push_back(Row("cargo", T("Ware", "Cargo"), ui.cargo.empty() ? T("wählen", "choose") : CargoName(ui.cargo)));
        items.push_back(Row("from", T("Von", "From"), ui.from.empty() ? T("wählen", "choose") : Pretty(FirmOf(ui.from)), ui.from.empty() ? "" : City(CityOf(ui.from)).name));
        items.push_back(Row("to", T("Nach", "To"), ui.to.empty() ? T("wählen", "choose") : Pretty(FirmOf(ui.to)), ui.to.empty() ? "" : City(CityOf(ui.to)).name));
        if (all) {
            Distance(*d, ui.from, ui.to, km, ft, fp, est);
            std::string t = std::string(est ? T("ca. ", "about ") : "") + std::to_string(km) + " km" + (ft ? T(" • mit Fähre", " • with ferry") : "");
            items.push_back({{"type", "text"}, {"title", T("Entfernung", "Distance")}, {"text", t}});
        }
        items.push_back({{"type", "button"}, {"id", "go"}, {"title", T("Auftrag anlegen", "Create job")}, {"enabled", all}});
        items.push_back({{"type", "text"}, {"text", std::string(T("Spielstand: ", "Save: ")) + d->profile + " • " + d->saveName + T(" • nur Einzelspieler", " • single player only")}});
        u["hint"] = T("Danach: F5 drücken, dann F9", "Afterwards: press F5, then F9");
    } else if (ui.screen == "cargo") {
        u["back"] = true; u["title"] = T("Ware", "Cargo");
        items.push_back({{"type", "search"}, {"id", "q"}, {"placeholder", T("Ware suchen …", "Search cargo …")}, {"text", ui.search}});
        int n = 0;
        for (auto& c : d->cargos) if (match(CargoName(c)) && n++ < 120) items.push_back(Row("c:" + c, CargoName(c), "", "", false));
        if (!n) items.push_back({{"type", "text"}, {"text", T("Nichts gefunden", "Nothing found")}});
    } else if (ui.screen == "fromCity" || ui.screen == "toCity") {
        const bool from = ui.screen == "fromCity";
        u["back"] = true; u["title"] = from ? T("Von – Stadt", "From - city") : T("Nach – Stadt", "To - city");
        items.push_back({{"type", "search"}, {"id", "q"}, {"placeholder", T("Stadt suchen …", "Search city …")}, {"text", ui.search}});
        if (from && ui.search.empty() && d->cityCompanies.count(nearest))
            items.push_back(Row("city:" + nearest, std::string(T("Hier: ", "Here: ")) + City(nearest).name, "", T("die Stadt, in der du gerade bist", "the city you are in now")));
        int n = 0;
        for (auto& c : d->cities) {
            if (!match(City(c).name)) continue;
            if (from) { bool any = false; for (auto& co : d->cityCompanies[c]) if (d->withOffers.count(co)) { any = true; break; } if (!any) continue; }
            if (n++ >= 150) break;
            items.push_back(Row("city:" + c, City(c).name));
        }
        if (!n) items.push_back({{"type", "text"}, {"text", T("Nichts gefunden", "Nothing found")}});
    } else if (ui.screen == "fromCompany" || ui.screen == "toCompany") {
        const bool from = ui.screen == "fromCompany";
        u["back"] = true; u["title"] = City(ui.cityPick).name;
        for (auto& co : d->cityCompanies[ui.cityPick]) {
            if (from && !d->withOffers.count(co)) continue;
            items.push_back(Row("co:" + co, Pretty(FirmOf(co)), "", "", false));
        }
    } else if (ui.screen == "wait") {
        u["title"] = T("Auftrag anlegen", "Create job");
        items.push_back({{"type", "text"}, {"title", T("Jetzt F5 drücken", "Now press F5")},
                         {"text", T("Schnellspeichern – das Handy trägt den Auftrag dann in diesen Spielstand ein.", "Quick save - the phone then puts the job into that save.")}});
        items.push_back({{"type", "text"}, {"text", CargoName(ui.cargo) + ": " + CompanyName(ui.from) + " → " + CompanyName(ui.to)}});
        items.push_back({{"type", "button"}, {"id", "cancel"}, {"title", T("Abbrechen", "Cancel")}});
    } else if (ui.screen == "done") {
        u["title"] = T("Auftrag bereit", "Job ready");
        items.push_back({{"type", "text"}, {"title", T("Jetzt F9 drücken", "Now press F9")}, {"text", ui.status}});
        items.push_back({{"type", "button"}, {"id", "again"}, {"title", T("Noch ein Auftrag", "Another job")}});
    } else if (ui.screen == "error") {
        u["title"] = T("Hat nicht geklappt", "That did not work");
        items.push_back({{"type", "text"}, {"text", ui.status}});
        items.push_back({{"type", "button"}, {"id", "again"}, {"title", T("Zurück", "Back")}});
    }
    rr.app_ui(kId, u.dump().c_str());
}

void Go(const std::string& screen) { ui.screen = screen; ui.search.clear(); ui.dirty = true; }

// the quick save is there: put the job into it
void Apply(const fs::path& file) {
    std::string bytes, err;
    SaveGame g;
    if (!ReadFile(file, bytes) || !g.Load(bytes, err)) { ui.status = std::string(T("Schnellspeicherstand nicht lesbar: ", "Quick save not readable: ")) + err; Go("error"); return; }
    const SaveCompany* src = g.Company(ui.from);
    if (!src || src->offers.empty()) { ui.status = T("Die Startfirma hat in diesem Spielstand keine Aufträge. Andere Startfirma wählen.", "The start company has no offers in this save. Choose another one."); Go("error"); return; }
    // the offer to replace: one with the same cargo if there is one, else the first; the template: an offer of that cargo
    std::string victim = src->offers.front(), tmpl;
    for (auto& id : src->offers) if (auto* o = g.Offer(id)) if (o->cargo == ui.cargo) { victim = id; tmpl = id; break; }
    if (tmpl.empty()) for (auto& o : g.offers) if (o.cargo == ui.cargo) { tmpl = o.id; break; }
    if (tmpl.empty() || !g.Offer(victim)) { ui.status = T("Diese Ware gibt es in diesem Spielstand nicht.", "This cargo is not in this save."); Go("error"); return; }
    std::shared_ptr<Data> d;
    { std::lock_guard<std::mutex> l(g_dataMtx); d = g_data; }
    unsigned km = 400, ft = 0, fp = 0; bool est = true;
    if (d) Distance(*d, ui.from, ui.to, km, ft, fp, est);
    const unsigned expiration = g.gameTime + 3 * 1440;   // three days to take it
    if (!g.Replace(victim, tmpl, ui.to, km, ft, fp, expiration, err)) { ui.status = err; Go("error"); return; }
    std::error_code ec;
    fs::copy_file(file, fs::path(file).concat(L".rr-backup"), fs::copy_options::overwrite_existing, ec);
    {
        std::ofstream o(file, std::ios::binary | std::ios::trunc);
        o << g.Bytes();
        if (!o) { ui.status = T("Spielstand konnte nicht geschrieben werden.", "Could not write the save."); Go("error"); return; }
    }
    Log("job written: " + ui.cargo + " " + ui.from + " -> " + ui.to + ", " + std::to_string(km) + " km, into " + U8(file.wstring()));
    ui.status = std::string(T("Schnellladen (F9) – dann steht der Auftrag im Frachtmarkt bei ", "Quick load (F9) - then the job is in the freight market at ")) +
                CompanyName(ui.from) + ":\n" + CargoName(ui.cargo) + " → " + CompanyName(ui.to) + " (" + (est ? T("ca. ", "about ") : "") + std::to_string(km) + " km)";
    Go("done");
    rr.notify(json{{"app", T("Aufträge+", "Jobs+")}, {"icon", "message"}, {"title", T("Auftrag bereit", "Job ready")},
                   {"text", T("Jetzt F9 drücken (Schnellladen)", "Now press F9 (quick load)")}}.dump().c_str());
}

void WaitForQuickSave() {
    static fs::path candidate; static uintmax_t lastSize = 0; static fs::file_time_type lastTime{}; static DWORD stableSince = 0;
    for (auto& s : AllSaves(L"quicksave")) {
        if (s.time <= ui.requestedAt) continue;
        if (s.path != candidate || s.size != lastSize || s.time != lastTime) { candidate = s.path; lastSize = s.size; lastTime = s.time; stableSince = GetTickCount(); return; }
        if (GetTickCount() - stableSince >= 1500) { candidate.clear(); Apply(s.path); }   // the game has finished writing
        return;
    }
}

void OnEvent(const json& e) {
    const std::string type = e.value("type", std::string()), id = e.value("id", std::string());
    if (type == "open") { if (ui.screen != "wait" && ui.screen != "done") { std::lock_guard<std::mutex> l(g_dataMtx); g_data.reset(); } if (!g_data) LoadNewest(); ui.dirty = true; return; }
    if (type == "text") { ui.search = e.value("text", std::string()); ui.dirty = true; return; }
    if (type == "back") {
        if (ui.screen == "fromCompany") Go("fromCity");
        else if (ui.screen == "toCompany") Go("toCity");
        else Go("main");
        return;
    }
    if (type != "click") return;
    if (id == "reload") { { std::lock_guard<std::mutex> l(g_dataMtx); g_data.reset(); } LoadNewest(); ui.dirty = true; }
    else if (id == "cargo") Go("cargo");
    else if (id == "from") Go("fromCity");
    else if (id == "to") Go("toCity");
    else if (id.rfind("c:", 0) == 0) { ui.cargo = id.substr(2); Go("main"); }
    else if (id.rfind("city:", 0) == 0) { ui.cityPick = id.substr(5); Go(ui.screen == "fromCity" ? "fromCompany" : "toCompany"); }
    else if (id.rfind("co:", 0) == 0) { (ui.screen == "fromCompany" ? ui.from : ui.to) = id.substr(3); Go("main"); }
    else if (id == "go") {
        ui.requestedAt = fs::file_time_type::clock::now() - std::chrono::seconds(2);
        // compare with the file times of the quick saves that exist now, so only a new F5 counts
        for (auto& s : AllSaves(L"quicksave")) if (s.time > ui.requestedAt) ui.requestedAt = s.time;
        Go("wait");
        Log("waiting for a quick save (F5): " + ui.cargo + " " + ui.from + " -> " + ui.to);
    }
    else if (id == "cancel" || id == "again") Go("main");
}

DWORD WINAPI Worker(LPVOID) {
    bool found = false;
    for (int i = 0; i < 600 && !g_quit && !found; ++i) { found = rr_addon_load(&rr) != 0; if (!found) Sleep(500); }
    if (!found) return 0;   // Realistic Radio older than 1.7.0 or missing
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // the icon picture is read with WIC
    Log("add-on " JOBS_VERSION " started");
    DWORD lastReg = 0;
    std::vector<char> buf(64 * 1024);
    bool wasLoading = false;
    while (!g_quit) {
        DWORD now = GetTickCount();
        if (now - lastReg >= 1000) {
            lastReg = now;
            Nearest();   // language
            rr.app_register(json{{"id", kId}, {"name", "Aufträge+"}, {"name_en", "Jobs+"}, {"mode", "list"}, {"color", "#FF9F0A"}}.dump().c_str());
        }
        int n = rr.app_events(kId, buf.data(), (int)buf.size());
        if (n < 0) { buf.resize((size_t)-n + 1024); n = rr.app_events(kId, buf.data(), (int)buf.size()); }
        if (n > 0) {
            json evs = json::parse(std::string(buf.data(), n), nullptr, false);
            if (evs.is_array()) for (auto& e : evs) OnEvent(e);
        }
        if (ui.screen == "wait") WaitForQuickSave();
        bool loading = g_loading;
        if (loading != wasLoading) { wasLoading = loading; ui.dirty = true; }
        if (ui.dirty) { ui.dirty = false; Show(); }
        Sleep(ui.screen == "wait" ? 250 : 50);
    }
    CoUninitialize();
    return 0;
}
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    g_ats = p->common.game_id && strcmp(p->common.game_id, "ats") == 0;
    if (g_thread) return SCS_RESULT_ok;
    g_quit = false;
    g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    p->common.log(SCS_LOG_TYPE_message, "[realistic-radio-jobs] Realistic Radio Jobs+ " JOBS_VERSION " loaded (needs Realistic Radio 1.7.0 or newer)");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    g_quit = true;
    if (g_thread) { WaitForSingleObject(g_thread, 5000); CloseHandle(g_thread); g_thread = nullptr; }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
