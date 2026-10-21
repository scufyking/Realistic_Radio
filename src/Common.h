// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>

#define REALISTIC_RADIO_VERSION "1.6.0"
#define REALISTIC_RADIO_VERSION_W L"1.6.0"

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
std::wstring PluginDataDir();   // <folder of DLL>\realistic-radio\  (with trailing backslash)

// Current job from the telemetry (configuration "job"); active = there is a job with a delivery window
struct JobInfo {
    bool active = false, loaded = true, special = false;
    unsigned deliveryTime = 0;   // absolute game time (minutes) when the delivery window ends
    std::string city, company, cargo;
    std::string cityId, sourceCity, sourceCityId, sourceCompany;
    long long income = 0;
    bool operator==(const JobInfo& o) const { return active == o.active && loaded == o.loaded && special == o.special && deliveryTime == o.deliveryTime && city == o.city && company == o.company && cargo == o.cargo &&
                                                     cityId == o.cityId && sourceCity == o.sourceCity && sourceCompany == o.sourceCompany && income == o.income; }
};
// Gameplay event from the telemetry (job delivered / cancelled, fine, ...)
struct GameEvent {
    std::string id;                                   // job.delivered | job.cancelled | player.fined | ...
    long long amount = 0;                             // revenue / penalty / fine
    std::string text;                                 // fine: offence
};
