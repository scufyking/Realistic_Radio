// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Speech.h"
#include "Logger.h"
#include <objbase.h>
#include <sapi.h>
#include <atomic>
#include <cmath>
#include <deque>
#include <thread>
#include <condition_variable>

namespace {
// SPDFID_WaveFormatEx (from sapi.lib), defined here so no extra library is needed
const GUID kWaveFormatEx = {0xC31ADBAE, 0x527F, 0x4ff5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C}};
const CLSID kSpVoice = {0x96749377, 0x3391, 0x11d2, {0x9e, 0xe3, 0x00, 0xc0, 0x4f, 0x79, 0x73, 0x96}};
const CLSID kSpStream = {0x715d9c59, 0x4442, 0x11d2, {0x96, 0x05, 0x00, 0xc0, 0x4f, 0x8e, 0xe6, 0x28}};
const IID kISpVoice = {0x6c44df74, 0x72b9, 0x4992, {0xa1, 0xec, 0xef, 0x99, 0x6e, 0x04, 0x22, 0xd4}};
const IID kISpStream = {0x12e3cca9, 0x7518, 0x44c5, {0xa5, 0xe7, 0xba, 0x5a, 0x79, 0xcb, 0x92, 0x9e}};

struct Job { int id; std::wstring text; std::string lang; };
std::mutex g_mtx;
std::deque<Job> g_jobs;
std::deque<std::pair<int, Speech::Clip>> g_done;
std::thread g_thread;
std::atomic<bool> g_quit{false}, g_running{false};
std::condition_variable g_cv;

std::wstring Escape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'<') o += L"&lt;"; else if (c == L'>') o += L"&gt;"; else if (c == L'&') o += L"&amp;"; else if (c == L'"') o += L"&quot;"; else o += c;
    }
    return o;
}

// telephone sound: 300 - 3400 Hz band, a little compression
void PhoneFilter(std::vector<float>& v) {
    const float rate = 48000.f;
    float hpA = std::exp(-6.2831853f * 300.f / rate), lpA = 1.f - std::exp(-6.2831853f * 3400.f / rate);
    float hpX = 0, hpY = 0, lp1 = 0, lp2 = 0;
    for (float& s : v) {
        float y = hpA * (hpY + s - hpX); hpX = s; hpY = y;   // high pass
        lp1 += (y - lp1) * lpA; lp2 += (lp1 - lp2) * lpA;    // low pass (12 dB / octave)
        float c = lp2 * 1.6f;
        s = c / (1.f + std::fabs(c) * 0.6f);                 // soft compression
    }
}

Speech::Clip Render(ISpVoice* voice, const Job& j) {
    IStream* mem = nullptr;
    ISpStream* sp = nullptr;
    Speech::Clip out;
    do {
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem))) break;
        if (FAILED(CoCreateInstance(kSpStream, nullptr, CLSCTX_ALL, kISpStream, (void**)&sp))) break;
        WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1; wf.nSamplesPerSec = 48000; wf.wBitsPerSample = 16;
        wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 96000;
        if (FAILED(sp->SetBaseStream(mem, kWaveFormatEx, &wf))) break;
        if (FAILED(voice->SetOutput(sp, TRUE))) break;
        // prefer a voice of the language (German 407, English 409); without one Windows' default voice speaks
        std::wstring xml = std::wstring(L"<voice optional=\"Language=") + (j.lang == "en" ? L"409" : L"407") + L"\">" + Escape(j.text) + L"</voice>";
        if (FAILED(voice->Speak(xml.c_str(), SPF_IS_XML, nullptr))) break;
        voice->SetOutput(nullptr, TRUE);
        HGLOBAL h = nullptr;
        if (FAILED(GetHGlobalFromStream(mem, &h)) || !h) break;
        STATSTG st{}; mem->Stat(&st, STATFLAG_NONAME);
        size_t bytes = (size_t)st.cbSize.QuadPart;
        const int16_t* p = (const int16_t*)GlobalLock(h);
        if (!p) break;
        auto v = std::make_shared<std::vector<float>>(bytes / 2 + 24000);   // + 0.5 s silence at the end
        for (size_t i = 0; i < bytes / 2; ++i) (*v)[i] = p[i] / 32768.f;
        GlobalUnlock(h);
        PhoneFilter(*v);
        out = v;
    } while (false);
    if (sp) sp->Release();
    if (mem) mem->Release();
    return out;
}

void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(kSpVoice, nullptr, CLSCTX_ALL, kISpVoice, (void**)&voice))) { voice = nullptr; Log::Write("Phone: no Windows voice (SAPI) - calls are shown as text"); }
    while (!g_quit) {
        Job j;
        {
            std::unique_lock<std::mutex> l(g_mtx);
            g_cv.wait(l, [] { return g_quit || !g_jobs.empty(); });
            if (g_quit) break;
            j = g_jobs.front(); g_jobs.pop_front();
        }
        Speech::Clip c = voice ? Render(voice, j) : nullptr;
        if (voice && !c) Log::Write("Phone: the Windows voice could not speak this text");
        std::lock_guard<std::mutex> l(g_mtx);
        g_done.push_back({j.id, c});
    }
    if (voice) voice->Release();
    CoUninitialize();
}
}

namespace Speech {
void Request(int id, const std::wstring& text, const std::string& lang) {
    {
        std::lock_guard<std::mutex> l(g_mtx);
        g_jobs.push_back({id, text, lang});
    }
    if (!g_running.exchange(true)) { g_quit = false; g_thread = std::thread(Worker); }
    g_cv.notify_one();
}
bool Take(int& id, Clip& clip) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_done.empty()) return false;
    id = g_done.front().first; clip = g_done.front().second; g_done.pop_front();
    return true;
}
void Shutdown() {
    if (!g_running) return;
    { std::lock_guard<std::mutex> l(g_mtx); g_quit = true; g_jobs.clear(); }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
    g_running = false;
}
Clip Ringtone() {
    static Clip ring = [] {
        const int rate = 48000;
        auto v = std::make_shared<std::vector<float>>(rate * 3, 0.f);
        // two short bursts of a warm two-tone ring (like a modern phone), then a pause
        auto burst = [&](double start, double len) {
            for (int i = 0; i < (int)(len * rate); ++i) {
                double t = (double)i / rate, env = std::min(1.0, t / 0.01) * std::min(1.0, (len - t) / 0.03);
                double trem = 0.75 + 0.25 * std::sin(6.2831853 * 20 * t);
                (*v)[(size_t)(start * rate) + i] += (float)(0.35 * env * trem * (std::sin(6.2831853 * 880 * t) + 0.6 * std::sin(6.2831853 * 1108.7 * t)));
            }
        };
        burst(0.0, 0.45); burst(0.6, 0.45);
        return Clip(v);
    }();
    return ring;
}
}
