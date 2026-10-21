// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "AudioEngine.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

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
    gTunMul_.target = 1.f; gTunMul_.cur = 1.f; gTunMul_.now = 1.f;   // no tunnel
    static_ = LoadClip(dataDir + L"audio\\whitestatic.mp3");
    switch_ = LoadClip(dataDir + L"audio\\radio_switch.mp3");
    tmp_.resize(kRate * kChannels);
    // virtual phone: two short notification tones (mono, sine with a soft attack and decay)
    auto tone = [](std::vector<float>& v, double start, double len, double f, double amp) {
        size_t s0 = (size_t)(start * kRate), n = (size_t)(len * kRate);
        if (v.size() < s0 + n) v.resize(s0 + n, 0.f);
        for (size_t i = 0; i < n; ++i) {
            double t = (double)i / kRate, env = std::min(1.0, t / 0.006) * std::exp(-t * 9.0);
            v[s0 + i] += (float)(amp * env * (std::sin(6.283185307 * f * t) + 0.25 * std::sin(6.283185307 * 2 * f * t)));
        }
    };
    { std::vector<float> a, b;
      tone(a, 0.00, 0.35, 1046.5, 0.5); tone(a, 0.14, 0.45, 1568.0, 0.5);   // connected: rising
      tone(b, 0.00, 0.30, 1318.5, 0.5); tone(b, 0.16, 0.40, 1318.5, 0.45);  // message: two pings
      chime_[0] = std::make_shared<const std::vector<float>>(std::move(a)); chime_[1] = std::make_shared<const std::vector<float>>(std::move(b)); }
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

