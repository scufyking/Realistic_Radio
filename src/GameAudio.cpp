// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "GameAudio.h"
#include "Common.h"
#include "Logger.h"
#include "MinHook.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

// FMOD C API (only what is needed, declared by hand: ETS2 1.61 ships FMOD 2.03.11). FMOD objects are opaque pointers.
namespace {
typedef int FRESULT;   // FMOD_RESULT, 0 = FMOD_OK
typedef void FSys; typedef void FSound; typedef void FChannel; typedef void FGroup; typedef void SSys; typedef void SBus; typedef void SBank;
typedef FRESULT(__stdcall* PcmReadCb)(FSound*, void*, unsigned);

// FMOD_CREATESOUNDEXINFO (FMOD 2.02 / 2.03 layout; cbsize tells FMOD how much of it we fill)
struct ExInfo {
    int cbsize; unsigned length, fileoffset; int numchannels, defaultfrequency, format; unsigned decodebuffersize;
    int initialsubsound, numsubsounds; int* inclusionlist; int inclusionlistnum;
    PcmReadCb pcmreadcallback; void* pcmsetposcallback; void* nonblockcallback; const char* dlsname; const char* encryptionkey;
    int maxpolyphony; void* userdata; int suggestedsoundtype;
    void *fileuseropen, *fileuserclose, *fileuserread, *fileuserseek, *fileuserasyncread, *fileuserasynccancel, *fileuserdata;
    int filebuffersize, channelorder; void* initialsoundgroup; unsigned initialseekposition, initialseekpostype;
    int ignoresetfilesystem; unsigned audioqueuepolicy, minmidigranularity; int nonblockthreadid; void* fsbguid;
};
constexpr unsigned kLoopNormal = 0x2, k2D = 0x8, kCreateStream = 0x80, kOpenUser = 0x400;
constexpr int kFormatFloat = 5;

struct Api {
    FRESULT (*GetBus)(SSys*, const char*, SBus**);
    FRESULT (*LockGroup)(SBus*);
    FRESULT (*GetGroup)(SBus*, FGroup**);
    FRESULT (*GetCore)(SSys*, FSys**);
    FRESULT (*BusVolume)(SBus*, float*, float*);
    FRESULT (*BankCount)(SSys*, int*);
    FRESULT (*BankList)(SSys*, SBank**, int, int*);
    FRESULT (*BankBusCount)(SBank*, int*);
    FRESULT (*BankBusList)(SBank*, SBus**, int, int*);
    FRESULT (*BusPath)(SBus*, char*, int, int*);
    FRESULT (*CreateSound)(FSys*, const char*, unsigned, ExInfo*, FSound**);
    FRESULT (*PlaySound)(FSys*, FSound*, FGroup*, int, FChannel**);
    FRESULT (*ChSetVolume)(FChannel*, float);
    FRESULT (*ChSetPaused)(FChannel*, int);
    FRESULT (*ChStop)(FChannel*);
    FRESULT (*ChIsPlaying)(FChannel*, int*);
    FRESULT (*ChSetMute)(FChannel*, int);
    FRESULT (*ChGetMute)(FChannel*, int*);
    FRESULT (*SoundRelease)(FSound*);
    FRESULT (*GrNumChannels)(FGroup*, int*);
    FRESULT (*GrChannel)(FGroup*, int, FChannel**);
    FRESULT (*GrNumGroups)(FGroup*, int*);
    FRESULT (*GrGroup)(FGroup*, int, FGroup**);
    // listener (the camera) and the game's global parameters - optional
    FRESULT (*GetListener)(SSys*, int, void* attributes, void* attenuationPos);
    FRESULT (*ParamCount)(SSys*, int*);
    FRESULT (*ParamList)(SSys*, void* array, int capacity, int* count);
    FRESULT (*ParamById)(SSys*, unsigned long long id, float* value, float* finalValue);
} A;
struct FVec { float x, y, z; };
struct F3D { FVec position, velocity, forward, up; };
struct ParamDesc {   // FMOD_STUDIO_PARAMETER_DESCRIPTION (2.02 / 2.03)
    const char* name; unsigned long long id; float minimum, maximum, defaultvalue; int type; unsigned flags; unsigned char guid[16];
};
bool g_apiOk = false;

typedef FRESULT (*UpdateFn)(SSys*);
UpdateFn g_origUpdate = nullptr;
std::atomic<bool> g_hooked{false};

// settings (radio thread) -> read on the game's thread
std::atomic<bool> g_cabin{false}, g_muteGame{true}, g_audible{false};
std::mutex g_cfgMtx;
std::string g_busPath = "bus:/radio";
std::mutex g_pullMtx;   // the stream callback never calls into an audio engine that is going away
GameAudio::PullFn g_pull = nullptr;
void* g_user = nullptr;

// state (game thread)
SSys* g_sys = nullptr;
FSound* g_sound = nullptr;
FChannel* g_channel = nullptr;
SBus* g_radioBus = nullptr;
bool g_radioLocked = false;
std::vector<FChannel*> g_muted;
ULONGLONG g_retryAt = 0, g_scanAt = 0;
std::atomic<int> g_state{0};
bool g_loggedMute = false, g_warnedVolume = false;

FRESULT __stdcall PcmRead(FSound*, void* data, unsigned bytes) {
    unsigned frames = bytes / (2 * sizeof(float));
    std::lock_guard<std::mutex> l(g_pullMtx);
    if (g_pull) g_pull((float*)data, frames, g_user);
    else memset(data, 0, bytes);
    return 0;
}

template <class F> bool Get(HMODULE m, const char* name, F& fn) { fn = (F)(void*)GetProcAddress(m, name); return fn != nullptr; }
bool LoadApi() {
    HMODULE st = GetModuleHandleW(L"fmodstudio.dll"), co = GetModuleHandleW(L"fmod.dll");
    if (!st || !co) return false;
    return Get(st, "FMOD_Studio_System_GetBus", A.GetBus) && Get(st, "FMOD_Studio_Bus_LockChannelGroup", A.LockGroup) &&
           Get(st, "FMOD_Studio_Bus_GetChannelGroup", A.GetGroup) && Get(st, "FMOD_Studio_System_GetCoreSystem", A.GetCore) &&
           Get(st, "FMOD_Studio_Bus_GetVolume", A.BusVolume) && Get(st, "FMOD_Studio_System_GetBankCount", A.BankCount) &&
           Get(st, "FMOD_Studio_System_GetBankList", A.BankList) && Get(st, "FMOD_Studio_Bank_GetBusCount", A.BankBusCount) &&
           Get(st, "FMOD_Studio_Bank_GetBusList", A.BankBusList) && Get(st, "FMOD_Studio_Bus_GetPath", A.BusPath) &&
           Get(co, "FMOD_System_CreateSound", A.CreateSound) && Get(co, "FMOD_System_PlaySound", A.PlaySound) &&
           Get(co, "FMOD_Channel_SetVolume", A.ChSetVolume) && Get(co, "FMOD_Channel_SetPaused", A.ChSetPaused) &&
           Get(co, "FMOD_Channel_Stop", A.ChStop) && Get(co, "FMOD_Channel_IsPlaying", A.ChIsPlaying) &&
           Get(co, "FMOD_Channel_SetMute", A.ChSetMute) && Get(co, "FMOD_Channel_GetMute", A.ChGetMute) &&
           Get(co, "FMOD_Sound_Release", A.SoundRelease) && Get(co, "FMOD_ChannelGroup_GetNumChannels", A.GrNumChannels) &&
           Get(co, "FMOD_ChannelGroup_GetChannel", A.GrChannel) && Get(co, "FMOD_ChannelGroup_GetNumGroups", A.GrNumGroups) &&
           Get(co, "FMOD_ChannelGroup_GetGroup", A.GrGroup) &&
           (Get(st, "FMOD_Studio_System_GetListenerAttributes", A.GetListener), Get(st, "FMOD_Studio_System_GetParameterDescriptionCount", A.ParamCount),
            Get(st, "FMOD_Studio_System_GetParameterDescriptionList", A.ParamList), Get(st, "FMOD_Studio_System_GetParameterByID", A.ParamById), true);
}

// ---- where the camera is (FMOD listener) and how the game mixes the cab: for "quieter outside the truck" ----
std::atomic<float> g_lx{0}, g_ly{0}, g_lz{0};
std::atomic<ULONGLONG> g_listenerAt{0};
std::atomic<float> g_winL{-1}, g_winR{-1}, g_winOut{-1}, g_interior{-1};
std::atomic<bool> g_diag{false};
ULONGLONG g_mixAt = 0, g_paramAt = 0;
int g_logLines = 0;
struct Watched { const char* path; SBus* bus = nullptr; bool missing = false; float last = -9; };
Watched g_watch[] = {{"bus:/cabin"}, {"bus:/cabin/interior"}, {"bus:/cabin/window_left"}, {"bus:/cabin/window_right"}, {"bus:/cabin/window_out"},
                     {"bus:/outside"}, {"bus:/radio"}};
struct Param { std::string name; unsigned long long id; float last; bool log; };
// the game's own cab state (global FMOD parameters): cabin_out 0 = camera in the cab .. 1 = outside, wnd_left / wnd_right = windows
std::atomic<float> g_cabinOut{-1}, g_wndLeft{-1}, g_wndRight{-1};
std::vector<Param> g_params;
bool g_paramsListed = false;

float FinalVolume(Watched& w) {
    if (w.missing) return -1;
    if (!w.bus && A.GetBus(g_sys, w.path, &w.bus) != 0) { w.bus = nullptr; w.missing = true; return -1; }
    float v = 0, f = -1;
    if (A.BusVolume(w.bus, &v, &f) != 0) return -1;
    return f;
}
void WatchMix(ULONGLONG now) {
    if (A.GetListener) {
        F3D a{}; 
        if (A.GetListener(g_sys, 0, &a, nullptr) == 0) { g_lx = a.position.x; g_ly = a.position.y; g_lz = a.position.z; g_listenerAt = now; }
    }
    if (now >= g_mixAt) {
        g_mixAt = now + 250;
        float v[7];
        std::string changed;
        for (int i = 0; i < 7; ++i) {
            v[i] = FinalVolume(g_watch[i]);
            if (v[i] >= 0 && std::abs(v[i] - g_watch[i].last) > 0.04f) {
                char b[96]; snprintf(b, sizeof(b), "%s%s %.2f", changed.empty() ? "" : ", ", g_watch[i].path + 5, v[i]); changed += b;
                g_watch[i].last = v[i];
            }
        }
        g_interior = v[1]; g_winL = v[2]; g_winR = v[3]; g_winOut = v[4];
        if (!changed.empty() && g_diag && g_logLines < 60) { ++g_logLines; Log::Write("Game audio: mix %s", changed.c_str()); }
    }
    if (g_diag && A.ParamCount && A.ParamList && A.ParamById && now >= g_paramAt) {   // the game's global parameters: listed once, read 10x per second
        g_paramAt = now + 100;
        if (!g_paramsListed) {
            g_paramsListed = true;
            int n = 0;
            if (A.ParamCount(g_sys, &n) == 0 && n > 0) {
                std::vector<ParamDesc> d(std::min(n, 128) + 4);
                int got = 0;
                if (A.ParamList(g_sys, d.data(), (int)d.size() - 4, &got) == 0) {
                    std::string list;
                    for (int i = 0; i < got; ++i) {
                        float val = 0, fin = 0;
                        A.ParamById(g_sys, d[i].id, &val, &fin);
                        std::string nm = d[i].name ? d[i].name : "?";
                        g_params.push_back({nm, d[i].id, val, nm == "cabin_out" || nm == "wnd_left" || nm == "wnd_right"});
                        char b[160]; snprintf(b, sizeof(b), "%s%s=%.2f", list.empty() ? "" : ", ", d[i].name ? d[i].name : "?", val); list += b;
                    }
                    Log::Write("Game audio: global parameters: %s", list.empty() ? "none" : list.c_str());
                }
            } else Log::Write("Game audio: no global parameters");
        }
        for (auto& p : g_params) {
            float val = 0, fin = 0;
            if (A.ParamById(g_sys, p.id, &val, &fin) != 0) continue;
            if (p.name == "cabin_out") g_cabinOut = val;
            else if (p.name == "wnd_left") g_wndLeft = val;
            else if (p.name == "wnd_right") g_wndRight = val;
            if (p.log && std::abs(val - p.last) > 0.2f) {
                if (g_logLines < 120) { ++g_logLines; Log::Write("Game audio: parameter %s = %.2f", p.name.c_str(), val); }
                p.last = val;
            }
        }
    }
}

void LogBuses(SSys* sys) {   // once: which buses the game has, with their volumes (to understand its mix)
    int nb = 0;
    if (A.BankCount(sys, &nb) != 0 || nb <= 0) return;
    std::vector<SBank*> banks(std::min(nb, 64));
    int got = 0;
    if (A.BankList(sys, banks.data(), (int)banks.size(), &got) != 0) return;
    std::string list;
    for (int b = 0; b < got; ++b) {
        int n = 0;
        if (A.BankBusCount(banks[b], &n) != 0 || n <= 0) continue;
        std::vector<SBus*> buses(std::min(n, 128));
        int k = 0;
        if (A.BankBusList(banks[b], buses.data(), (int)buses.size(), &k) != 0) continue;
        for (int i = 0; i < k; ++i) {
            char path[256] = {0}; int len = 0;
            if (A.BusPath(buses[i], path, sizeof(path), &len) != 0) continue;
            float v = 0, f = 0; A.BusVolume(buses[i], &v, &f);
            char e[300]; snprintf(e, sizeof(e), "%s%s (%.2f)", list.empty() ? "" : ", ", path, v);
            list += e;
        }
    }
    Log::Write("Game audio: buses %s", list.empty() ? "none" : list.c_str());
}

// all channels in a group and its sub groups
void Channels(FGroup* g, std::vector<FChannel*>& out, int depth = 0) {
    if (!g || depth > 6) return;
    int n = 0;
    if (A.GrNumChannels(g, &n) == 0) for (int i = 0; i < n; ++i) { FChannel* c = nullptr; if (A.GrChannel(g, i, &c) == 0 && c) out.push_back(c); }
    int ng = 0;
    if (A.GrNumGroups(g, &ng) == 0) for (int i = 0; i < ng; ++i) { FGroup* s = nullptr; if (A.GrGroup(g, i, &s) == 0) Channels(s, out, depth + 1); }
}

FGroup* BusGroup(SBus*& bus, bool& locked, const std::string& path) {
    if (!bus && A.GetBus(g_sys, path.c_str(), &bus) != 0) { bus = nullptr; return nullptr; }
    if (!locked) { A.LockGroup(bus); locked = true; }   // takes effect with the next update: the group exists then
    FGroup* g = nullptr;
    return A.GetGroup(bus, &g) == 0 ? g : nullptr;
}

void StopOurs() {
    if (g_channel) { A.ChStop(g_channel); g_channel = nullptr; }
    if (g_sound) { A.SoundRelease(g_sound); g_sound = nullptr; }
}

void StartOurs(ULONGLONG now) {
    std::string path; { std::lock_guard<std::mutex> l(g_cfgMtx); path = g_busPath; }
    static SBus* bus = nullptr; static bool locked = false; static std::string busOf;
    if (busOf != path) { bus = nullptr; locked = false; busOf = path; }
    FGroup* group = BusGroup(bus, locked, path);
    if (!group) { g_retryAt = now + 250; return; }   // first frame after locking, or the bus does not exist
    float vol = 1, fin = 1;
    if (A.BusVolume(bus, &vol, &fin) == 0 && vol < 0.02f && !g_warnedVolume) {
        g_warnedVolume = true;
        Log::Write("Game audio: the volume of %s is %.2f - the radio volume in the game's audio settings mutes the radio in the cab", path.c_str(), vol);
    }
    FSys* core = nullptr;
    if (A.GetCore(g_sys, &core) != 0 || !core) { g_retryAt = now + 2000; return; }
    ExInfo ex{};
    ex.numchannels = 2; ex.defaultfrequency = 48000; ex.format = kFormatFloat;
    ex.decodebuffersize = 1024;                  // frames per callback: ~21 ms, keeps the radio in time with the screen
    ex.length = 48000 * 2 * sizeof(float) * 4;   // 4 s, looping = endless
    ex.pcmreadcallback = &PcmRead;
    FRESULT r = -1;
    for (int size : {(int)sizeof(ExInfo), (int)sizeof(ExInfo) - 8, (int)sizeof(ExInfo) + 8, (int)sizeof(ExInfo) + 16}) {
        ex.cbsize = size;
        r = A.CreateSound(core, nullptr, kOpenUser | kCreateStream | kLoopNormal | k2D, &ex, &g_sound);
        if (r == 0) break;
    }
    if (r != 0 || !g_sound) { g_sound = nullptr; g_state = -1; Log::Write("Game audio: cannot create the radio stream (FMOD error %d) - sound stays on the audio device", r); return; }
    r = A.PlaySound(core, g_sound, group, 1, &g_channel);
    if (r != 0 || !g_channel) { StopOurs(); g_state = -1; Log::Write("Game audio: cannot play on %s (FMOD error %d)", path.c_str(), r); return; }
    A.ChSetVolume(g_channel, 1.f);
    A.ChSetPaused(g_channel, 0);
    g_state = 2;
    Log::Write("Game audio: the radio plays in the cab (%s, bus volume %.2f)", path.c_str(), vol);
}

void MuteGameRadio(ULONGLONG now) {
    if (now < g_scanAt) return;
    g_scanAt = now + 400;
    bool want = g_muteGame && g_audible;
    if (!want) {
        if (!g_muted.empty()) {
            for (FChannel* c : g_muted) A.ChSetMute(c, 0);   // FMOD ignores channels that ended (invalid handle)
            g_muted.clear();
            Log::Write("Game audio: the game's radio is audible again");
        }
        return;
    }
    FGroup* g = BusGroup(g_radioBus, g_radioLocked, "bus:/radio");
    if (!g) return;
    std::vector<FChannel*> chans;
    Channels(g, chans);
    for (FChannel* c : chans) {
        if (c == g_channel) continue;
        int playing = 0, muted = 0;
        if (A.ChIsPlaying(c, &playing) != 0 || !playing) continue;
        if (A.ChGetMute(c, &muted) == 0 && muted) continue;
        if (A.ChSetMute(c, 1) == 0) {
            g_muted.push_back(c);
            if (!g_loggedMute) { g_loggedMute = true; Log::Write("Game audio: the game's own radio is muted while Realistic Radio plays"); }
        }
    }
    if (g_muted.size() > 64) g_muted.erase(g_muted.begin(), g_muted.begin() + 32);
}

void OnUpdate(SSys* sys) {
    if (!g_apiOk) return;
    if (!g_sys) { g_sys = sys; Log::Write("Game audio: found the game's sound system (FMOD Studio)"); LogBuses(sys); }
    if (sys != g_sys) return;
    ULONGLONG now = GetTickCount64();
    bool pullSet; { std::lock_guard<std::mutex> l(g_pullMtx); pullSet = g_pull != nullptr; }
    bool want = g_cabin && pullSet && g_state != -1;
    if (want && !g_channel && now >= g_retryAt) StartOurs(now);
    else if (!want && g_channel) { StopOurs(); if (g_state != -1) g_state = g_cabin ? 1 : 0; Log::Write("Game audio: cab output off"); }
    if (g_channel) {   // still alive? (the game may have reset its sound system)
        int playing = 0;
        if (A.ChIsPlaying(g_channel, &playing) != 0 || !playing) { g_channel = nullptr; if (g_sound) { A.SoundRelease(g_sound); g_sound = nullptr; } g_state = 1; g_retryAt = now + 500; }
    }
    MuteGameRadio(now);
    WatchMix(now);
}

FRESULT HookUpdate(SSys* sys) {
    OnUpdate(sys);
    return g_origUpdate(sys);
}

void Install() {
    if (g_hooked.exchange(true)) return;
    HMODULE st = GetModuleHandleW(L"fmodstudio.dll");
    if (!st) { Log::Write("Game audio: fmodstudio.dll not loaded - not possible"); g_state = -1; return; }
    if (!(g_apiOk = LoadApi())) { Log::Write("Game audio: FMOD functions not found - not possible"); g_state = -1; return; }
    void* update = (void*)GetProcAddress(st, "?update@System@Studio@FMOD@@QEAA?AW4FMOD_RESULT@@XZ");
    if (!update) update = (void*)GetProcAddress(st, "FMOD_Studio_System_Update");
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log::Write("Game audio: hooks not available (%s)", MH_StatusToString(s)); g_state = -1; return; }
    if (!update || (s = MH_CreateHook(update, (void*)&HookUpdate, (LPVOID*)&g_origUpdate)) != MH_OK || (s = MH_EnableHook(update)) != MH_OK) {
        Log::Write("Game audio: cannot attach to the game's sound system (%s)", MH_StatusToString(s)); g_state = -1; return;
    }
    HMODULE self; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&HookUpdate, &self);   // FMOD may call us until the end
    Log::Write("Game audio: waiting for the game's sound system");
}
} // namespace

