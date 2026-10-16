// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SCS Telemetry SDK entry points. ETS2/ATS load every DLL from <game>\bin\win_x64\plugins\ and call
// scs_telemetry_init / scs_telemetry_shutdown. DllMain does nothing (loader lock!); all work starts in
// scs_telemetry_init on a separate "radio thread", so the game callbacks never block on audio or network.
#include "Common.h"
#include <objbase.h>
#include "Logger.h"
#include "RadioManager.h"
#include "InputManager.h"
#include "EmbeddedData.h"
#include <scssdk_telemetry.h>
#include <eurotrucks2/scssdk_eut2.h>
#include <eurotrucks2/scssdk_telemetry_eut2.h>
#include <amtrucks/scssdk_ats.h>
#include <amtrucks/scssdk_telemetry_ats.h>
#include <common/scssdk_telemetry_common_channels.h>
#include <common/scssdk_telemetry_truck_common_channels.h>

namespace {
std::atomic<double> g_x{0}, g_z{0};
std::atomic<bool> g_havePos{false};
std::atomic<bool> g_electric{false}, g_engine{false};

SCSAPI_VOID OnBool(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t ctx) {
    bool v = value && value->type == SCS_VALUE_TYPE_bool && value->value_bool.value != 0;
    (ctx ? g_engine : g_electric) = v;
}
std::atomic<bool> g_quit{false};
HANDLE g_thread = nullptr;
HANDLE g_instance = nullptr;   // per-process single-instance mutex
std::string g_game = "ets2";

SCSAPI_VOID OnPlacement(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_dplacement) return;
    g_x = value->value_dplacement.position.x;
    g_z = value->value_dplacement.position.z;
    g_havePos = true;
}

DWORD WINAPI RadioThread(LPVOID) {
    std::wstring dir = PluginDataDir();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    RadioManager radio; InputManager input;
    if (!radio.Init(dir, g_game)) {
        Log::Write("Initialisation failed - radio disabled. Check the files in %s", WideToUtf8(dir).c_str());
        CoUninitialize();
        return 1;
    }
    { Config c; c.Load(dir + L"realistic-radio.json"); input.Init(c); }
    Log::Write("Radio thread running");
    while (!g_quit) {
        input.Poll([&](Action a) { Log::Write("Action: %s", ActionName(a)); radio.OnAction(a); });
        radio.Tick(g_x, g_z, g_havePos, g_electric, g_engine);
        Sleep(50);
    }
    radio.Shutdown();
    CoUninitialize();
    return 0;
}
}

// Earlier releases were called "ETS2 Local Radio Native" and kept their files in "<plugins>\\local-radio\\".
// Copy the user's own files over once (never overwrites anything that already exists in the new folder).
static void MigrateFromLocalRadio(const std::wstring& dir) {
    std::wstring base = dir.substr(0, dir.size() - 1);
    base = base.substr(0, base.find_last_of(L"\\/") + 1);
    std::wstring old = base + L"local-radio\\";
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    auto copy = [&](const std::wstring& from, const std::wstring& to) {
        if (GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        if (CopyFileW(from.c_str(), to.c_str(), TRUE)) Log::Write("Migrated %s", WideToUtf8(to).c_str());
    };
    copy(old + L"local-radio.json", dir + L"realistic-radio.json");
    for (const wchar_t* g : {L"ets2", L"ats"}) {
        copy(old + L"favourites-" + g + L".json", dir + L"favourites-" + g + L".json");
        copy(old + L"custom-stations-" + g + L".json", dir + L"custom-stations-" + g + L".json");
    }
    CreateDirectoryW((dir + L"logos").c_str(), nullptr);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((old + L"logos\\*").c_str(), &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && CopyFileW((old + L"logos\\" + fd.cFileName).c_str(), (dir + L"logos\\" + fd.cFileName).c_str(), TRUE)) ++n; }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n) Log::Write("Migrated %d cached logos", n);
    Log::Write("Old folder %s found - after checking your settings you can delete it and the old local-radio-native.dll", WideToUtf8(old).c_str());
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    // Only one copy may run per game process: a second DLL (e.g. "realistic-radio (1).dll") would play a second
    // radio with its own volume. The copy that comes second stays inactive and does not touch the shared log file.
    wchar_t mtxName[80]; swprintf(mtxName, 80, L"Local\\ETS2LocalRadioNative-%lu", GetCurrentProcessId());   // name kept from the previous release, so old and new DLL exclude each other
    g_instance = CreateMutexW(nullptr, FALSE, mtxName);
    if (g_instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instance); g_instance = nullptr;
        p->common.log(SCS_LOG_TYPE_warning, "[realistic-radio] another copy of the Realistic Radio plugin is already loaded - this copy stays inactive. Remove duplicate DLLs from the plugins folder.");
        return SCS_RESULT_ok;
    }
    std::wstring dir = PluginDataDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    Log::Init(dir + L"realistic-radio.log");
    ExtractEmbeddedData(dir);   // only the DLL is needed: station data and sounds are unpacked from it
    MigrateFromLocalRadio(dir);
    std::string gid = p->common.game_id ? p->common.game_id : "";
    g_game = (gid == SCS_GAME_ID_ATS) ? "ats" : "ets2";
    Log::Write("Realistic Radio " REALISTIC_RADIO_VERSION " loaded by %s (game id '%s', telemetry 0x%x)", p->common.game_name ? p->common.game_name : "?", gid.c_str(), version);
    {   // warn about other radio plugin DLLs in the same folder
        HMODULE self = nullptr; wchar_t me[MAX_PATH] = {0};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&scs_telemetry_init, &self);
        GetModuleFileNameW(self, me, MAX_PATH);
        std::wstring mine = me, folder = mine.substr(0, mine.find_last_of(L"\\/") + 1);
        Log::Write("Plugin file: %s", WideToUtf8(mine).c_str());
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((folder + L"*.dll").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName, l = n; for (auto& ch : l) ch = towlower(ch);
                if (folder + n != mine && (l.find(L"local-radio") != std::wstring::npos || l.find(L"realistic-radio") != std::wstring::npos))
                    Log::Write("WARNING: another radio plugin DLL is in the plugins folder: %s - remove it, otherwise two radios may play", WideToUtf8(n).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement,
                                SCS_TELEMETRY_CHANNEL_FLAG_none, OnPlacement, nullptr) != SCS_RESULT_ok) {
        p->common.log(SCS_LOG_TYPE_error, "[realistic-radio] cannot register truck.world.placement");
        return SCS_RESULT_generic_error;
    }
    // ignition / engine state (radio only plays while the truck is powered, see "powerSource")
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_electric_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, (scs_context_t)1);
    p->common.log(SCS_LOG_TYPE_message, "[realistic-radio] Realistic Radio plugin initialised");
    g_quit = false;
    g_thread = CreateThread(nullptr, 0, RadioThread, nullptr, 0, nullptr);
    return g_thread ? SCS_RESULT_ok : SCS_RESULT_generic_error;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    if (!g_instance) return;   // inactive duplicate copy
    g_quit = true;
    if (g_thread) { WaitForSingleObject(g_thread, 10000); CloseHandle(g_thread); g_thread = nullptr; }
    g_havePos = false; g_electric = false; g_engine = false;
    Log::Write("Plugin shut down");
    Log::Shutdown();
    CloseHandle(g_instance); g_instance = nullptr;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
