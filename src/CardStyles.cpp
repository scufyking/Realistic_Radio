// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "CardStyles.h"
#include "OverlayImage.h"
#include "Logger.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

using nlohmann::json;

namespace {
std::mutex g_mtx;
std::wstring g_dir;
struct Entry {
    ULONGLONG checkedAt = 0;
    FILETIME fileTime{}, picTime{};
    std::wstring pic;                         // background picture (its time is watched as well)
    bool missingLogged = false;
    std::shared_ptr<const CardTheme> theme;   // nullptr: missing or broken
};
std::map<std::string, Entry> g_cache;

FILETIME TimeOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return FILETIME{};
    return d.ftLastWriteTime;
}
bool Same(const FILETIME& a, const FILETIME& b) { return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime; }

std::string ReadFile(const std::wstring& path) {
    std::ifstream f{std::filesystem::path(path), std::ios::binary};
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);   // BOM
    return s;
}

// "#RRGGBB", "#RGB" or [r, g, b] (0..255) -> 0..1
bool ParseColor(const json& v, float out[3]) {
    if (v.is_array() && v.size() >= 3) {
        for (int k = 0; k < 3; ++k) { if (!v[k].is_number()) return false; out[k] = std::clamp(v[k].get<float>() / 255.f, 0.f, 1.f); }
        return true;
    }
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() == 3) s = std::string{s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
    unsigned long c = std::stoul(s, nullptr, 16);
    out[0] = ((c >> 16) & 255) / 255.f; out[1] = ((c >> 8) & 255) / 255.f; out[2] = (c & 255) / 255.f;
    return true;
}

std::shared_ptr<const CardTheme> Load(const std::string& name, const std::wstring& file, const std::wstring& dir, std::wstring& picPath, FILETIME& picTime) {
    picPath.clear(); picTime = FILETIME{};
    std::string text = ReadFile(file);
    if (text.empty()) return nullptr;
    json j = json::parse(text, nullptr, false, true);   // no exceptions, // comments allowed
    if (j.is_discarded() || !j.is_object()) { Log::Write("Styles: '%s.json' is not valid JSON (missing comma or quote?) - modern is shown", name.c_str()); return nullptr; }
    std::string based = j.value("basedOn", std::string("modern"));
    if (based.rfind("style:", 0) == 0) based = "modern";   // no chains of own styles
    auto t = std::make_shared<CardTheme>(*CardThemeFor(based));
    t->background.reset();
    std::string layout = j.value("layout", t->layout);
    if (layout == "standard" || layout == "classic" || layout == "smart" || layout == "presets") t->layout = layout;
    else Log::Write("Styles: '%s.json': unknown layout '%s' (standard, classic, smart, presets)", name.c_str(), layout.c_str());
    if (j.contains("font") && j["font"].is_string()) t->font = Utf8ToWide(j["font"].get<std::string>());
    if (j.contains("glow") && j["glow"].is_number()) t->glow = std::clamp(j["glow"].get<float>(), 0.f, 1.f);
    if (j.contains("colors") && j["colors"].is_object()) {
        struct { const char* key; float* dst; } map[] = {
            {"background", t->bgTop}, {"backgroundTop", t->bgTop}, {"backgroundBottom", t->bgBottom}, {"accent", t->accent}, {"onAccent", t->chipText},
            {"text", t->text}, {"subtext", t->sub}, {"muted", t->muted}, {"tile", t->tile}, {"inactive", t->inactive}, {"icon", t->art},
            {"logoTile", t->logoTile}, {"lcd", t->lcd}, {"lcdGlow", t->lcdGlow}, {"lcdText", t->lcdText}};
        const json& c = j["colors"];
        bool bottom = c.contains("backgroundBottom");
        for (auto& m : map) {
            if (!c.contains(m.key)) continue;
            if (!ParseColor(c[m.key], m.dst)) Log::Write("Styles: '%s.json': colour '%s' is not #RRGGBB", name.c_str(), m.key);
        }
        if (!bottom && c.contains("background")) for (int k = 0; k < 3; ++k) t->bgBottom[k] = t->bgTop[k];   // one colour = no gradient
    }
    if (j.contains("backgroundImage") && j["backgroundImage"].is_string() && !j["backgroundImage"].get<std::string>().empty()) {
        std::wstring pic = Utf8ToWide(j["backgroundImage"].get<std::string>());
        if (!(pic.size() > 1 && pic[1] == L':')) pic = dir + pic;
        picPath = pic; picTime = TimeOf(pic);
        auto img = ShrinkImage(LoadImageFile(pic), 2048);
        if (img) t->background = img;
        else Log::Write("Styles: '%s.json': picture '%s' not found or not readable (PNG, JPG, BMP)", name.c_str(), WideToUtf8(pic).c_str());
        t->backgroundOpacity = std::clamp(j.value("backgroundOpacity", 1.f), 0.f, 1.f);
    }
    Log::Write("Styles: '%s' loaded (layout %s, based on %s%s)", name.c_str(), t->layout.c_str(), based.c_str(), t->background ? ", with picture" : "");
    return t;
}

