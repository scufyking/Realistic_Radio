// Realistic Radio Bus+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// What a save tells about buses (Coaches DLC): bus stops, the line offers ("line_offer_data"), the bus job that is being
// driven ("player_bus_job" with its stages) and the bus job log - and how to put an own line into a save.
// Stops are named like the game names them in line offers: "city.berlin", "viewpoints.deberlin".
#include "SiiFile.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

const unsigned kNil = 4294967295u;   // "not yet" in the save

struct BusLeg { unsigned km = 0, time = 0, ferry = 0, base = 0; };   // minutes: time = planned stage time, base = pure driving time

struct BusStage {
    std::string from, to;
    unsigned distance = 0, plannedDeparture = kNil, plannedArrival = kNil, stageTime = 0, ferryTime = 0, baseTime = 0;
    unsigned departure = kNil, arrival = kNil, travelled = 0;
    unsigned enterPlanned = 0, leavePlanned = 0, entered = kNil, left = kNil;
    double satisfaction = -1;
};

struct BusJob {
    bool active = false;
    std::string type;                  // regular | tourist
    std::vector<BusStage> stages;
    unsigned stageIdx = 0;
    unsigned passengers = 0;           // on board
    double satisfaction = -1;          // mean of the passengers on board, 0..1
    // planned arrival at stage i: the save only knows the departure of a stage once it started; the rest is chained
    unsigned PlannedArrival(size_t i) const;
    unsigned PlannedDeparture(size_t i) const;
};

struct BusLogEntry {
    std::string type, from, to, bus;
    unsigned stages = 0, km = 0, start = 0, end = 0, passengers = 0;
    long long reward = 0;
    double satisfaction = -1;
};

struct BusData {
    unsigned gameTime = 0;
    std::map<std::string, int> stops;                                   // stop -> number of line offers there
    std::map<std::pair<std::string, std::string>, BusLeg> legs;        // known legs (from the line offers)
    BusJob job;
    std::vector<BusLogEntry> log;                                       // newest last
    size_t offers = 0;
};

std::string StopOfUnit(const std::string& busStopUnit);   // "bus_stop.city.berlin" -> "city.berlin"
bool ReadBus(const SiiFile& f, BusData& d);                // false: no bus stops in the save (no Coaches DLC)

// Distance and times of a leg: known from the save, else from the way back, else over known legs, else from the map.
// quality: 0 known, 1 known the other way, 2 over other stops, 3 from the map (straight line), 4 unknown (guess)
class LegEstimator {
public:
    using CoordFn = std::function<bool(const std::string& stop, double& x, double& z)>;
    void Init(const std::map<std::pair<std::string, std::string>, BusLeg>& legs, CoordFn coord);
    BusLeg Get(const std::string& from, const std::string& to, int& quality) const;
    double kmPerUnit = 0, timePerKm = 1.04, basePerKm = 0.83;
private:
    bool Route(const std::string& from, const std::string& to, BusLeg& out) const;
    std::map<std::pair<std::string, std::string>, BusLeg> legs_;
    std::map<std::string, std::vector<std::pair<std::string, BusLeg>>> next_;
    CoordFn coord_;
};

struct LinePlan {
    std::string source;                // "city.innsbruck"
    bool tourist = false;              // tourist: targets = {viewpoint, source}
    std::vector<std::string> targets;
    unsigned departure = 480;          // minute of the day
};

// Puts the line into the save as one of the source stop's line offers (the offer and its expiry event are changed;
// nothing else). The offer is valid for one game day. summary: what was written (for the log).
bool ApplyLine(SiiFile& f, const LinePlan& plan, const LegEstimator& est, std::string& summary, std::string& error);
