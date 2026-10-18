// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "StationDatabase.h"
#include <set>
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
            for (auto& s : arr)
                stations[k].push_back(Station{s.value("name", ""), s.value("url", ""), s.value("logo", ""), s.value("relative_radius", -1.0),
                                              (int)std::lround(s.value("frequency", 0.0) * 10)});
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
    constexpr int kLo = 876, kHi = 1079, kN = kHi - kLo + 1;   // 87.6 - 107.9 MHz
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
    int planned = 0, shared = 0;
    for (auto& kv : stations) {
        const std::string& region = kv.first;
        auto& list = kv.second;
        auto& mine = U(region);
        for (auto& s : list) {
            if (s.freq >= kLo && s.freq <= kHi) continue;
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
    Log::Write("FM frequencies: %d planned, %d shared with a neighbouring region", planned, shared);
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
