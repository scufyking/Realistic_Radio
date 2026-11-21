// Realistic Radio Bus+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "BusLogic.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>

namespace {
unsigned Num(const std::string& s) { return s.empty() ? 0 : (unsigned)strtoull(s.c_str(), nullptr, 10); }
unsigned Round5(double m) { return (unsigned)std::max(5.0, std::round(m / 5.0) * 5.0); }
}

std::string StopOfUnit(const std::string& u) { return u.rfind("bus_stop.", 0) == 0 ? u.substr(9) : u; }

unsigned BusJob::PlannedDeparture(size_t i) const {
    if (i >= stages.size()) return kNil;
    const BusStage& s = stages[i];
    if (s.plannedDeparture != kNil) return s.plannedDeparture;
    if (s.departure != kNil) return s.departure;
    if (i == 0) return kNil;
    unsigned a = PlannedArrival(i - 1);
    return a == kNil ? kNil : a + 15;   // the game plans 15 minutes at a stop
}
unsigned BusJob::PlannedArrival(size_t i) const {
    if (i >= stages.size()) return kNil;
    const BusStage& s = stages[i];
    if (s.plannedArrival != kNil) return s.plannedArrival;
    unsigned dep = PlannedDeparture(i);
    if (dep == kNil) return kNil;
    return dep + s.stageTime + s.ferryTime;
}

bool ReadBus(const SiiFile& f, BusData& d) {
    d = BusData();
    for (const SiiUnit* e : f.OfClass("economy")) d.gameTime = e->U("game_time");
    for (const SiiUnit* s : f.OfClass("bus_stop")) d.stops[StopOfUnit(s->id)] = (int)s->A("lines_offer").size();
    if (d.stops.empty()) return false;
    for (const SiiUnit* o : f.OfClass("line_offer_data")) {
        ++d.offers;
        std::string prev = StopOfUnit(o->S("source"));
        auto t = o->A("saved_targets"), km = o->A("targets_distances"), tm = o->A("targets_times"), fe = o->A("targets_ferry_times"), ba = o->A("targets_base_times");
        for (size_t i = 0; i < t.size(); ++i) {
            BusLeg l;
            l.km = i < km.size() ? Num(km[i]) : 0; l.time = i < tm.size() ? Num(tm[i]) : 0;
            l.ferry = i < fe.size() ? Num(fe[i]) : 0; l.base = i < ba.size() ? Num(ba[i]) : 0;
            if (l.km && prev != t[i]) d.legs[{prev, t[i]}] = l;
            prev = t[i];
        }
    }
    // the bus job being driven
    std::string jobId;
    for (const SiiUnit* p : f.OfClass("player")) jobId = p->S("current_bus_job");
    if (const SiiUnit* j = (jobId.empty() || jobId == "null") ? nullptr : f.Unit(jobId)) {
        BusJob& job = d.job;
        job.active = true;
        job.type = j->S("job_type");
        job.stageIdx = j->U("stage_idx");
        for (auto& sid : j->A("stages")) {
            const SiiUnit* s = f.Unit(sid);
            if (!s) continue;
            BusStage st;
            st.from = StopOfUnit(s->S("source_bus_stop")); st.to = StopOfUnit(s->S("target_bus_stop"));
            st.distance = s->U("planned_distance");
            st.plannedDeparture = s->U("planned_departure_time", kNil); st.plannedArrival = s->U("planned_arrival_time", kNil);
            st.stageTime = s->U("planned_stage_time"); st.ferryTime = s->U("planned_ferry_time"); st.baseTime = s->U("base_time");
            st.departure = s->U("departure_time", kNil); st.arrival = s->U("arrival_time", kNil);
            st.travelled = s->U("travelled_distance");
            st.enterPlanned = s->U("planned_entering_passenger_count"); st.leavePlanned = s->U("planned_leaving_passenger_count");
            st.entered = s->U("entering_passenger_count", kNil); st.left = s->U("leaving_passenger_count", kNil);
            st.satisfaction = s->D("satisfaction", -1);
            job.stages.push_back(st);
        }
        double sum = 0; unsigned n = 0;
        for (auto& pid : j->A("passengers")) if (const SiiUnit* p = f.Unit(pid)) { sum += p->D("satisfaction", 0); ++n; }
        job.passengers = n;
        job.satisfaction = n ? sum / n : -1;
        if (job.stages.empty()) job.active = false;
    }
    for (const SiiUnit* l : f.OfClass("bus_job_log"))
        for (auto& eid : l->A("entries"))
            if (const SiiUnit* e = f.Unit(eid)) {
                BusLogEntry x;
                x.type = e->S("type"); x.from = e->S("source_bus_stop_name"); x.to = e->S("target_bus_stop_name"); x.bus = e->S("assigned_bus");
                x.stages = e->U("stage_count"); x.km = e->U("travelled_distance"); x.start = e->U("start_time"); x.end = e->U("end_time");
                x.passengers = e->U("total_passengers"); x.reward = (long long)strtoll(e->S("reward").c_str(), nullptr, 10);
                x.satisfaction = e->D("satisfaction", -1);
                d.log.push_back(x);
            }
    return true;
}

