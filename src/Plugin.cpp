// ETS2 Local Radio Native
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
    { Config c; c.Load(dir + L"local-radio.json"); input.Init(c); }
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

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    std::wstring dir = PluginDataDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    Log::Init(dir + L"local-radio.log");
    std::string gid = p->common.game_id ? p->common.game_id : "";
    g_game = (gid == SCS_GAME_ID_ATS) ? "ats" : "ets2";
    Log::Write("ETS2 Local Radio Native loaded by %s (game id '%s', telemetry 0x%x)", p->common.game_name ? p->common.game_name : "?", gid.c_str(), version);
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement,
                                SCS_TELEMETRY_CHANNEL_FLAG_none, OnPlacement, nullptr) != SCS_RESULT_ok) {
        p->common.log(SCS_LOG_TYPE_error, "[local-radio] cannot register truck.world.placement");
        return SCS_RESULT_generic_error;
    }
    // ignition / engine state (radio only plays while the truck is powered, see "powerSource")
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_electric_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, nullptr);
    p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                            SCS_TELEMETRY_CHANNEL_FLAG_no_value, OnBool, (scs_context_t)1);
    p->common.log(SCS_LOG_TYPE_message, "[local-radio] native Local Radio plugin initialised");
    g_quit = false;
    g_thread = CreateThread(nullptr, 0, RadioThread, nullptr, 0, nullptr);
    return g_thread ? SCS_RESULT_ok : SCS_RESULT_generic_error;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    g_quit = true;
    if (g_thread) { WaitForSingleObject(g_thread, 10000); CloseHandle(g_thread); g_thread = nullptr; }
    g_havePos = false; g_electric = false; g_engine = false;
    Log::Write("Plugin shut down");
    Log::Shutdown();
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
