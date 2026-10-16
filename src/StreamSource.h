// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"
#include "miniaudio.h"
#include <memory>

constexpr ma_uint32 kRate = 48000;
constexpr ma_uint32 kChannels = 2;

// One network/file stream. Runs on its own worker thread, decodes to 48 kHz stereo float into a ring buffer.
// The thread holds a reference on the DLL and leaves via FreeLibraryAndExitThread, so ETS2 can unload
// the plugin even if a blocking network call is still finishing.
class StreamSession : public std::enable_shared_from_this<StreamSession> {
public:
    // once = play a local file a single time (USB/CD mode): no reconnect loop, Ended() becomes true at the end
    StreamSession(const std::string& url, int reconnectMaxSeconds, bool once = false);
    ~StreamSession();
    void Start();
    void Stop();                         // non-blocking
    bool WaitFinished(DWORD ms);
    ma_uint32 Read(float* out, ma_uint32 frames);   // audio thread; returns frames read (rest must be silence)
    bool Buffering() const { return !ready_.load(); }
    int Failures() const { return failures_.load(); }
    bool Ended() const { return ended_.load(); }                       // once mode: everything decoded and played
    uint64_t PlayedFrames() const { return played_.load(); }           // frames handed to the mixer (48 kHz)
    uint64_t LengthFrames() const { return length_.load(); }           // 0 = unknown
    const std::string& Url() const { return url_; }

    // used by the worker implementation
    std::atomic<bool> stop_{false};
    std::atomic<HANDLE> req_{nullptr};
    bool Write(const float* frames, ma_uint32 count);   // blocks while ring buffer is full
private:
    static DWORD WINAPI ThreadProc(LPVOID p);
    void Run();
    bool PlayOnce(const std::string& url, int depth);
    bool PlayMp3(void* http);
    bool PlayMediaFoundation(const std::string& url);
    bool PlayLocalFile(const std::string& path);
    std::string url_;
    int reconnectMax_;
    ma_pcm_rb rb_;
    std::atomic<bool> ready_{false};
    std::atomic<int> failures_{0};
    bool once_ = false;
    std::atomic<bool> decoded_{false}, ended_{false};
    std::atomic<uint64_t> played_{0}, length_{0};
    HANDLE thread_ = nullptr;
    HANDLE done_ = nullptr;
};
