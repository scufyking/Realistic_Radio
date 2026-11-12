// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "ForeignKeys.h"
#include "Logger.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <map>
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
// SPF key names ("KEY_NUMPAD7") -> virtual key code. Modifier keys are left out: SPF never takes them from the game.
int VkOf(const std::string& n) {
    if (n.rfind("KEY_", 0) != 0) return 0;
    std::string k = n.substr(4);
    if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') return k[0];
    if (k.size() == 1 && k[0] >= '0' && k[0] <= '9') return k[0];
    if (k.rfind("NUMPAD", 0) == 0 && k.size() == 7 && k[6] >= '0' && k[6] <= '9') return VK_NUMPAD0 + (k[6] - '0');
    if (k[0] == 'F' && k.size() <= 3) { int f = atoi(k.c_str() + 1); if (f >= 1 && f <= 24) return VK_F1 + f - 1; }
    static const std::map<std::string, int> t = {
        {"ADD", VK_ADD}, {"SUBTRACT", VK_SUBTRACT}, {"MULTIPLY", VK_MULTIPLY}, {"DIVIDE", VK_DIVIDE}, {"NUMPADDECIMAL", VK_DECIMAL},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT}, {"END", VK_END}, {"HOME", VK_HOME}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"PAUSE", VK_PAUSE}, {"SPACE", VK_SPACE},
        {"ENTER", VK_RETURN}, {"BACKSPACE", VK_BACK}, {"TAB", VK_TAB}, {"ESCAPE", VK_ESCAPE}, {"CAPSLOCK", VK_CAPITAL},
        {"NUMLOCK", VK_NUMLOCK}, {"SCROLLLOCK", VK_SCROLL}, {"PRINTSCREEN", VK_SNAPSHOT}, {"LBRACKET", VK_OEM_4},
        {"RBRACKET", VK_OEM_6}, {"SEMICOLON", VK_OEM_1}, {"COMMA", VK_OEM_COMMA}, {"PERIOD", VK_OEM_PERIOD}, {"APOSTROPHE", VK_OEM_7},
        {"SLASH", VK_OEM_2}, {"BACKSLASH", VK_OEM_5}, {"GRAVE", VK_OEM_3}, {"EQUAL", VK_OEM_PLUS}, {"HYPHEN", VK_OEM_MINUS},
        {"OEM8", VK_OEM_8}, {"OEM102", VK_OEM_102}, {"MENU", VK_APPS},
        {"VOLUME_MUTE", VK_VOLUME_MUTE}, {"VOLUME_DOWN", VK_VOLUME_DOWN}, {"VOLUME_UP", VK_VOLUME_UP},
        {"MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK}, {"MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK}, {"MEDIA_STOP", VK_MEDIA_STOP},
        {"MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE}};
    auto it = t.find(k);
    return it == t.end() ? 0 : it->second;
}
// every single-key keyboard binding in a settings file (chords like Ctrl + X need more than the key and are left alone)
void Collect(const json& j, const std::string& owner, std::vector<ForeignKeys::Key>& out) {
    if (j.is_object()) {
        auto b = j.find("bindings");
        if (b != j.end() && b->is_array())
            for (auto& e : *b)
                if (e.is_object() && e.value("type", std::string()) == "keyboard" && e.contains("key") && e["key"].is_string())
                    if (int vk = VkOf(e["key"].get<std::string>())) out.push_back({vk, owner});
        for (auto& [k, v] : j.items()) if (k != "bindings") Collect(v, owner, out);
    } else if (j.is_array()) {
        for (auto& v : j) Collect(v, owner, out);
    }
}
bool ReadFile(const fs::path& p, const std::string& owner, std::vector<ForeignKeys::Key>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    try { json j = json::parse(f, nullptr, true, true); size_t n = out.size(); Collect(j, owner, out); return out.size() > n; }
    catch (...) { Log::Write("Keys: %s unreadable", p.string().c_str()); return false; }
}
}

namespace ForeignKeys {
std::vector<Key> Scan(const std::wstring& pluginsDir) {
    std::vector<Key> out;
    std::error_code ec;
    fs::path root(pluginsDir);
    ReadFile(root / L"spfAssets" / L"config" / L"framework_settings.json", "SPF Framework", out);
    fs::path spf = root / L"spfPlugins";
    if (fs::is_directory(spf, ec))
        for (auto& d : fs::directory_iterator(spf, ec)) {
            if (!d.is_directory(ec)) continue;
            std::string owner = WideToUtf8(d.path().filename().wstring());
            bool found = ReadFile(d.path() / L"config" / L"settings.json", owner, out);
            std::string low = owner; for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (!found && low == "motioncab")   // installed, keys not saved yet: MotionCab's own defaults
                for (const char* k : {"KEY_DIVIDE", "KEY_MULTIPLY", "KEY_NUMPAD7", "KEY_NUMPAD9", "KEY_SUBTRACT", "KEY_F10", "KEY_F9", "KEY_PAGEUP"})
                    out.push_back({VkOf(k), owner});
        }
    return out;
}
}