namespace GameAudio {
void Configure(bool cabin, bool muteGameRadio, const std::string& bus, PullFn pull, void* user) {
    { std::lock_guard<std::mutex> l(g_cfgMtx); g_busPath = bus.empty() ? "bus:/radio" : bus; }
    { std::lock_guard<std::mutex> l(g_pullMtx); g_pull = pull; g_user = user; }
    g_cabin = cabin; g_muteGame = muteGameRadio;
    if (cabin && g_state == 0) g_state = 1;
    if (!cabin && g_state == 1) g_state = 0;
    if ((cabin || muteGameRadio || g_diag) && pull) Install();
}
void WatchListener(bool on) { if (on && !g_diag.exchange(true)) Install(); else if (!on) g_diag = false; }
bool Listener(float pos[3], unsigned maxAgeMs) {
    ULONGLONG at = g_listenerAt.load();
    if (!at || GetTickCount64() - at > maxAgeMs) return false;
    pos[0] = g_lx; pos[1] = g_ly; pos[2] = g_lz;
    return true;
}
bool CabState(float& cabinOut, float& windowLeft, float& windowRight) {
    cabinOut = g_cabinOut; windowLeft = g_wndLeft; windowRight = g_wndRight;
    return cabinOut >= 0.f;
}
void CabMix(float& windowLeft, float& windowRight, float& windowOut, float& interior) { windowLeft = g_winL; windowRight = g_winR; windowOut = g_winOut; interior = g_interior; }
void SetRadioAudible(bool on) { g_audible = on; }
bool Playing() { return g_state.load() == 2; }
int State() { return g_state.load(); }
}
