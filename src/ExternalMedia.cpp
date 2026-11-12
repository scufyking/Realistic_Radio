// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ExternalMedia.h"
#include "Logger.h"
#include "Common.h"
#include <deque>
#include <mutex>
#include <cmath>

namespace {
std::mutex g_mtx;
ExternalMedia::State g_s;
ULONGLONG g_heard = 0, g_posAt = 0;
std::deque<int> g_cmds;
constexpr ULONGLONG kAlive = 5000;   // the add-on reports at least every second
}

namespace ExternalMedia {
State Get() {
    std::lock_guard<std::mutex> l(g_mtx);
    State s = g_s;
    ULONGLONG now = GetTickCount64();
    s.addon = g_heard && now - g_heard < kAlive;
    if (!s.addon) { s.session = false; s.status = None; }
    if (s.status == Playing && s.pos >= 0) {
        s.pos += (now - g_posAt) / 1000.0;
        if (s.len > 0 && s.pos > s.len) s.pos = s.len;
    }
    return s;
}
void Register(const std::wstring& name) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (!g_heard || g_s.addonName != name) Log::Write("Phone media: add-on '%s' connected", WideToUtf8(name).c_str());
    g_s.addonName = name; g_heard = GetTickCount64();
}
void Set(const std::wstring& app, const std::wstring& title, const std::wstring& artist, const std::wstring& album, int status, long long posMs, long long lenMs, int caps) {
    std::lock_guard<std::mutex> l(g_mtx);
    g_heard = GetTickCount64();
    bool track = title != g_s.title || artist != g_s.artist || app != g_s.app;
    bool changed = track || !g_s.session || status != g_s.status || caps != g_s.caps || album != g_s.album || (lenMs >= 0 ? lenMs / 1000.0 : -1) != g_s.len;
    g_s.session = true; g_s.app = app; g_s.title = title; g_s.artist = artist; g_s.album = album; g_s.status = status; g_s.caps = caps;
    g_s.len = lenMs > 0 ? lenMs / 1000.0 : -1;
    double pos = posMs >= 0 ? posMs / 1000.0 : -1;
    if (pos >= 0 && std::abs(pos - g_s.pos - (status == Playing ? (g_heard - g_posAt) / 1000.0 : 0)) > 1.5) changed = true;   // seeked
    g_s.pos = pos; g_posAt = g_heard;
    if (track) {
        ++g_s.trackSerial;
        g_s.cover.reset();   // the new cover comes separately
        Log::Write("Phone media: %s - %s (%s)", WideToUtf8(artist).c_str(), WideToUtf8(title).c_str(), WideToUtf8(app).c_str());
    }
    if (changed) ++g_s.serial;
}
void SetCover(std::shared_ptr<OverlayImage> cover) {
    std::lock_guard<std::mutex> l(g_mtx);
    g_heard = GetTickCount64();
    g_s.cover = std::move(cover);
    ++g_s.coverSerial; ++g_s.serial;
}
void Clear() {
    std::lock_guard<std::mutex> l(g_mtx);
    g_heard = GetTickCount64();
    if (!g_s.session) return;
    g_s.session = false; g_s.status = None; g_s.title.clear(); g_s.artist.clear(); g_s.album.clear(); g_s.app.clear(); g_s.cover.reset();
    g_s.pos = g_s.len = -1;
    ++g_s.serial; ++g_s.trackSerial;
}
int TakeCommand() {
    std::lock_guard<std::mutex> l(g_mtx);
    g_heard = GetTickCount64();
    if (g_cmds.empty()) return CmdNone;
    int c = g_cmds.front(); g_cmds.pop_front();
    return c;
}
void Command(int cmd) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_cmds.size() < 8) g_cmds.push_back(cmd);
}
}
