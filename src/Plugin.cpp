// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SCS Telemetry SDK entry points. ETS2/ATS load every DLL from <game>\bin\win_x64\plugins\ and call
// scs_telemetry_init / scs_telemetry_shutdown. DllMain does nothing (loader lock!); all work starts in
// scs_telemetry_init on a separate "radio thread", so the game callbacks never block on audio or network.
#include "Common.h"
#include <objbase.h>
#include <cmath>
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
#include <common/scssdk_telemetry_common_configs.h>
#include <scssdk_telemetry_event.h>
#include <common/scssdk_telemetry_common_gameplay_events.h>


namespace {
std::atomic<double> g_x{0}, g_y{0}, g_z{0}, g_heading{0};
std::atomic<float> g_cabX{0}, g_cabY{0}, g_cabZ{0}, g_headX{0}, g_headY{0}, g_headZ{0};   // config: cabin in the truck, head in the cabin
std::atomic<bool> g_havePos{false};
std::atomic<bool> g_electric{false}, g_engine{false};
std::atomic<float> g_speed{0.f};   // m/s, negative when reversing
std::atomic<float> g_backlight{1.f};                 // dashboard backlight 0..1
std::atomic<bool> g_lightParking{false}, g_lightLow{false};

// Truck identity (configuration event "truck"): used to pick the truck preset of the in-cab screen
std::mutex g_truckMtx;
std::string g_truckId, g_truckName, g_truckBrandId, g_truckBrand, g_truckModel;
std::atomic<int> g_truckVersion{0};
// current job (configuration event "job") and the game time: for the virtual phone's message from the customer
std::mutex g_jobMtx;
JobInfo g_job;
std::atomic<int> g_jobVersion{0};
std::atomic<unsigned> g_gameTime{0};
std::atomic<float> g_navDist{0}, g_navTime{0};   // route advisor: metres / seconds to the destination
SCSAPI_VOID OnNav(const scs_string_t, const scs_u32_t, const scs_value_t* const v, const scs_context_t ctx) {
    float f = (v && v->type == SCS_VALUE_TYPE_float) ? v->value_float.value : 0.f;
    (ctx ? g_navTime : g_navDist) = f;
}
std::mutex g_eventMtx;
std::vector<GameEvent> g_events;
SCSAPI_VOID OnGameplay(const scs_event_t, const void* const info, const scs_context_t) {
    const auto* e = static_cast<const scs_telemetry_gameplay_event_t*>(info);
    if (!e || !e->id) return;
    GameEvent g; g.id = e->id;
    for (const scs_named_value_t* a = e->attributes; a && a->name; ++a) {
        const scs_value_t& v = a->value;
        long long n = v.type == SCS_VALUE_TYPE_s64 ? v.value_s64.value : v.type == SCS_VALUE_TYPE_u64 ? (long long)v.value_u64.value :
                      v.type == SCS_VALUE_TYPE_s32 ? v.value_s32.value : v.type == SCS_VALUE_TYPE_u32 ? v.value_u32.value : 0;
        if (!strcmp(a->name, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_revenue) || !strcmp(a->name, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cancel_penalty) ||
            !strcmp(a->name, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_fine_amount)) g.amount = n;
        else if (!strcmp(a->name, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_fine_offence) && v.type == SCS_VALUE_TYPE_string && v.value_string.value) g.text = v.value_string.value;
    }
    std::lock_guard<std::mutex> l(g_eventMtx);
    if (g_events.size() < 32) g_events.push_back(g);
}
SCSAPI_VOID OnGameTime(const scs_string_t, const scs_u32_t, const scs_value_t* const v, const scs_context_t) {
    if (v && v->type == SCS_VALUE_TYPE_u32) g_gameTime = v->value_u32.value;
}
void OnJob(const scs_telemetry_configuration_t* cfg) {
    JobInfo j;
    for (const scs_named_value_t* a = cfg->attributes; a && a->name; ++a) {
        const scs_value_t& v = a->value;
        auto str = [&]() { return v.type == SCS_VALUE_TYPE_string && v.value_string.value ? std::string(v.value_string.value) : std::string(); };
        if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_delivery_time) && v.type == SCS_VALUE_TYPE_u32) j.deliveryTime = v.value_u32.value;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city)) j.city = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company)) j.company = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo)) j.cargo = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_is_cargo_loaded) && v.type == SCS_VALUE_TYPE_bool) j.loaded = v.value_bool.value != 0;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_special_job) && v.type == SCS_VALUE_TYPE_bool) j.special = v.value_bool.value != 0;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city_id)) j.cityId = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city)) j.sourceCity = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city_id)) j.sourceCityId = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company)) j.sourceCompany = str();
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_income) && v.type == SCS_VALUE_TYPE_u64) j.income = (long long)v.value_u64.value;
    }
    j.active = j.deliveryTime != 0;
    std::lock_guard<std::mutex> l(g_jobMtx);
    if (j == g_job) return;
    g_job = j; ++g_jobVersion;
}
SCSAPI_VOID OnConfiguration(const scs_event_t, const void* const info, const scs_context_t) {
    const auto* cfg = static_cast<const scs_telemetry_configuration_t*>(info);
    if (cfg && cfg->id && !strcmp(cfg->id, SCS_TELEMETRY_CONFIG_job)) { OnJob(cfg); return; }
    if (!cfg || !cfg->id || strcmp(cfg->id, SCS_TELEMETRY_CONFIG_truck) != 0) return;
    std::string id, brand, name, brandId;
    for (const scs_named_value_t* a = cfg->attributes; a && a->name; ++a) {
        if (a->value.type == SCS_VALUE_TYPE_fvector && !strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cabin_position)) {
            g_cabX = a->value.value_fvector.x; g_cabY = a->value.value_fvector.y; g_cabZ = a->value.value_fvector.z; continue;
        }
        if (a->value.type == SCS_VALUE_TYPE_fvector && !strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_head_position)) {
            g_headX = a->value.value_fvector.x; g_headY = a->value.value_fvector.y; g_headZ = a->value.value_fvector.z; continue;
        }
        if (a->value.type != SCS_VALUE_TYPE_string || !a->value.value_string.value) continue;
        if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_id)) id = a->value.value_string.value;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_brand)) brand = a->value.value_string.value;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_name)) name = a->value.value_string.value;
        else if (!strcmp(a->name, SCS_TELEMETRY_CONFIG_ATTRIBUTE_brand_id)) brandId = a->value.value_string.value;
    }
    if (id.empty()) return;
    std::lock_guard<std::mutex> l(g_truckMtx);
    if (id == g_truckId) return;
    g_truckId = id;
    g_truckName = brand.empty() ? name : (name.rfind(brand, 0) == 0 ? name : brand + " " + name);
    g_truckBrandId = brandId; g_truckBrand = brand;
    g_truckModel = (!brand.empty() && name.rfind(brand, 0) == 0) ? name.substr(brand.size()) : name;
    g_truckModel.erase(0, g_truckModel.find_first_not_of(' '));
    ++g_truckVersion;
}

