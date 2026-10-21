// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Phone.h"
#include "D3D11Overlay.h"
#include "InputBlock.h"
#include "MenuInput.h"
#include "Logger.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace {
const size_t kMaxMsgs = 60, kMaxCalls = 40;
const ULONGLONG kRingMs = 25000;   // unanswered after 25 s: missed call, the text goes to the mailbox
}

void Phone::Init(const std::wstring& file, PhoneHost host) {
    host_ = std::move(host); file_ = file;
    Load();
}

std::wstring Phone::TimeText(unsigned t) const {
    if (!t) return L"";
    unsigned now = host_.gameTime ? host_.gameTime() : 0;
    wchar_t b[32];
    swprintf(b, 32, L"%02u:%02u", (t / 60) % 24, t % 60);
    if (now && now / 1440 != t / 1440) {   // another day: weekday in front
        static const wchar_t* de[] = {L"Mo", L"Di", L"Mi", L"Do", L"Fr", L"Sa", L"So"}, *en[] = {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"};
        bool e = host_.english && host_.english();
        return std::wstring(e ? en[(t / 1440) % 7] : de[(t / 1440) % 7]) + L" " + b;
    }
    return b;
}

void Phone::AddMessage(const std::wstring& from, const std::wstring& text) {
    msgs_.push_front({from, text, host_.gameTime ? host_.gameTime() : 0, false});
    while (msgs_.size() > kMaxMsgs) msgs_.pop_back();
    saveDue_ = true; dirty_ = true;
}

void Phone::Call(const std::wstring& from, const std::wstring& text) {
    if (call_.state != CallState::None) {   // busy: the second caller leaves a message
        AddMessage(from, (host_.english() ? L"Voicemail: " : L"Mailbox: ") + text);
        calls_.push_front({from, text, host_.gameTime(), 0});
        return;
    }
    call_ = Call_{};
    call_.state = CallState::Ringing; call_.from = from; call_.text = text; call_.time = host_.gameTime(); call_.at = GetTickCount64();
    const PhoneConfig& ph = host_.settings();
    if (ph.voice) { call_.speechId = nextSpeechId_++; Speech::Request(call_.speechId, text, host_.english() ? "en" : "de"); }   // ready when answered
    host_.play(Speech::Ringtone(), true, 0.32f);
    host_.callBanner(from, text);
    if (open_) { screen_ = PhoneScreen::Incoming; sel_ = 0; }
    dirty_ = true;
    Log::Write("Phone: incoming call from %s", WideToUtf8(from).c_str());
}

void Phone::Accept() {
    if (call_.state != CallState::Ringing) return;
    host_.stopSound();
    call_.state = CallState::Connecting; call_.talkAt = GetTickCount64();
    screen_ = PhoneScreen::InCall; dirty_ = true;
    Log::Write("Phone: call accepted");
}
void Phone::Decline() {
    if (call_.state == CallState::Ringing) { EndCall(2); return; }
    if (call_.state == CallState::Connecting || call_.state == CallState::Talking) EndCall(1);
}
void Phone::EndCall(int kind) {
    host_.stopSound();
    calls_.push_front({call_.from, call_.text, call_.time, kind});
    while (calls_.size() > kMaxCalls) calls_.pop_back();
    if (kind != 1) AddMessage(call_.from, (host_.english() ? L"Voicemail: " : L"Mailbox: ") + call_.text);   // not answered: the text is not lost
    Log::Write(kind == 0 ? "Phone: missed call" : kind == 1 ? "Phone: call ended" : "Phone: call declined");
    call_ = Call_{};
    if (screen_ == PhoneScreen::Incoming || screen_ == PhoneScreen::InCall) { screen_ = PhoneScreen::Home; sel_ = 0; }
    saveDue_ = true; dirty_ = true;
}

void Phone::Update(ULONGLONG now) {
    // voices that are ready
    int id; Speech::Clip clip;
    while (Speech::Take(id, clip)) {
        if (id == call_.speechId) { call_.clip = clip; call_.clipDone = true; }
        else if (id == replayId_ && clip) { host_.play(clip, false, 0.9f); replayId_ = -1; }
    }
    switch (call_.state) {
    case CallState::Ringing:
        if (now - call_.at > kRingMs) EndCall(0);
        break;
    case CallState::Connecting:   // answered: the voice starts as soon as it is ready (text only without a voice)
        if (call_.clipDone || !host_.settings().voice || now - call_.talkAt > 6000) {
            call_.state = CallState::Talking; call_.talkAt = now;
            if (call_.clip) host_.play(call_.clip, false, 0.9f);
            dirty_ = true;
        }
        break;
    case CallState::Talking: {
        bool done = call_.clip ? (!host_.soundPlaying() && now - call_.talkAt > 1500) : now - call_.talkAt > 4000 + call_.text.size() * 60;
        if (done) EndCall(1);
        break; }
    default: break;
    }
    if (open_) {   // clock and call timer
        PhoneView v = View();
        std::wstring stamp = v.clock + v.callState;
        if (stamp != lastClock_) { lastClock_ = stamp; dirty_ = true; }
        if (screen_ == PhoneScreen::Music && now - lastRender_ > 1000) dirty_ = true;   // running time
    }
    if (saveDue_ && now >= saveAt_) { saveDue_ = false; saveAt_ = now + 5000; Save(); }
}

std::vector<std::wstring> Phone::Senders() const {
    std::vector<std::wstring> s;
    for (auto& m : msgs_) if (std::find(s.begin(), s.end(), m.from) == s.end()) s.push_back(m.from);
    return s;
}

int Phone::ItemCount() const {
    switch (screen_) {
    case PhoneScreen::Home: return 4;
    case PhoneScreen::Messages: return (int)Senders().size();
    case PhoneScreen::Calls: return (int)calls_.size();
    case PhoneScreen::Music: { PhoneView v; v.en = host_.english(); host_.fillMusic(v); return (int)v.musicFolders.size(); }
    case PhoneScreen::Incoming: return 2;
    default: return 0;
    }
}

PhoneView Phone::View() const {
    PhoneView v;
    v.en = host_.english();
    v.design = host_.design();
    const PhoneConfig& ph = host_.settings();
    v.device = Utf8ToWide(ph.name);
    unsigned t = host_.gameTime();
    if (t) {
        wchar_t b[16]; swprintf(b, 16, L"%02u:%02u", (t / 60) % 24, t % 60); v.clock = b;
        static const wchar_t* de[] = {L"Montag", L"Dienstag", L"Mittwoch", L"Donnerstag", L"Freitag", L"Samstag", L"Sonntag"};
        static const wchar_t* en[] = {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"};
        v.day = v.en ? en[(t / 1440) % 7] : de[(t / 1440) % 7];
    } else v.clock = L"--:--";
    for (auto& m : msgs_) if (!m.read) ++v.unreadMessages;
    for (auto& c : calls_) if (c.kind == 0) ++v.missedCalls;
    v.screen = screen_; v.sel = sel_; v.scroll = scroll_;
    const bool e = v.en;
    switch (screen_) {
    case PhoneScreen::Home: v.hint = e ? L"Esc: close  •  1-4: app" : L"Esc: schließen  •  1-4: App"; break;
    case PhoneScreen::Messages:
        v.title = e ? L"Messages" : L"Nachrichten";
        for (auto& s : Senders()) {
            for (auto& m : msgs_) if (m.from == s) {
                bool unread = false; for (auto& k : msgs_) if (k.from == s && !k.read) unread = true;
                v.items.push_back({s, m.text, TimeText(m.time), 0, unread});
                break;
            }
        }
        if (v.items.empty()) v.hint = e ? L"No messages" : L"Keine Nachrichten";
        break;
    case PhoneScreen::Thread: {
        auto s = Senders();
        if (thread_ >= 0 && thread_ < (int)s.size()) {
            v.title = s[thread_];
            for (auto it = msgs_.rbegin(); it != msgs_.rend(); ++it) if (it->from == s[thread_]) { v.bubbles.push_back(it->text); v.bubbleTimes.push_back(TimeText(it->time)); }
        }
        break; }
    case PhoneScreen::Calls:
        v.title = e ? L"Phone" : L"Telefon";
        for (auto& c : calls_) {   // kind 3 = missed, already seen
            std::wstring st = (c.kind == 0 || c.kind == 3) ? (e ? L"Missed" : L"Verpasst") : c.kind == 1 ? (e ? L"Incoming" : L"Eingehend") : (e ? L"Declined" : L"Abgelehnt");
            v.items.push_back({c.from, st, TimeText(c.time), c.kind == 3 ? 0 : c.kind, c.kind == 0});
        }
        if (v.items.empty()) v.hint = e ? L"No calls yet" : L"Noch keine Anrufe";
        break;
    case PhoneScreen::CallDetail:
        if (detail_ >= 0 && detail_ < (int)calls_.size()) {
            v.title = e ? L"Call" : L"Anruf";
            v.caller = calls_[detail_].from; v.callText = calls_[detail_].text; v.callState = TimeText(calls_[detail_].time);
        }
        break;
    case PhoneScreen::Incoming:
        v.caller = call_.from;
        v.callState = e ? L"Incoming call …" : L"Eingehender Anruf …";
        v.hint = e ? L"Enter: answer  •  Backspace: decline" : L"Enter: annehmen  •  Rücktaste: ablehnen";
        break;
    case PhoneScreen::InCall: {
        v.caller = call_.from;
        if (call_.state == CallState::Connecting) v.callState = e ? L"Connecting …" : L"Verbinde …";
        else {
            unsigned s = (unsigned)((GetTickCount64() - call_.talkAt) / 1000);
            wchar_t b[16]; swprintf(b, 16, L"%02u:%02u", s / 60, s % 60); v.callState = b;
            v.callText = call_.text;
        }
        break; }
    case PhoneScreen::Job:
        v.title = e ? L"Job" : L"Auftrag";
        host_.fillJob(v);
        break;
    case PhoneScreen::Music:
        v.title = L"Bluetooth Audio";
        host_.fillMusic(v);
        if (v.musicFolders.empty()) v.hint = e ? L"Settings menu → USB / CD: set a music folder to Bluetooth" : L"Einstellungsmenü → USB / CD: einen Musikordner auf Bluetooth stellen";
        break;
    }
    return v;
}

void Phone::Open() {
    if (open_) return;
    renderH_ = D3D11Overlay::ScreenHeight() >= 200 ? D3D11Overlay::ScreenHeight() : 1080;
    float cx = std::max(1, D3D11Overlay::ScreenWidth()) * 0.8f, cy = renderH_ * 0.7f;
    InputBlock::Install();
    InputBlock::AttachWindow(GetForegroundWindow());
    InputBlock::SetMouseBlocked(true);
    bool hooks = MenuInput::Start(cx, cy);
    mouse_ = hooks && MenuInput::MouseActive();
    D3D11Overlay::SetCursorImage(RenderMenuCursor(renderH_));
    D3D11Overlay::SetCursorVisible(mouse_);
    open_ = true; dirty_ = true;
    if (call_.state == CallState::Ringing) screen_ = PhoneScreen::Incoming;
    else if (call_.state != CallState::None) screen_ = PhoneScreen::InCall;
    else if (screen_ == PhoneScreen::Incoming || screen_ == PhoneScreen::InCall) screen_ = PhoneScreen::Home;
    sel_ = 0;
    Log::Write("Phone opened");
}
void Phone::Close() {
    if (!open_) return;
    MenuInput::Stop();
    InputBlock::SetMouseBlocked(false);
    D3D11Overlay::SetCursorVisible(false);
    open_ = false;
    Log::Write("Phone closed");
}

void Phone::Back() {
    switch (screen_) {
    case PhoneScreen::Thread: screen_ = PhoneScreen::Messages; sel_ = std::max(0, thread_); break;
    case PhoneScreen::CallDetail: screen_ = PhoneScreen::Calls; sel_ = std::max(0, detail_); break;
    case PhoneScreen::Home: Close(); return;
    case PhoneScreen::Incoming: Decline(); return;
    case PhoneScreen::InCall: screen_ = PhoneScreen::Home; sel_ = 0; break;   // the call goes on in the background
    default: screen_ = PhoneScreen::Home; sel_ = 0; break;
    }
    scroll_ = 0; dirty_ = true;
}

void Phone::Act(int action, int index) {
    dirty_ = true;
    switch (action) {
    case PA_Back: Back(); break;
    case PA_Close: Close(); break;
    case PA_App: {
        static const PhoneScreen apps[] = {PhoneScreen::Messages, PhoneScreen::Calls, PhoneScreen::Job, PhoneScreen::Music};
        if (index >= 0 && index < 4) { screen_ = apps[index]; sel_ = 0; scroll_ = 0; }
        break; }
    case PA_Item:
        if (screen_ == PhoneScreen::Messages) {
            auto s = Senders();
            if (index >= 0 && index < (int)s.size()) {
                thread_ = index; screen_ = PhoneScreen::Thread;
                for (auto& m : msgs_) if (m.from == s[index] && !m.read) { m.read = true; saveDue_ = true; }
            }
        } else if (screen_ == PhoneScreen::Calls && index >= 0 && index < (int)calls_.size()) {
            detail_ = index; screen_ = PhoneScreen::CallDetail;
            if (calls_[index].kind == 0) { calls_[index].kind = 3; saveDue_ = true; }   // seen
        }
        break;
    case PA_Folder: host_.musicFolder(index); break;
    case PA_Accept: Accept(); break;
    case PA_Decline: case PA_Hangup: Decline(); break;
    case PA_Replay:
        if (detail_ >= 0 && detail_ < (int)calls_.size()) {
            replayId_ = nextSpeechId_++;
            Speech::Request(replayId_, calls_[detail_].text, host_.english() ? "en" : "de");
        }
        break;
    case PA_Prev: host_.musicControl(-1); break;
    case PA_PlayPause: host_.musicControl(0); break;
    case PA_Next: host_.musicControl(1); break;
    default: break;
    }
}

void Phone::Tick(InputManager&) {
    if (!open_) return;
    MenuInput::Event e;
    const KeyBinding k = host_.key();
    while (open_ && MenuInput::Pop(e)) {
        if (e.type == MenuInput::Event::Key) {
            if (e.vk == k.vk && e.ctrl == k.ctrl && e.shift == k.shift && e.alt == k.alt) { Close(); break; }   // the phone key closes it
            int n = ItemCount();
            switch (e.vk) {
            case VK_ESCAPE: Close(); break;
            case VK_BACK: case VK_LEFT: Back(); break;
            case VK_UP: if (n > 0) { sel_ = (sel_ + n - 1) % n; dirty_ = true; } break;
            case VK_DOWN: if (n > 0) { sel_ = (sel_ + 1) % n; dirty_ = true; } break;
            case VK_RETURN: case VK_SPACE: case VK_RIGHT:
                if (screen_ == PhoneScreen::Home) Act(PA_App, sel_);
                else if (screen_ == PhoneScreen::Incoming) { if (sel_ == 1) Decline(); else Accept(); }
                else if (screen_ == PhoneScreen::Music) Act(PA_Folder, sel_);
                else if (screen_ == PhoneScreen::CallDetail) Act(PA_Replay, 0);
                else if (n > 0) Act(PA_Item, sel_);
                break;
            case VK_DELETE: if (screen_ == PhoneScreen::Incoming || screen_ == PhoneScreen::InCall) Decline(); break;
            case 'P': if (screen_ == PhoneScreen::Music) Act(PA_PlayPause, 0); break;
            default:
                if (screen_ == PhoneScreen::Home && e.vk >= '1' && e.vk <= '4') Act(PA_App, e.vk - '1');
                break;
            }
            continue;
        }
        if (e.type != MenuInput::Event::MouseDown || e.button != 0) {
            if (e.type == MenuInput::Event::MouseDown && e.button == 1) Back();   // right click: back
            if (e.type == MenuInput::Event::Wheel && ItemCount() > 0) { scroll_ = std::max(0, scroll_ - e.wheel); dirty_ = true; }
            continue;
        }
        float cx, cy, ix, iy, iw, ih;
        D3D11Overlay::CursorPos(cx, cy);
        if (!D3D11Overlay::ImageRect(ix, iy, iw, ih)) continue;
        float x = (cx - ix) * (iw > 0 && imgW_ ? imgW_ / iw : 1.f), y = (cy - iy) * (ih > 0 && imgH_ ? imgH_ / ih : 1.f);   // screen -> picture pixels
        for (auto& s : spots_) if (x >= s.r.left && x < s.r.right && y >= s.r.top && y < s.r.bottom) { Act(s.action, s.index); break; }
    }
}

std::shared_ptr<OverlayImage> Phone::TakeImage() {
    if (!open_ || !dirty_) return nullptr;
    dirty_ = false; lastRender_ = GetTickCount64();
    PhoneView v = View();
    std::vector<PhoneSpot> spots;
    auto img = RenderPhone(v, renderH_, &spots);
    spots_ = spots;
    if (img) { imgW_ = (float)img->width; imgH_ = (float)img->height; }
    return img;
}

void Phone::Load() {
    std::ifstream f{std::filesystem::path(file_)};
    if (!f) return;
    std::stringstream ss; ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    if (j.contains("messages") && j["messages"].is_array())
        for (auto& m : j["messages"]) if (m.is_object())
            msgs_.push_back({Utf8ToWide(m.value("from", std::string())), Utf8ToWide(m.value("text", std::string())), m.value("time", 0u), m.value("read", true)});
    if (j.contains("calls") && j["calls"].is_array())
        for (auto& c : j["calls"]) if (c.is_object())
            calls_.push_back({Utf8ToWide(c.value("from", std::string())), Utf8ToWide(c.value("text", std::string())), c.value("time", 0u), c.value("kind", 1)});
    while (msgs_.size() > kMaxMsgs) msgs_.pop_back();
    while (calls_.size() > kMaxCalls) calls_.pop_back();
}
void Phone::Save() {
    json j; j["messages"] = json::array(); j["calls"] = json::array();
    for (auto& m : msgs_) j["messages"].push_back({{"from", WideToUtf8(m.from)}, {"text", WideToUtf8(m.text)}, {"time", m.time}, {"read", m.read}});
    for (auto& c : calls_) j["calls"].push_back({{"from", WideToUtf8(c.from)}, {"text", WideToUtf8(c.text)}, {"time", c.time}, {"kind", c.kind}});
    std::ofstream f{std::filesystem::path(file_), std::ios::binary | std::ios::trunc};
    if (f) f << j.dump(1);
}
void Phone::Shutdown() {
    if (call_.state != CallState::None) host_.stopSound();
    Close();
    Save();
    Speech::Shutdown();
}
