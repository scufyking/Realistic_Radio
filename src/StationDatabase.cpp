// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "StationDatabase.h"
#include <set>
#include <sstream>
#include <cmath>
#include "Logger.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
using nlohmann::json;



bool StationDatabase::Load(const std::wstring& file) {
    std::ifstream f{std::filesystem::path(file), std::ios::binary};
    if (!f) { Log::Write("Data file not found: %s", WideToUtf8(file).c_str()); return false; }
    try {
        json j = json::parse(f);
        for (auto& [k, v] : j["countries"].items())
            countries[k] = CountryInfo{v.value("name", k), v.value("native_name", ""), v.value("relative_radius", 1.0)};
        auto& cp = j["city_properties"];
        for (auto& c : j["cities"]) {
            City city{c["gameName"].get<std::string>(), c.value("realName", ""), c["country"].get<std::string>(), c["x"].get<double>(), c["z"].get<double>()};
            if (cp.contains(city.gameName)) {
                city.relRadius = cp[city.gameName].value("relative_radius", 1.0);
                city.relWhitenoise = cp[city.gameName].value("relative_whitenoise", 1.0);
            }
            if (!countries.count(city.country)) countries[city.country] = CountryInfo{city.country, "", 1.0};
            cities.push_back(std::move(city));
        }
        for (auto& [k, arr] : j["stations"].items())
            for (auto& s : arr) {
                Station st{s.value("name", ""), s.value("url", ""), s.value("logo", ""), s.value("relative_radius", -1.0),
                           (int)std::lround(s.value("frequency", 0.0) * 10)};
                stations[k].push_back(std::move(st));
            }
        Log::Write("Loaded %zu cities, %zu countries, %zu station lists", cities.size(), countries.size(), stations.size());
        return !cities.empty();
    } catch (const std::exception& e) {
        Log::Write("Data file parse error: %s", e.what());
        return false;
    }
}

void StationDatabase::LoadCustom(const std::wstring& file) {
    std::ifstream f{std::filesystem::path(file)};
    if (!f) return;
    try {
        json j = json::parse(f, nullptr, true, true);   // comments allowed
        int added = 0;
        for (auto& [k, arr] : j.items()) {
            if (!arr.is_array()) continue;
            auto& list = stations[k];
            for (auto& s : arr) {
                Station st{s.value("name", ""), s.value("url", ""), s.value("logo", ""), s.value("relative_radius", -1.0),
                           (int)std::lround(s.value("frequency", 0.0) * 10)};   // optional: the real frequency, e.g. 94.7
                if (st.name.empty() || st.url.empty()) continue;
                bool dup = false;
                for (auto& e : list) if (e.name == st.name) { e = st; dup = true; break; }   // same name replaces the original entry
                if (!dup) list.push_back(st);
                ++added;
            }
        }
        Log::Write("Custom stations: %d entries from %s", added, WideToUtf8(file).c_str());
    } catch (const std::exception& e) {
        Log::Write("custom-stations.json parse error: %s", e.what());
    }
}

