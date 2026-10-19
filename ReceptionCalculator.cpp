// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#include "ReceptionCalculator.h"
#include <cmath>

ReceptionResult CalculateReception(const StationDatabase& db, double x, double z, double radius) {
    ReceptionResult r;
    if (x == 0.0 && z == 0.0) return r;    // original: position 0,0,0 means "not connected"
    double lowest = 1e300; const City* nearest = nullptr;
    for (const City& c : db.cities) {
        double d = std::sqrt((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z));
        if (d < lowest) { lowest = d; nearest = &c; }
        double countryR = db.CountryRadius(c.country);
        if (d < radius * countryR * c.relRadius) {
            double w = d / radius / countryR / c.relRadius * c.relWhitenoise;
            auto it = r.available.find(c.country);
            if (it == r.available.end()) { r.available[c.country] = {c.country, d, w}; r.insertionOrder.push_back(c.country); }
            else if (it->second.whitenoise > w) { it->second.whitenoise = w; it->second.distance = d; }
        }
    }
    if (!nearest) return r;
    r.valid = true;
    r.nearestCity = nearest->gameName; r.nearestCountry = nearest->country; r.nearestDistance = lowest;
    if (!r.available.count(nearest->country)) {
        double w = lowest / radius / db.CountryRadius(nearest->country) / nearest->relRadius * nearest->relWhitenoise;
        r.available[nearest->country] = {nearest->country, lowest, w};
        r.insertionOrder.push_back(nearest->country);
    }
    double lw = 999999;
    // same loop as main.js: "<=" over keys in insertion order -> on equal reception the LAST inserted country wins
    for (auto& k : r.insertionOrder) { const auto& v = r.available[k]; if (v.whitenoise <= lw) { lw = v.whitenoise; r.bestCountry = k; } }
    return r;
}

int ReceptionBars(double whitenoise) {
    double rec = whitenoise * whitenoise - 0.15;
    if (rec < 0.05) return 5; if (rec < 0.20) return 4; if (rec < 0.35) return 3;
    if (rec < 0.50) return 2; if (rec < 0.75) return 1; return 0;
}