// ---- legs
void LegEstimator::Init(const std::map<std::pair<std::string, std::string>, BusLeg>& legs, CoordFn coord) {
    legs_ = legs; coord_ = coord; next_.clear();
    for (auto& kv : legs_) next_[kv.first.first].push_back({kv.first.second, kv.second});
    // ratios from the known legs without ferry
    std::vector<double> ku, tk, bk;
    for (auto& kv : legs_) {
        const BusLeg& l = kv.second;
        if (l.ferry || l.km < 40) continue;
        tk.push_back((double)l.time / l.km);
        bk.push_back((double)l.base / l.km);
        double ax, az, bx, bz;
        if (coord_ && coord_(kv.first.first, ax, az) && coord_(kv.first.second, bx, bz)) {
            double e = std::hypot(ax - bx, az - bz);
            if (e > 1000) ku.push_back(l.km / e);
        }
    }
    auto median = [](std::vector<double>& v, double def) { if (v.empty()) return def; std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
    timePerKm = median(tk, 1.04); basePerKm = median(bk, 0.83); kmPerUnit = median(ku, 0);
}

// shortest way over known legs (both directions), by km
bool LegEstimator::Route(const std::string& from, const std::string& to, BusLeg& out) const {
    std::map<std::string, std::vector<std::pair<std::string, BusLeg>>> g = next_;
    for (auto& kv : legs_) {
        auto& v = g[kv.first.second];
        bool have = false;
        for (auto& e : v) if (e.first == kv.first.first) { have = true; break; }
        if (!have) v.push_back({kv.first.first, kv.second});
    }
    std::map<std::string, std::pair<unsigned, BusLeg>> best;   // stop -> km, summed leg
    using Item = std::pair<unsigned, std::string>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
    best[from] = {0, BusLeg()};
    q.push({0, from});
    while (!q.empty()) {
        auto [km, s] = q.top(); q.pop();
        if (km != best[s].first) continue;
        if (s == to) { out = best[s].second; return true; }
        auto it = g.find(s);
        if (it == g.end()) continue;
        for (auto& e : it->second) {
            unsigned nk = km + e.second.km;
            auto b = best.find(e.first);
            if (b != best.end() && b->second.first <= nk) continue;
            BusLeg sum = best[s].second;
            sum.km += e.second.km; sum.time += e.second.time; sum.ferry += e.second.ferry; sum.base += e.second.base;
            best[e.first] = {nk, sum};
            q.push({nk, e.first});
        }
    }
    return false;
}

BusLeg LegEstimator::Get(const std::string& from, const std::string& to, int& quality) const {
    auto it = legs_.find({from, to});
    if (it != legs_.end()) { quality = 0; return it->second; }
    it = legs_.find({to, from});
    if (it != legs_.end()) { quality = 1; return it->second; }
    double ax, az, bx, bz, straight = 0;
    bool map = coord_ && kmPerUnit > 0 && coord_(from, ax, az) && coord_(to, bx, bz);
    if (map) straight = std::max(5.0, std::hypot(ax - bx, az - bz) * kmPerUnit);
    BusLeg r;
    if (Route(from, to, r) && (!map || r.km <= straight * 1.4)) {
        // over other stops: a little shorter than the sum (no detour through the towns)
        quality = 2;
        if (map && r.km > straight) {
            double k = std::max(straight, r.km * 0.95) / r.km;
            r.km = (unsigned)std::round(r.km * k); r.base = (unsigned)std::round(r.base * k);
            r.time = Round5(r.time * k);
        }
        return r;
    }
    if (map) {
        quality = 3;
        r = BusLeg();
        r.km = (unsigned)std::round(straight);
        r.time = Round5(r.km * timePerKm);
        r.base = (unsigned)std::round(r.km * basePerKm);
        return r;
    }
    quality = 4;
    r = BusLeg(); r.km = 300; r.time = Round5(300 * timePerKm); r.base = (unsigned)(300 * basePerKm);
    return r;
}

// ---- own line into the save
bool ApplyLine(SiiFile& f, const LinePlan& plan, const LegEstimator& est, std::string& summary, std::string& error) {
    const SiiUnit* stop = f.Unit("bus_stop." + plan.source);
    if (!stop) { error = "stop not in the save: " + plan.source; return false; }
    std::vector<std::string> offers = stop->A("lines_offer");
    if (offers.empty()) { error = "no line offers at " + plan.source; return false; }
    if (plan.targets.empty()) { error = "no stops"; return false; }
    if (plan.tourist && (plan.targets.size() != 2 || plan.targets[1] != plan.source)) { error = "a tourist line goes to one viewpoint and back"; return false; }
    {
        std::string prev = plan.source;
        for (auto& t : plan.targets) {
            if (t == prev) { error = "the same stop twice in a row: " + t; return false; }
            if (!f.Unit("bus_stop." + t)) { error = "stop not in the save: " + t; return false; }
            prev = t;
        }
    }
    const std::string type = plan.tourist ? "tourist" : "regular";
    // the offer to replace: one of the same type (the last one: the list on the stop starts with the other type)
    size_t victim = offers.size();
    for (size_t i = offers.size(); i-- > 0;) if (const SiiUnit* o = f.Unit(offers[i])) if (o->S("type") == type) { victim = i; break; }
    if (victim == offers.size()) for (size_t i = 0; i < offers.size(); ++i) if (f.Unit(offers[i])) { victim = i; break; }
    if (victim == offers.size()) { error = "line offers of the stop are missing"; return false; }
    const SiiUnit* vo = f.Unit(offers[victim]);
    std::string busName = vo->S("bus_name");
    if (busName.empty())
        for (const SiiUnit* o : f.OfClass("line_offer_data")) if (!o->S("bus_name").empty() && o->S("type") == type) { busName = o->S("bus_name"); break; }

    unsigned gameTime = 0;
    for (const SiiUnit* e : f.OfClass("economy")) gameTime = e->U("game_time");
    const unsigned expiration = gameTime + 1440;   // one game day to take it

    std::vector<std::string> km, tm, fe, ba;
    std::string prev = plan.source;
    summary = type + " " + plan.source;
    for (auto& t : plan.targets) {
        int q = 0;
        BusLeg l = est.Get(prev, t, q);
        km.push_back(std::to_string(l.km)); tm.push_back(std::to_string(l.time)); fe.push_back(std::to_string(l.ferry)); ba.push_back(std::to_string(l.base));
        summary += " -> " + t + " (" + std::to_string(l.km) + " km, " + std::to_string(l.time) + " min, q" + std::to_string(q) + ")";
        prev = t;
    }
    const std::string id = offers[victim];
    f.Set(id, "type", type);
    f.Set(id, "saved_targets", plan.targets, true);
    f.Set(id, "targets_distances", km);
    f.Set(id, "targets_times", tm);
    f.Set(id, "targets_ferry_times", fe);
    f.Set(id, "targets_base_times", ba);
    f.Set(id, "departure_time_of_day", std::to_string(plan.departure % 1440));
    f.Set(id, "bus_name", busName, true);
    f.Set(id, "expiration_time", std::to_string(expiration));
    f.Set(id, "tight_schedule_rank", "0");
    f.Set(id, "satisfaction_decrease_rank", "0");
    f.Set(id, "low_acceleration_rank", "0");
    summary += ", departure " + std::to_string(plan.departure % 1440) + ", offer " + id + " (#" + std::to_string(victim) + ")";

    // the offer's expiry event (economy_event: unit_link = the stop, param = index of the offer) moves to the new expiry;
    // the event queue is sorted by time, so the event moves there too
    const std::string stopUnit = "bus_stop." + plan.source;
    std::string eventId;
    std::map<std::string, unsigned> eventTime;
    for (const SiiUnit* e : f.OfClass("economy_event")) {
        eventTime[e->id] = e->U("time");
        if (e->S("unit_link") == stopUnit && e->U("param", kNil) == victim) eventId = e->id;
    }
    if (!eventId.empty()) {
        f.Set(eventId, "time", std::to_string(expiration));
        eventTime[eventId] = expiration;
        for (const SiiUnit* qu : f.OfClass("economy_event_queue")) {
            std::vector<std::string> data = qu->A("data");
            auto it = std::find(data.begin(), data.end(), eventId);
            if (it == data.end()) continue;
            data.erase(it);
            size_t pos = data.size();
            for (size_t i = 0; i < data.size(); ++i) {
                auto t = eventTime.find(data[i]);
                if (t != eventTime.end() && t->second > expiration) { pos = i; break; }
            }
            data.insert(data.begin() + pos, eventId);
            f.Set(qu->id, "data", data);
        }
        summary += ", event " + eventId;
    } else summary += ", no expiry event found";
    if (!f.Commit(error)) return false;
    return true;
}