void AudioEngine::PlayUrl(const std::string& url, bool once) {
    auto s = std::make_shared<StreamSession>(url, reconnectMax_, once);
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

// The step is taken from the distance between the new target and where the gain is *now* (or was heading).
// Up to 1.2.0 it used only the old target: setting the same target twice while a fade was still running (e.g.
// "static 0" when switching to USB, then again when the first track starts) made the step almost zero, so static or
// the tuning sound stayed audible in USB/CD mode after quick source changes.
void AudioEngine::SetRamp(Ramp& r, float g, float seconds) {
    g = std::clamp(g, 0.f, 1.f);
    float dist = std::max(std::abs(g - r.target.load()), std::abs(g - r.now.load()));
    r.step = seconds <= 0 ? 10.f : std::max(dist, 0.05f) / (seconds * kRate);
    r.target = g;
}
void AudioEngine::SetStreamGain(float g, float s) { SetRamp(gStream_, g, s); }
void AudioEngine::SetStaticGain(float g, float s) { SetRamp(gStatic_, g, s); }
void AudioEngine::SetSwitchGain(float g, float s) { SetRamp(gSwitch_, g, s); }
bool AudioEngine::IsBuffering() { std::lock_guard<std::mutex> l(sessMtx_); return session_ && session_->Buffering(); }
int AudioEngine::StreamFailures() { std::lock_guard<std::mutex> l(sessMtx_); return session_ ? session_->Failures() : 0; }
bool AudioEngine::SongTitle(std::string& out) { std::lock_guard<std::mutex> l(sessMtx_); out.clear(); return session_ && session_->Title(out); }
AudioEngine::TrackState AudioEngine::Track() {
    std::lock_guard<std::mutex> l(sessMtx_);
    TrackState t;
    if (!session_) return t;
    t.active = true; t.ended = session_->Ended(); t.failed = session_->Failures() > 0;
    t.pos = session_->PlayedFrames() / (double)kRate; t.len = session_->LengthFrames() / (double)kRate;
    return t;
}
float AudioEngine::StreamGain() const { return gStream_.target; }

void AudioEngine::Callback(ma_device* d, void* out, const void*, ma_uint32 frames) {
    AudioEngine* e = (AudioEngine*)d->pUserData;
    std::unique_lock<std::mutex> l(e->mixMtx_, std::try_to_lock);
    if (!l.owns_lock() || e->external_) { memset(out, 0, frames * kChannels * sizeof(float)); return; }   // the game plays our sound (cab)
    e->Mix((float*)out, frames);
}
// cab output: FMOD pulls the mix through its stream callback (in pieces of at most the temp buffer)
void AudioEngine::PullMix(float* out, unsigned frames) {
    std::lock_guard<std::mutex> l(mixMtx_);
    const unsigned chunk = (unsigned)(tmp_.size() / kChannels);
    while (frames > 0) {
        unsigned n = std::min(frames, chunk);
        Mix(out, n);
        out += n * kChannels; frames -= n;
    }
}

void AudioEngine::PlayChime(int kind) { if (kind >= 0 && kind < 2) PlayPhone(chime_[kind], false, 0.30f); }
void AudioEngine::PlayPhone(Clip clip, bool loop, float gain) {
    if (!clip || clip->empty()) return;
    std::lock_guard<std::mutex> l(phoneMtx_);
    phoneReq_ = PhoneReq{clip, loop, gain, false, true};
    phoneBusy_ = true;
}
void AudioEngine::StopPhone() {
    std::lock_guard<std::mutex> l(phoneMtx_);
    phoneReq_ = PhoneReq{nullptr, false, 0, true, true};
}

void AudioEngine::Mix(float* out, ma_uint32 frames) {
    {   // phone channel: take a new clip / stop (never block the audio thread)
        std::unique_lock<std::mutex> l(phoneMtx_, std::try_to_lock);
        if (l.owns_lock() && phoneReq_.pending) {
            if (phoneReq_.stop) { phoneCur_.reset(); phoneBusy_ = false; }
            else { phoneCur_ = phoneReq_.clip; phoneLoop_ = phoneReq_.loop; phoneGain_ = phoneReq_.gain; phonePos_ = 0; }
            phoneReq_ = PhoneReq{};
        }
    }
    memset(out, 0, frames * kChannels * sizeof(float));
    ma_uint32 got = 0;
    if (frames > tmp_.size() / kChannels) frames = (ma_uint32)(tmp_.size() / kChannels);
    {   // never block the audio thread
        std::unique_lock<std::mutex> l(sessMtx_, std::try_to_lock);
        if (l.owns_lock() && session_ && !paused_) got = session_->Read(tmp_.data(), frames);
    }
    const float masterTarget = muted_ ? 0.f : std::min(1.f, master_.load() * boost_.load());   // never louder than full volume (no clipping); 0 stays silent
    const float masterStep = 1.f / (0.08f * kRate);   // full range in 80 ms
    // outside the truck: smooth towards the targets over about 0.4 s
    const float og = outGain_.load(), om = outMono_.load(), oc = outCut_.load();
    const float coefTarget = oc >= 19999.f ? 1.f : 1.f - std::exp(-6.2831853f * oc / kRate);
    const float k = 1.f - std::exp(-1.f / (0.12f * kRate));
    for (ma_uint32 i = 0; i < frames; ++i) {
        if (masterCur_ < masterTarget) masterCur_ = std::min(masterTarget, masterCur_ + masterStep);
        else if (masterCur_ > masterTarget) masterCur_ = std::max(masterTarget, masterCur_ - masterStep);
        const float master = masterCur_;
        float gs = gStream_.Next() * gTunMul_.Next(), gn = std::min(1.f, gStatic_.Next() + gTunStatic_.Next()), gw = gSwitch_.Next();
        for (ma_uint32 c = 0; c < kChannels; ++c) {
            float v = 0;
            if (i < got) v += tmp_[i * kChannels + c] * gs;
            if (!static_.empty()) v += static_[(staticPos_ + c) % static_.size()] * gn;
            if (!switch_.empty()) v += switch_[(switchPos_ + c) % switch_.size()] * gw;
            out[i * kChannels + c] = v;
        }
        outGainCur_ += (og - outGainCur_) * k; outMonoCur_ += (om - outMonoCur_) * k; outCoefCur_ += (coefTarget - outCoefCur_) * k;
        if (kChannels == 2 && outMonoCur_ > 0.001f) {   // heard from outside: the stereo picture narrows
            float& l = out[i * 2]; float& r = out[i * 2 + 1];
            float m = (l + r) * 0.5f;
            l += (m - l) * outMonoCur_; r += (m - r) * outMonoCur_;
        }
        for (ma_uint32 c = 0; c < kChannels; ++c) {
            float v = out[i * kChannels + c];
            if (outCoefCur_ < 0.9999f) {   // muffled through the cab: two one-pole low-passes (12 dB / octave)
                lp1_[c] += (v - lp1_[c]) * outCoefCur_;
                lp2_[c] += (lp1_[c] - lp2_[c]) * outCoefCur_;
                v = lp2_[c];
            } else { lp1_[c] = v; lp2_[c] = v; }
            float o = v * master * outGainCur_;
            if (!std::isfinite(o) || master <= 0.f) { o = 0.f; lp1_[c] = lp2_[c] = 0.f; }   // volume 0 is always silence; never pass NaN/Inf to the device
            if (phoneCur_) o += (*phoneCur_)[phonePos_] * phoneGain_ * outGainCur_;   // the phone: its own volume, also with the radio off
            out[i * kChannels + c] = std::clamp(o, -1.f, 1.f);
        }
        if (phoneCur_ && ++phonePos_ >= phoneCur_->size()) {
            if (phoneLoop_) phonePos_ = 0; else { phoneCur_.reset(); phoneBusy_ = false; }
        }
        if (!static_.empty()) staticPos_ = (staticPos_ + kChannels) % static_.size();
        if (!switch_.empty()) switchPos_ = gw > 0 ? (switchPos_ + kChannels) % switch_.size() : 0;
    }
}