void StationDatabase::AssignFrequencies() {
    constexpr int kLo = 876, kHi = 1079, kN = kHi - kLo + 1;   // planned: 87.6 - 107.9 MHz
    constexpr int kFixLo = 875, kFixHi = 1080;                  // real frequencies (data, custom stations, station name): 87.5 - 108.0
    // neighbouring regions: cities closer than about two broadcast ranges
    std::map<std::string, std::vector<const City*>> byRegion;
    for (const City& c : cities) byRegion[c.country].push_back(&c);
    std::map<std::string, std::set<std::string>> nbr;
    const double d2 = 18000.0 * 18000.0;
    for (size_t i = 0; i < cities.size(); ++i)
        for (size_t k = i + 1; k < cities.size(); ++k) {
            const City &a = cities[i], &b = cities[k];
            if (a.country == b.country) continue;
            double dx = a.x - b.x, dz = a.z - b.z;
            if (dx * dx + dz * dz < d2) { nbr[a.country].insert(b.country); nbr[b.country].insert(a.country); }
        }
    std::map<std::string, std::vector<bool>> used;   // channels used per region
    auto U = [&](const std::string& r) -> std::vector<bool>& { auto& v = used[r]; if (v.empty()) v.assign(kN, false); return v; };
    for (auto& [region, list] : stations)   // fixed frequencies first
        for (auto& s : list) if (s.freq >= kLo && s.freq <= kHi) U(region)[s.freq - kLo] = true;
    int planned = 0, shared = 0, real = 0;
    for (auto& [region, list] : stations) for (auto& s : list) if (s.freq >= kFixLo && s.freq <= kFixHi) ++real;
    for (auto& kv : stations) {
        const std::string& region = kv.first;
        auto& list = kv.second;
        auto& mine = U(region);
        for (auto& s : list) {
            if (s.freq >= kFixLo && s.freq <= kFixHi) continue;   // real frequency: keep it
            uint32_t h = 2166136261u;   // FNV-1a: the same station always starts at the same channel
            for (char ch : region + "|" + s.name) { h ^= (uint8_t)ch; h *= 16777619u; }
            int start = (int)(h % kN), pick = -1;
            for (int pass = 0; pass < 4 && pick < 0; ++pass)
                for (int n = 0; n < kN && pick < 0; ++n) {
                    int c = (start + n * 7) % kN;   // step 0.7 MHz: spreads a region's stations over the band
                    if (mine[c]) continue;
                    if (pass <= 1 && ((c > 0 && mine[c - 1]) || (c + 1 < kN && mine[c + 1]))) continue;   // keep 0.2 MHz apart
                    bool neighbour = false;
                    if (pass == 0) for (auto& o : nbr[region]) if (U(o)[c]) { neighbour = true; break; }
                    if (neighbour) continue;
                    pick = c;
                }
            if (pick < 0) continue;   // band full (more than 204 stations in one region)
            for (auto& o : nbr[region]) if (U(o)[pick]) { ++shared; break; }
            mine[pick] = true;
            s.freq = kLo + pick;
            ++planned;
        }
    }
    Log::Write("FM frequencies: %d real, %d planned, %d shared with a neighbouring region", real, planned, shared);
}

namespace {
// "94.3", 94.3, "94,3 MHz" -> FM in 0.1 MHz; "560 AM", 560, "AM 560 kHz" -> AM in kHz. 0 = empty / not understood.
void ParseFreq(const json& v, int& fm, int& am) {
    fm = am = 0;
    double f = 0; bool amWord = false;
    if (v.is_number()) f = v.get<double>();
    else if (v.is_string()) {
        std::string t;
        for (char c : v.get<std::string>()) {
            if (c == ',') c = '.';
            if ((c >= '0' && c <= '9') || c == '.') t += c;
            else if (c == 'A' || c == 'a') amWord = true;   // "AM"
        }
        if (t.empty()) return;
        f = atof(t.c_str());
    } else return;
    if (!amWord && f >= 87.5 && f <= 108.0) fm = (int)std::lround(f * 10);
    else if (f >= 530 && f <= 1710) am = (int)std::lround(f);
}
std::string JsonStr(const std::string& s) { return json(s).dump(); }
}

