// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#pragma once
#include "Common.h"

struct City { std::string gameName, realName, country; double x, z; double relRadius = 1, relWhitenoise = 1; };
struct Station { std::string name, url, logo; double relativeRadius = -1; };
struct CountryInfo { std::string name, nativeName; double relativeRadius = 1; };

class StationDatabase {
public:
    bool Load(const std::wstring& file);
    void LoadCustom(const std::wstring& file);   // custom-stations.json, same idea as stations_custom in the original custom.js
    std::vector<City> cities;
    std::map<std::string, CountryInfo> countries;
    std::map<std::string, std::vector<Station>> stations;
    double CountryRadius(const std::string& c) const { auto it = countries.find(c); return it == countries.end() ? 1.0 : it->second.relativeRadius; }
    std::string CountryDisplayName(const std::string& c) const { auto it = countries.find(c); return it == countries.end() ? c : (it->second.nativeName.empty() ? it->second.name : it->second.nativeName); }
    const std::vector<Station>* StationsOf(const std::string& c) const { auto it = stations.find(c); return it == stations.end() || it->second.empty() ? nullptr : &it->second; }
};

// favourites.json : { "<country>": "<station name>" }  (same semantics as the original Favourites.cs)
class Favourites {
public:
    void Load(const std::wstring& file);
    void Set(const std::string& country, const std::string& name);
    std::string Get(const std::string& country) const;
    bool Has(const std::string& country) const;
private:
    std::wstring file_;
    std::map<std::string, std::string> fav_;
};