SCSAPI_VOID OnBacklight(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
    g_backlight = (value && value->type == SCS_VALUE_TYPE_float) ? value->value_float.value : 1.f;
}
SCSAPI_VOID OnLight(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t ctx) {
    bool v = value && value->type == SCS_VALUE_TYPE_bool && value->value_bool.value != 0;
    (ctx ? g_lightLow : g_lightParking) = v;
}
SCSAPI_VOID OnSpeed(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
    g_speed = (value && value->type == SCS_VALUE_TYPE_float) ? value->value_float.value : 0.f;
}

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
    g_y = value->value_dplacement.position.y;
    g_z = value->value_dplacement.position.z;
    g_heading = value->value_dplacement.orientation.heading;
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
    int truckVersion = 0, jobVersion = 0;
    while (!g_quit) {
        if (radio.MenuOpen()) {   // key menu: keys go to the menu, no radio actions
            if (radio.MenuTick(input)) { input.Init(radio.Settings()); input.Resync(); }
        } else if (radio.PhoneOpen()) {   // the phone has keyboard and mouse; a controller button can still close it
            radio.PhoneUiTick(input);
            input.Poll([&](Action a, bool) { if (a == Action::Phone && radio.PhoneOpen()) radio.OnAction(a, false); });
            if (!radio.PhoneOpen()) input.Resync();
        } else input.Poll([&](Action a, bool hold) { Log::Write("Action: %s%s", ActionName(a), hold ? " (held)" : ""); radio.OnAction(a, hold); });
        if (int v = g_truckVersion.load(); v != truckVersion) {
            truckVersion = v;
            std::string id, name, brandId, brand, model;
            { std::lock_guard<std::mutex> l(g_truckMtx); id = g_truckId; name = g_truckName; brandId = g_truckBrandId; brand = g_truckBrand; model = g_truckModel; }
            radio.SetTruck(id, name, brandId, brand, model);
        }
        if (int v = g_jobVersion.load(); v != jobVersion) {
            jobVersion = v;
            JobInfo j; { std::lock_guard<std::mutex> l(g_jobMtx); j = g_job; }
            radio.SetJob(j);
        }
        radio.SetGameTime(g_gameTime);
        radio.SetNavigation(g_navDist, g_navTime);
        {
            std::vector<GameEvent> ev;
            { std::lock_guard<std::mutex> l(g_eventMtx); ev.swap(g_events); }
            for (auto& e : ev) radio.OnGameEvent(e);
        }
        radio.SetLights(g_backlight, g_lightParking || g_lightLow);
        {   // driver's head in world coordinates (truck position + cabin + head, turned by the heading) for "quieter outside"
            const double a = g_heading * 6.283185307179586;
            const double lx = g_cabX + g_headX, ly = g_cabY + g_headY, lz = g_cabZ + g_headZ;
            double head[3] = {g_x + lx * std::cos(a) + lz * std::sin(a), g_y + ly, g_z - lx * std::sin(a) + lz * std::cos(a)};
            double truck[3] = {g_x, g_y, g_z};
            radio.SetPose(g_havePos, truck, head, g_speed, g_heading);
        }
        radio.Tick(g_x, g_z, g_havePos, g_electric, g_engine, g_speed);
        Sleep(radio.MenuOpen() || radio.PhoneOpen() ? 15 : 50);   // the menu reacts faster to the mouse
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
                if (folder + n != mine && (l.find(L"local-radio") != std::wstring::npos || l.find(L"realistic-radio") != std::wstring::npos ||
                                           l.find(L"realisticradio") != std::wstring::npos))
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
    // truck model (for the truck presets of the in-cab screen)
    if (p->register_for_event(SCS_TELEMETRY_EVENT_configuration, OnConfiguration, nullptr) != SCS_RESULT_ok)
        Log::Write("Truck presets: configuration event not available");
    // speed for the speed-dependent volume ("autoVolume")
    // lights and dashboard backlight: the cab screen dims at night (beta)
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_dashboard_backlight, SCS_U32_NIL, SCS_VALUE_TYPE_float,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBacklight, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_parking, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnLight, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_low_beam, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnLight, (scs_context_t)1);
    p->register_for_channel(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnGameTime, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnNav, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnNav, (scs_context_t)1);
    if (p->register_for_event(SCS_TELEMETRY_EVENT_gameplay, OnGameplay, nullptr) != SCS_RESULT_ok) Log::Write("Phone: gameplay events not available");
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnSpeed, nullptr);
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
