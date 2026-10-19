// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "TunnelMap.h"
#include "EmbeddedData.h"
#include "Logger.h"
#include "json.hpp"
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <cmath>

using json = nlohmann::json;

namespace {
double DistToSegment(double px, double pz, double ax, double az, double bx, double bz) {
    double dx = bx - ax, dz = bz - az, len2 = dx * dx + dz * dz;
    double t = len2 <= 1e-9 ? 0 : ((px - ax) * dx + (pz - az) * dz) / len2;
    t = std::clamp(t, 0.0, 1.0);
    double ex = px - (ax + t * dx), ez = pz - (az + t * dz);
    return std::sqrt(ex * ex + ez * ez);
}
// "t": [[ax, az, bx, bz, hw], ...] (embedded / own file) or Tunnel Radio's own list [{"Ax":..,"Az":..,"Bx":..,"Bz":..,"HalfWidth":..}]
template <class Seg> void Parse(const json& j, std::vector<Seg>& out) {
    const json& arr = j.is_object() && j.contains("t") ? j["t"] : j;
    if (!arr.is_array()) return;
    for (auto& e : arr) {
        if (e.is_array() && e.size() >= 5)
            out.push_back({e[0].get<float>(), e[1].get<float>(), e[2].get<float>(), e[3].get<float>(), e[4].get<float>()});
        else if (e.is_object())
            out.push_back({e.value("Ax", 0.f), e.value("Az", 0.f), e.value("Bx", 0.f), e.value("Bz", 0.f), e.value("HalfWidth", 12.f)});
    }
}
}

void TunnelMap::Init(const std::wstring& dataDir, const std::string& game) {
    dir_ = dataDir; game_ = game;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        gameLog_ = std::wstring(docs) + (game == "ats" ? L"\\American Truck Simulator\\game.log.txt" : L"\\Euro Truck Simulator 2\\game.log.txt");
        CoTaskMemFree(docs);
    }
    LoadDb(DetectProMods());
    LoadOwn();
}

void TunnelMap::LoadDb(bool promods) {
    int want = promods ? 1 : 0;
    if (game_ != "ets2") { segs_.clear(); db_ = want; return; }   // the databases cover ETS2 only
    if (want == db_) return;
    const char* data = nullptr; size_t size = 0;
    segs_.clear();
    if (EmbeddedResource(promods ? 107 : 106, data, size)) {
        try { Parse(json::parse(data, data + size), segs_); } catch (...) { segs_.clear(); }
    }
    db_ = want;
    Log::Write("Tunnels: %zu from the %s database (ETS2 Tunnel Radio, MIT)", segs_.size(), promods ? "ProMods 2.83 + Middle-East" : "ETS2 1.60 + map DLCs");
}

void TunnelMap::LoadOwn() {
    own_.clear();
    std::ifstream f{std::filesystem::path(dir_ + L"tunnels-own.json")};
    if (!f) return;
    try { Parse(json::parse(f, nullptr, true, true), own_); } catch (...) { Log::Write("tunnels-own.json could not be read"); }
    if (!own_.empty()) Log::Write("Tunnels: %zu own (tunnels-own.json)", own_.size());
}

// game.log.txt lists the mounted mods when a profile is loaded (the same check as ETS2 Tunnel Radio)
bool TunnelMap::DetectProMods() {
    if (gameLog_.empty()) return false;
    HANDLE h = CreateFileW(gameLog_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return db_ == 1;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    logSize_ = sz.QuadPart;
    auto read = [&](long long from, DWORD n) {
        std::string s(n, '\0'); LARGE_INTEGER p; p.QuadPart = from;
        DWORD got = 0;
        if (SetFilePointerEx(h, p, nullptr, FILE_BEGIN) && ReadFile(h, &s[0], n, &got, nullptr)) s.resize(got); else s.clear();
        return s;
    };
    long long tail = std::min<long long>(sz.QuadPart, 1024 * 1024);
    std::string text = read(0, (DWORD)std::min<long long>(sz.QuadPart, 64 * 1024)) + read(sz.QuadPart - tail, (DWORD)tail);
    CloseHandle(h);
    return text.find("promods-me-defmap") != std::string::npos || text.find("promods-eu-map") != std::string::npos ||
           text.find("promods-map") != std::string::npos;
}

void TunnelMap::Tick() {
    ULONGLONG now = GetTickCount64();
    if (now < nextCheck_) return;
    nextCheck_ = now + 20000;
    if (gameLog_.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(gameLog_.c_str(), GetFileExInfoStandard, &a)) return;
    long long size = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    if (size == logSize_) return;   // nothing new in the log
    LoadDb(DetectProMods());
}

bool TunnelMap::Inside(double x, double z) const {
    for (const Seg& s : segs_) if (DistToSegment(x, z, s.ax, s.az, s.bx, s.bz) <= s.hw) return true;
    for (const Seg& s : own_) if (DistToSegment(x, z, s.ax, s.az, s.bx, s.bz) <= s.hw) return true;
    return false;
}

std::wstring TunnelMap::Mark(double x, double z, bool en) {
    if (!haveEntry_) {
        haveEntry_ = true; entX_ = x; entZ_ = z;
        return en ? L"Tunnel entry saved - press again at the exit" : L"Tunnel-Anfang gespeichert – am Ende nochmal drücken";
    }
    haveEntry_ = false;
    double len = std::hypot(x - entX_, z - entZ_);
    if (len < 20 || len > 15000) return en ? L"Tunnel not saved (too short or too long)" : L"Tunnel nicht gespeichert (zu kurz oder zu lang)";
    own_.push_back({(float)entX_, (float)entZ_, (float)x, (float)z, 12.f});
    json j; j["t"] = json::array();
    for (const Seg& s : own_) j["t"].push_back({std::round(s.ax * 10) / 10, std::round(s.az * 10) / 10, std::round(s.bx * 10) / 10, std::round(s.bz * 10) / 10, s.hw});
    std::ofstream o{std::filesystem::path(dir_ + L"tunnels-own.json")}; o << j.dump(1);
    Log::Write("Tunnels: own tunnel saved (%.0f m)", len);
    wchar_t b[96]; swprintf(b, 96, en ? L"Tunnel saved (%.0f m)" : L"Tunnel gespeichert (%.0f m)", len);
    return b;
}
