// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "StreamSource.h"
#include <memory>

// Mixes: radio stream + looping static (whitestatic.mp3) + station-switch sound (radio_switch.mp3)
// Replacement for the three <audio> elements (#player, #whitenoise, #switchStation) of the web UI.
class AudioEngine {
public:
    bool Init(const std::wstring& dataDir, int reconnectMaxSeconds);
    void Shutdown();
    void PlayUrl(const std::string& url, bool once = false);   // replaces current stream (old one is stopped asynchronously); once = USB/CD track
    void SetPaused(bool p) { paused_ = p; }    // mixer stops reading the stream (USB/CD pause keeps the position)
    bool Paused() const { return paused_; }
    struct TrackState { bool active = false, ended = false, failed = false; double pos = 0, len = 0; };
    TrackState Track();                        // position/length in seconds of the current once-session
    void StopStream();
    void SetStreamGain(float g, float seconds);
    void SetStaticGain(float g, float seconds);
    void SetSwitchGain(float g, float seconds);
    // tunnel: the programme fades (streamMul 1 -> 0) and extra static comes in (FM); on top of the other gains
    void SetTunnel(float streamMul, float staticAdd, float seconds) { SetRamp(gTunMul_, streamMul, seconds); SetRamp(gTunStatic_, staticAdd, seconds); }
    float StreamGain() const;
    bool NoiseActive() const { return gStatic_.target.load() > 0.f || gSwitch_.target.load() > 0.f || gStatic_.now.load() > 0.f || gSwitch_.now.load() > 0.f; }
    void SetMaster(float v) { float c = std::clamp(v, 0.f, 1.f); master_ = c * c; }   // perceptual: 50 % slider = -12 dB, 10 % = -40 dB
    void SetMuted(bool m) { muted_ = m; }
    void SetBoost(float g) { boost_ = std::clamp(g, 1.f, 2.f); }   // speed-dependent volume: extra gain on top of the master
    bool Running() const { return device_ok_; }
    bool IsBuffering();
    int StreamFailures();
    bool SongTitle(std::string& out);
    // outside the truck: gain, low-pass cut-off (Hz) and how much the stereo picture collapses to mono (0..1); ramps smoothly
    void SetOutside(float gain, float cutoffHz, float mono) { outGain_ = std::clamp(gain, 0.f, 1.f); outCut_ = std::clamp(cutoffHz, 80.f, 20000.f); outMono_ = std::clamp(mono, 0.f, 1.f); }
    void SetExternal(bool on) { external_ = on; }
    // virtual phone: its own sound channel (own volume, plays also with the radio off or muted)
    using Clip = std::shared_ptr<const std::vector<float>>;   // 48 kHz mono
    void PlayChime(int kind);                                  // 0 = connected, 1 = message
    void PlayPhone(Clip clip, bool loop, float gain);          // ringtone (loop) or a voice
    void StopPhone();
    bool PhonePlaying() const { return phoneBusy_; }   // the game's sound system plays the mix (cab output): the device stays silent
    void PullMix(float* out, unsigned frames);       // called by the game's sound system (FMOD stream thread)   // live title of the current stream (false = none sent)
private:
    // Gain that moves towards `target` by `step` per sample (audio thread). `now` mirrors the current value for SetRamp.
    struct Ramp {
        std::atomic<float> target{0}; std::atomic<float> step{1}; std::atomic<float> now{0}; float cur = 0;
        float Next() {
            float t = target.load(std::memory_order_relaxed), s = step.load(std::memory_order_relaxed);
            if (cur < t) cur = std::min(t, cur + s); else if (cur > t) cur = std::max(t, cur - s);
            now.store(cur, std::memory_order_relaxed);
            return cur;
        }
    };
    static void Callback(ma_device* d, void* out, const void*, ma_uint32 frames);
    void Mix(float* out, ma_uint32 frames);
    static void SetRamp(Ramp& r, float g, float seconds);
    static std::vector<float> LoadClip(const std::wstring& path);

    ma_device device_{};
    bool device_ok_ = false;
    std::vector<float> static_, switch_;
    size_t staticPos_ = 0, switchPos_ = 0;
    Ramp gStream_, gStatic_, gSwitch_, gTunMul_, gTunStatic_;
    std::atomic<float> master_{0.f};
    float masterCur_ = 0.f;   // audio thread only: smoothed master (no sudden jumps)
    std::atomic<bool> muted_{false}, paused_{false};
    std::atomic<float> boost_{1.f};
    std::mutex sessMtx_;
    std::shared_ptr<StreamSession> session_;
    std::vector<std::shared_ptr<StreamSession>> retired_;
    int reconnectMax_ = 30;
    std::vector<float> tmp_;
    std::mutex mixMtx_;
    std::atomic<bool> external_{false};
    Clip chime_[2];
    std::mutex phoneMtx_;
    struct PhoneReq { Clip clip; bool loop = false; float gain = 0; bool stop = false; bool pending = false; } phoneReq_;
    Clip phoneCur_; bool phoneLoop_ = false; float phoneGain_ = 0; size_t phonePos_ = 0;   // audio thread only
    std::atomic<bool> phoneBusy_{false};
    std::atomic<float> outGain_{1.f}, outCut_{20000.f}, outMono_{0.f};
    float outGainCur_ = 1.f, outCoefCur_ = 1.f, outMonoCur_ = 0.f, lp1_[2] = {0, 0}, lp2_[2] = {0, 0};   // audio thread only
};
