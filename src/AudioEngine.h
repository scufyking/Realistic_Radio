// ETS2 Local Radio Native
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
    void PlayUrl(const std::string& url);   // replaces current stream (old one is stopped asynchronously)
    void StopStream();
    void SetStreamGain(float g, float seconds);
    void SetStaticGain(float g, float seconds);
    void SetSwitchGain(float g, float seconds);
    float StreamGain() const;
    void SetMaster(float v) { master_ = v; }
    void SetMuted(bool m) { muted_ = m; }
    bool Running() const { return device_ok_; }
    bool IsBuffering();
    int StreamFailures();
private:
    struct Ramp { std::atomic<float> target{0}; std::atomic<float> step{1}; float cur = 0; float Next() {
        float t = target, s = step; if (cur < t) cur = std::min(t, cur + s); else if (cur > t) cur = std::max(t, cur - s); return cur; } };
    static void Callback(ma_device* d, void* out, const void*, ma_uint32 frames);
    void Mix(float* out, ma_uint32 frames);
    static void SetRamp(Ramp& r, float g, float seconds);
    static std::vector<float> LoadClip(const std::wstring& path);

    ma_device device_{};
    bool device_ok_ = false;
    std::vector<float> static_, switch_;
    size_t staticPos_ = 0, switchPos_ = 0;
    Ramp gStream_, gStatic_, gSwitch_;
    std::atomic<float> master_{0.8f};
    std::atomic<bool> muted_{false};
    std::mutex sessMtx_;
    std::shared_ptr<StreamSession> session_;
    std::vector<std::shared_ptr<StreamSession>> retired_;
    int reconnectMax_ = 30;
    std::vector<float> tmp_;
};