// frequencies-<game>.json: the real frequencies of the stations, filled in by the players. Every station of the game
// is listed (new ones are added on the next start, entries are never removed); an entry with a frequency wins over
// everything else, empty entries keep the planned frequency.
void StationDatabase::LoadFrequencies(const std::wstring& file, const std::string& gameName) {
    std::map<std::string, std::vector<std::pair<std::string, json>>> have;   // region -> (station, value) in file order
    std::string old;
    {
        std::ifstream f{std::filesystem::path(file), std::ios::binary};
        if (f) {
            std::stringstream ss; ss << f.rdbuf(); old = ss.str();
            try {
                json j = json::parse(old, nullptr, true, true);   // comments allowed
                for (auto& [region, list] : j.items())
                    if (list.is_object()) for (auto& [name, v] : list.items()) have[region].push_back({name, v});
            } catch (const std::exception& e) {
                Log::Write("%s: parse error (%s) - the file is not changed, fix it or delete it", WideToUtf8(file).c_str(), e.what());
                return;
            }
        }
    }
    int fmN = 0, amN = 0;
    for (auto& [region, list] : have) {
        auto it = stations.find(region);
        if (it == stations.end()) continue;
        for (auto& [name, v] : list) {
            int fm, am; ParseFreq(v, fm, am);
            if (!fm && !am) continue;
            for (auto& st : it->second) if (st.name == name) {
                if (fm) { st.freq = fm; st.am = 0; ++fmN; } else { st.am = am; ++amN; }
            }
        }
    }
    // write the list back: every station, the values that were there, empty "" for the others
    std::string out =
        "// Realistic Radio - real frequencies of the stations (" + gameName + ")\n"
        "// Enter the frequency of a station: \"94.3\" (FM in MHz) or \"560 AM\" (AM in kHz). Empty \"\" = the plugin plans one.\n"
        "// Entries here win over everything else. New stations are added automatically on the next start.\n"
        "// Please send me your filled-in file (SCS forum / GitHub) - collected frequencies go into the plugin.\n"
        "// Echte Frequenzen der Sender: \"94.3\" (UKW in MHz) oder \"560 AM\" (AM in kHz) eintragen, leer \"\" = das Plugin plant eine.\n"
        "// Eintraege hier haben Vorrang. Neue Sender kommen beim naechsten Start dazu. Ausgefuellte Datei gern an mich schicken.\n"
        "// RU: Укажите реальные частоты станций: \"94.3\" (FM в МГц) или \"560 AM\" (AM в кГц), пусто \"\" = плагин подберёт частоту сам.\n"
        "// Записи здесь имеют приоритет. Новые станции добавятся при следующем запуске. Заполненный файл, пожалуйста, пришлите мне.\n"
        "{\n";
    bool firstRegion = true;
    auto regionBlock = [&](const std::string& region, const std::vector<std::pair<std::string, json>>& rows) {
        out += std::string(firstRegion ? "" : ",\n") + "  " + JsonStr(region) + ": {\n";
        firstRegion = false;
        for (size_t i = 0; i < rows.size(); ++i)
            out += "    " + JsonStr(rows[i].first) + ": " + rows[i].second.dump() + (i + 1 < rows.size() ? ",\n" : "\n");
        out += "  }";
    };
    for (auto& [region, list] : stations) {
        std::vector<std::pair<std::string, json>> rows;
        auto h = have.find(region);
        for (auto& st : list) {
            json v = "";
            if (h != have.end()) for (auto& [n, val] : h->second) if (n == st.name) { v = val; break; }
            rows.push_back({st.name, v});
        }
        if (h != have.end())   // entries for stations that are no longer in the list stay in the file
            for (auto& [n, val] : h->second) {
                bool known = false;
                for (auto& st : list) if (st.name == n) { known = true; break; }
                if (!known) rows.push_back({n, val});
            }
        if (!rows.empty()) regionBlock(region, rows);
    }
    for (auto& [region, rows] : have) if (!stations.count(region) && !rows.empty()) regionBlock(region, rows);
    out += "\n}\n";
    if (out != old) {
        std::ofstream o{std::filesystem::path(file), std::ios::binary};
        if (o) { o << out; Log::Write("Frequencies: %s written (all stations listed)", WideToUtf8(file).c_str()); }
    }
    Log::Write("Frequencies: %d FM and %d AM entered in %s", fmN, amN, WideToUtf8(file).c_str());
}

void Favourites::Load(const std::wstring& file) {
    file_ = file;
    std::ifstream f{std::filesystem::path(file)};
    if (!f) return;
    try { json j = json::parse(f); for (auto& [k, v] : j.items()) if (v.is_string()) fav_[k] = v.get<std::string>(); }
    catch (...) { Log::Write("favourites.json unreadable, ignoring"); }
}
void Favourites::Set(const std::string& country, const std::string& name) {
    fav_[country] = name;
    json j = json::object(); for (auto& [k, v] : fav_) j[k] = v;
    std::ofstream o{std::filesystem::path(file_)}; o << j.dump(2);
}
std::string Favourites::Get(const std::string& c) const { auto it = fav_.find(c); return it == fav_.end() ? "" : it->second; }
bool Favourites::Has(const std::string& c) const { auto it = fav_.find(c); return it != fav_.end() && !it->second.empty(); }
