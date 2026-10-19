// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Station selection, reception and favourite logic derived from MPL-2.0 licensed code (see NOTICE.md).
#pragma once
#include "StationDatabase.h"

struct CountryReception { std::string country; double distance; double whitenoise; };

struct ReceptionResult {
    bool valid = false;
    std::string nearestCity, nearestCountry, bestCountry;
    double nearestDistance = 0;
    std::map<std::string, CountryReception> available;   // sorted by key, like sortObject() in main.js
    std::vector<std::string> insertionOrder;              // JS object key order before sortObject(): decides ties like the original
};

// 1:1 port of refresh() from web/lib/js/main.js (distance in the X/Z plane)
ReceptionResult CalculateReception(const StationDatabase& db, double x, double z, double radius);
// Port of calculateReception(): 0..5 bars
int ReceptionBars(double whitenoise);
