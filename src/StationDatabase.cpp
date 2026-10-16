// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "StationDatabase.h"
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
                stations[k].push_back(Station{s.value("name", ""), s.value("url", ""), s.value("logo", ""), s.value("relative_radius", -1.0)});
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
                Station st{s.value("name", ""), s.value("url", ""), s.value("logo", ""), s.value("relative_radius", -1.0)};
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