const char* kTemplate =
    "// Realistic Radio - eigener Style / own style\r\n"
    "// DE: Datei kopieren, umbenennen (ohne _ am Anfang, z. B. mein-style.json) und anpassen. Danach im Spiel:\r\n"
    "//     Strg + 0 -> Navi-Bildschirm -> Design. Aenderungen an der Datei wirken sofort (nach ein paar Sekunden).\r\n"
    "// EN: Copy this file, rename it (without the _ at the start, e.g. my-style.json) and edit it. Then in the game:\r\n"
    "//     Ctrl + 0 -> Navigation screen -> Design. Changes to the file apply at once (after a few seconds).\r\n"
    "{\r\n"
    "  \"name\": \"Mein Style\",            // Name im Menue / name in the menu\r\n"
    "  \"layout\": \"smart\",               // standard | classic (altes LKW-Radio) | smart (Smart-Dashboard) | presets (Senderliste)\r\n"
    "  \"basedOn\": \"smart\",              // Farben dieses Designs als Start / start from the colours of: modern, ocean, sport, light, retro, classic, smart, presets\r\n"
    "  \"font\": \"Segoe UI\",              // jede installierte Schrift / any installed font\r\n"
    "  \"glow\": 0.06,                    // Leuchten in der Akzentfarbe 0 .. 1 / glow in the accent colour\r\n"
    "  \"colors\": {                      // nur was sich aendern soll / only what should change (#RRGGBB)\r\n"
    "    \"background\": \"#18191D\",\r\n"
    "    \"backgroundBottom\": \"#0B0C0E\",\r\n"
    "    \"accent\": \"#FFB300\",           // Quelle, Songtitel, Balken / source, song title, bars\r\n"
    "    \"onAccent\": \"#141210\",         // Schrift auf der Akzentfarbe / text on the accent colour\r\n"
    "    \"text\": \"#F7F7FA\",\r\n"
    "    \"subtext\": \"#CCCED6\",\r\n"
    "    \"muted\": \"#9499A6\",\r\n"
    "    \"tile\": \"#212226\",             // Flaechen, Tasten / panels, buttons\r\n"
    "    \"logoTile\": \"#F2F2F5\",         // hinter den Senderlogos / behind the station logos\r\n"
    "    \"inactive\": \"#40424A\",         // leere Balken / empty bars\r\n"
    "    \"icon\": \"#C7CCD6\",\r\n"
    "    \"lcd\": \"#B5D187\",              // nur classic: LCD / classic only\r\n"
    "    \"lcdGlow\": \"#ED9E4D\",\r\n"
    "    \"lcdText\": \"#17210F\"\r\n"
    "  },\r\n"
    "  \"backgroundImage\": \"\",           // Bild im Ordner styles (PNG/JPG), fuellt die Anzeige / picture in the styles folder, fills the display\r\n"
    "  \"backgroundOpacity\": 1.0\r\n"
    "}\r\n";
} // namespace

void InitCardStyles(const std::wstring& dir) {
    { std::lock_guard<std::mutex> l(g_mtx); g_dir = dir; g_cache.clear(); }
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tpl = dir + L"_vorlage-template.json";
    if (GetFileAttributesW(tpl.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f{std::filesystem::path(tpl), std::ios::binary};
        f << kTemplate;
    }
    auto n = ListCustomStyles().size();
    if (n) Log::Write("Styles: %d own style(s) in the styles folder", (int)n);
}

std::vector<CardStyleEntry> ListCustomStyles() {
    std::wstring dir; { std::lock_guard<std::mutex> l(g_mtx); dir = g_dir; }
    std::vector<CardStyleEntry> v;
    if (dir.empty()) return v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || fd.cFileName[0] == L'_') continue;
        std::wstring stem = fd.cFileName; stem.resize(stem.size() - 5);
        CardStyleEntry e; e.id = "style:" + WideToUtf8(stem); e.name = stem;
        json j = json::parse(ReadFile(dir + fd.cFileName), nullptr, false, true);
        if (!j.is_discarded() && j.is_object() && j.contains("name") && j["name"].is_string() && !j["name"].get<std::string>().empty())
            e.name = Utf8ToWide(j["name"].get<std::string>());
        v.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end(), [](const CardStyleEntry& a, const CardStyleEntry& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return v;
}

std::shared_ptr<const CardTheme> CustomCardTheme(const std::string& design) {
    if (design.rfind("style:", 0) != 0) return nullptr;
    std::string name = design.substr(6);
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_dir.empty() || name.empty() || name.find_first_of("\\/:") != std::string::npos) return nullptr;
    Entry& e = g_cache[name];
    ULONGLONG now = GetTickCount64();
    if (e.checkedAt && now - e.checkedAt < 2000) return e.theme;   // files are looked at every 2 s at most
    e.checkedAt = now;
    std::wstring file = g_dir + Utf8ToWide(name) + L".json";
    FILETIME ft = TimeOf(file);
    bool picChanged = !e.pic.empty() && !Same(TimeOf(e.pic), e.picTime);   // the picture was replaced
    if (!ft.dwLowDateTime && !ft.dwHighDateTime) {
        if (!e.missingLogged) Log::Write("Styles: '%s.json' not found in the styles folder - modern is shown", name.c_str());
        e.missingLogged = true; e.theme.reset(); e.fileTime = ft;
        return nullptr;
    }
    e.missingLogged = false;
    if (Same(ft, e.fileTime) && !picChanged) return e.theme;   // unchanged (also: broken and unchanged)
    e.fileTime = ft;
    e.theme = Load(name, file, g_dir, e.pic, e.picTime);
    return e.theme;
}

bool StyleUsesTiles(const std::string& design) { return CardThemeFor(design)->layout == "presets"; }
