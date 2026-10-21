// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// The virtual phone (1.6.0): a smartphone that comes up in the corner of the screen with its key (like in GTA).
// Apps: messages, phone (calls from the dispatcher, spoken by a Windows voice), job with a small map, music from the
// music folders marked as Bluetooth. Messages and calls are kept in phone-<game>.json. While the phone is open,
// keyboard and mouse go to the phone (like the settings menu).
#include "Common.h"
#include "Config.h"
#include "OverlayImage.h"
#include "Speech.h"
#include <deque>
#include <functional>

class InputManager;

struct PhoneHost {   // what the phone needs from the radio
    std::function<bool()> english;
    std::function<unsigned()> gameTime;                 // absolute game minutes (0 = unknown)
    std::function<void(PhoneView&)> fillJob, fillMusic;
    std::function<void(int)> musicFolder;               // play this Bluetooth folder
    std::function<void(int)> musicControl;              // -1 previous, 0 play / pause, 1 next
    std::function<const PhoneConfig&()> settings;
    std::function<std::string()> design;
    std::function<KeyBinding()> key;                    // the phone key (closes the phone again)
    std::function<void(Speech::Clip, bool loop, float gain)> play;
    std::function<void()> stopSound;
    std::function<bool()> soundPlaying;
    std::function<void(const std::wstring& caller, const std::wstring& text)> callBanner;   // incoming call on the navigation screen
};

class Phone {
public:
    void Init(const std::wstring& file, PhoneHost host);
    // contents
    void AddMessage(const std::wstring& from, const std::wstring& text);
    void Call(const std::wstring& from, const std::wstring& text);   // incoming call (rings, then a voice)
    bool Ringing() const { return call_.state == CallState::Ringing; }
    // screen
    void Open();
    void Close();
    bool IsOpen() const { return open_; }
    void Update(ULONGLONG now);                         // always (call timer, voices, saving)
    void Tick(InputManager& input);                     // while open: keyboard and mouse
    std::shared_ptr<OverlayImage> TakeImage();          // new picture when something changed
    void Shutdown();
private:
    enum class CallState { None, Ringing, Connecting, Talking };
    struct Msg { std::wstring from, text; unsigned time = 0; bool read = false; };
    struct Log_ { std::wstring from, text; unsigned time = 0; int kind = 0; };   // 0 missed, 1 answered, 2 declined
    struct Call_ { CallState state = CallState::None; std::wstring from, text; unsigned time = 0; ULONGLONG at = 0, talkAt = 0; int speechId = -1; Speech::Clip clip; bool clipDone = false; };
    PhoneView View() const;
    std::wstring TimeText(unsigned t) const;
    void Act(int action, int index);
    void Back();
    void Accept();
    void Decline();
    void EndCall(int kind);
    void Load();
    void Save();
    int ItemCount() const;
    std::vector<std::wstring> Senders() const;          // conversations, newest first

    PhoneHost host_;
    std::wstring file_;
    std::deque<Msg> msgs_;                              // newest first
    std::deque<Log_> calls_;
    Call_ call_;
    bool open_ = false, dirty_ = true, mouse_ = false, saveDue_ = false;
    PhoneScreen screen_ = PhoneScreen::Home;
    int sel_ = 0, scroll_ = 0, thread_ = -1, detail_ = -1, renderH_ = 1080, nextSpeechId_ = 1, replayId_ = -1;
    std::vector<PhoneSpot> spots_;
    float imgW_ = 0, imgH_ = 0;
    std::wstring lastClock_;
    ULONGLONG saveAt_ = 0, lastRender_ = 0;
};
