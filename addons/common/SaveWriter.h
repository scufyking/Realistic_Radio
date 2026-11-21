// Realistic Radio add-ons (Jobs+, Bus+)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Writes a changed save back.
//
// Profiles with Steam Cloud live in Steam\userdata\<id>\227300\remote\profiles\... and the game reads them through Steam
// ("<UFS>/steam/..."). Steam keeps the size of every cloud file: a file changed on disk behind Steam's back is read with
// the old size, cut off, and the game says "Error while reading stream" / "failed to load the save". So such a save is
// written through Steam (ISteamRemoteStorage::FileWrite of the steam_api64.dll the game has loaded) - then Steam knows
// the new file and syncs it. Local profiles (Documents\Euro Truck Simulator 2\profiles) are written as a normal file.
//
// The original is copied to backupDir first (outside the cloud folder, so Steam does not see a strange file).
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace SaveWriter {

// "...\userdata\123\227300\remote\profiles\X\save\quicksave\game.sii" -> "profiles/X/save/quicksave/game.sii"; "" = local
inline std::string CloudName(const std::filesystem::path& file) {
    std::wstring p = file.wstring();
    for (auto& c : p) if (c == L'/') c = L'\\';
    std::wstring low = p;
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t u = low.find(L"\\userdata\\");
    size_t r = low.find(L"\\remote\\", u == std::wstring::npos ? 0 : u);
    if (u == std::wstring::npos || r == std::wstring::npos) return {};
    std::wstring rel = p.substr(r + 8);
    for (auto& c : rel) if (c == L'\\') c = L'/';
    int n = WideCharToMultiByte(CP_UTF8, 0, rel.data(), (int)rel.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, rel.data(), (int)rel.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// false + error: nothing was written (the original stays as it is)
inline bool Write(const std::filesystem::path& file, const std::string& data, const std::filesystem::path& backupDir, std::string& how, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(backupDir, ec);
    std::filesystem::path backup = backupDir / (file.parent_path().filename().wstring() + L"-game.sii");
    std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, ec);

    const std::string name = CloudName(file);
    if (name.empty()) {
        std::ofstream o(file, std::ios::binary | std::ios::trunc);
        o << data;
        o.close();
        if (!o) { error = "could not write the file"; return false; }
        how = "file";
        return true;
    }
    // Steam Cloud
    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    typedef void* (__cdecl* GetIface)();
    typedef bool (__cdecl* FileWrite)(void*, const char*, const void*, int);
    typedef int (__cdecl* GetSize)(void*, const char*);
    typedef bool (__cdecl* Exists)(void*, const char*);
    GetIface get = steam ? (GetIface)GetProcAddress(steam, "SteamAPI_SteamRemoteStorage_v016") : nullptr;
    FileWrite write = steam ? (FileWrite)GetProcAddress(steam, "SteamAPI_ISteamRemoteStorage_FileWrite") : nullptr;
    GetSize size = steam ? (GetSize)GetProcAddress(steam, "SteamAPI_ISteamRemoteStorage_GetFileSize") : nullptr;
    Exists exists = steam ? (Exists)GetProcAddress(steam, "SteamAPI_ISteamRemoteStorage_FileExists") : nullptr;
    void* rs = get ? get() : nullptr;
    if (!rs || !write || !size || !exists) { error = "steam cloud: steam_api64.dll not usable"; return false; }
    if (!exists(rs, name.c_str())) { error = "steam cloud: Steam does not know " + name; return false; }
    if (data.size() > 0x7FFFFFFF) { error = "save too big"; return false; }
    if (!write(rs, name.c_str(), data.data(), (int)data.size())) { error = "steam cloud: FileWrite failed for " + name; return false; }
    int now = size(rs, name.c_str());
    if (now != (int)data.size()) { error = "steam cloud: size after writing is " + std::to_string(now) + " instead of " + std::to_string(data.size()); return false; }
    how = "Steam Cloud (" + name + ")";
    return true;
}
}
