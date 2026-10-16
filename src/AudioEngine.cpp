// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "AudioEngine.h"
#include "Logger.h"
#include <algorithm>

std::vector<float> AudioEngine::LoadClip(const std::wstring& path) {
    std::vector<float> v;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, kRate);
    ma_decoder dec;
    if (ma_decoder_init_file_w(path.c_str(), &cfg, &dec) != MA_SUCCESS) { Log::Write("Cannot load %s", WideToUtf8(path).c_str()); return v; }
    float buf[4096 * kChannels];
    for (;;) { ma_uint64 got = 0; ma_decoder_read_pcm_frames(&dec, buf, 4096, &got); if (!got) break; v.insert(v.end(), buf, buf + got * kChannels); }
    ma_decoder_uninit(&dec);
    return v;
}

bool AudioEngine::Init(const std::wstring& dataDir, int reconnectMaxSeconds) {
    reconnectMax_ = reconnectMaxSeconds;
    static_ = LoadClip(dataDir + L"audio\\whitestatic.mp3");
    switch_ = LoadClip(dataDir + L"audio\\radio_switch.mp3");
    tmp_.resize(kRate * kChannels);
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32; cfg.playback.channels = kChannels; cfg.sampleRate = kRate;
    cfg.dataCallback = Callback; cfg.pUserData = this;
    if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) { Log::Write("Audio device init failed"); return false; }
    if (ma_device_start(&device_) != MA_SUCCESS) { ma_device_uninit(&device_); Log::Write("Audio device start failed"); return false; }
    device_ok_ = true;
    Log::Write("Audio device: %s", device_.playback.name);
    return true;
}

void AudioEngine::Shutdown() {
    std::vector<std::shared_ptr<StreamSession>> all;
    { std::lock_guard<std::mutex> l(sessMtx_); if (session_) all.push_back(session_); session_.reset(); all.insert(all.end(), retired_.begin(), retired_.end()); retired_.clear(); }
    for (auto& s : all) s->Stop();
    for (auto& s : all) s->WaitFinished(3000);   // threads hold a DLL reference, so a slow one cannot crash on unload
    if (device_ok_) { ma_device_uninit(&device_); device_ok_ = false; }
}

void AudioEngine::PlayUrl(const std::string& url) {
    auto s = std::make_shared<StreamSession>(url, reconnectMax_);
    s->Start();
    std::lock_guard<std::mutex> l(sessMtx_);
    if (session_) { session_->Stop(); retired_.push_back(session_); }
    session_ = s;
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [](auto& r) { return r->WaitFinished(0); }), retired_.end());
}
void AudioEngine::StopStream() {
    std::lock_guard<std::mutex> l(sessMtx_);
    if (session_) { session_->Stop(); retired_.push_back(session_); session_.reset(); }
}

void AudioEngine::SetRamp(Ramp& r, float g, float seconds) {
    r.step = seconds <= 0 ? 10.f : std::max(1e-6f, std::abs(g - r.target.load() + 1e-6f) / (seconds * kRate));
    r.target = std::clamp(g, 0.f, 1.f);
}
void AudioEngine::SetStreamGain(float g, float s) { SetRamp(gStream_, g, s); }
void AudioEngine::SetStaticGain(float g, float s) { SetRamp(gStatic_, g, s); }
void AudioEngine::SetSwitchGain(float g, float s) { SetRamp(gSwitch_, g, s); }
bool AudioEngine::IsBuffering() { std::lock_guard<std::mutex> l(sessMtx_); return session_ && session_->Buffering(); }
int AudioEngine::StreamFailures() { std::lock_guard<std::mutex> l(sessMtx_); return session_ ? session_->Failures() : 0; }
float AudioEngine::StreamGain() const { return gStream_.target; }

void AudioEngine::Callback(ma_device* d, void* out, const void*, ma_uint32 frames) { ((AudioEngine*)d->pUserData)->Mix((float*)out, frames); }

void AudioEngine::Mix(float* out, ma_uint32 frames) {
    memset(out, 0, frames * kChannels * sizeof(float));
    ma_uint32 got = 0;
    if (frames > tmp_.size() / kChannels) frames = (ma_uint32)(tmp_.size() / kChannels);
    {   // never block the audio thread
        std::unique_lock<std::mutex> l(sessMtx_, std::try_to_lock);
        if (l.owns_lock() && session_) got = session_->Read(tmp_.data(), frames);
    }
    float master = muted_ ? 0.f : master_.load();
    for (ma_uint32 i = 0; i < frames; ++i) {
        float gs = gStream_.Next(), gn = gStatic_.Next(), gw = gSwitch_.Next();
        for (ma_uint32 c = 0; c < kChannels; ++c) {
            float v = 0;
            if (i < got) v += tmp_[i * kChannels + c] * gs;
            if (!static_.empty()) v += static_[(staticPos_ + c) % static_.size()] * gn;
            if (!switch_.empty()) v += switch_[(switchPos_ + c) % switch_.size()] * gw;
            out[i * kChannels + c] = std::clamp(v * master, -1.f, 1.f);
        }
        if (!static_.empty()) staticPos_ = (staticPos_ + kChannels) % static_.size();
        if (!switch_.empty()) switchPos_ = gw > 0 ? (switchPos_ + kChannels) % switch_.size() : 0;
    }
}
