// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "StreamSource.h"
#include "Logger.h"
#include <winhttp.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>
#include <cstring>
#include <thread>
#include "NetReader.h"

namespace {
struct Http {
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    std::string contentType;
    int metaint = 0, kbps = 0;      // ICY: audio bytes between two metadata blocks, bit rate (icy-br)
    StreamSession* owner = nullptr;
    bool abortable = true;          // Stop() may close this request (not the title poller's own request)
    ~Http() { Close(); }
    void Close() {
        if (owner && abortable) owner->req_.store(nullptr);
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
        request = connect = session = nullptr;
    }
    // returns bytes read, 0 on end/error
    DWORD Read(void* buf, DWORD size) {
        DWORD got = 0;
        if (!request || owner->stop_) return 0;
        if (!WinHttpReadData(request, buf, size, &got)) return 0;
        return got;
    }
};

bool HttpOpen(Http& h, const std::string& url) {
    std::wstring w = Utf8ToWide(url);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[512], path[4096];
    uc.lpszHostName = host; uc.dwHostNameLength = 512; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    wchar_t extra[4096]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) { Log::Write("Invalid URL: %s", url.c_str()); return false; }
    h.session = WinHttpOpen(L"RealisticRadio/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) return false;
    WinHttpSetTimeouts(h.session, 10000, 10000, 10000, 15000);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(h.session, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /*TLS1_3*/;
    if (!WinHttpSetOption(h.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(h.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    h.connect = WinHttpConnect(h.session, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!h.connect) return false;
    std::wstring obj = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    if (obj.empty()) obj = L"/";
    h.request = WinHttpOpenRequest(h.connect, L"GET", obj.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!h.request) return false;
    if (h.abortable) h.owner->req_.store(h.request);
    // "Icy-MetaData: 1": the server puts the song title into the stream every icy-metaint bytes (only when the live song
    // title is switched on; without it servers send pure audio)
    const bool icy = StreamSession::MetadataEnabled();
    if (!WinHttpSendRequest(h.request, icy ? L"Icy-MetaData: 1\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS, icy ? (DWORD)-1L : 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(h.request, nullptr)) {
        Log::Write("HTTP request failed for %s (error %lu)", url.c_str(), GetLastError());
        return false;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status >= 400) { Log::Write("HTTP %lu for %s", status, url.c_str()); return false; }
    wchar_t ct[256] = {0}; sz = sizeof(ct);
    if (WinHttpQueryHeaders(h.request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ct, &sz, WINHTTP_NO_HEADER_INDEX))
        h.contentType = WideToUtf8(ct);
    std::transform(h.contentType.begin(), h.contentType.end(), h.contentType.begin(), ::tolower);
    if (icy) {
        auto header = [&](const wchar_t* name) {
            wchar_t v[64] = {0}; DWORD n = sizeof(v);
            return WinHttpQueryHeaders(h.request, WINHTTP_QUERY_CUSTOM, name, v, &n, WINHTTP_NO_HEADER_INDEX) ? _wtoi(v) : 0;
        };
        h.metaint = std::clamp(header(L"icy-metaint"), 0, 1 << 20);
        h.kbps = std::clamp(header(L"icy-br"), 0, 1024);
    }
    return true;
}

// --- ICY song title -----------------------------------------------------------------------------------------------
bool ReadFull(Http& h, void* buf, size_t n) {
    size_t got = 0;
    while (got < n) { DWORD k = h.Read((char*)buf + got, (DWORD)(n - got)); if (!k) return false; got += k; }
    return true;
}
bool ValidUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size() + (n == 0 ? 1 : 0)) return false;
        for (int k = 1; k <= n; ++k) if (((unsigned char)s[i + k] >> 6) != 2) return false;
        i += n + 1;
    }
    return true;
}
// StreamTitle='Artist - Title'; -> "Artist - Title" (UTF-8; older servers send Windows-1252). found = the field exists.
std::string IcyTitle(const std::string& meta, bool& found) {
    found = false;
    size_t p = meta.find("StreamTitle='");
    if (p == std::string::npos) return "";
    found = true;
    p += 13;
    size_t e = meta.find("';", p);
    if (e == std::string::npos) e = meta.find('\'', p);
    std::string t = meta.substr(p, e == std::string::npos ? std::string::npos : e - p);
    while (!t.empty() && (t.back() == '\0' || t.back() == ' ')) t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    if (t == "-" || t == " - ") t.clear();
    if (!ValidUtf8(t)) {
        int n = MultiByteToWideChar(1252, 0, t.c_str(), (int)t.size(), nullptr, 0);
        std::wstring w(n, L'\0'); MultiByteToWideChar(1252, 0, t.c_str(), (int)t.size(), &w[0], n);
        t = WideToUtf8(w);
    }
    return t;
}
// Opens the stream once, reads up to the first metadata block and closes again (streams Media Foundation plays)
bool FetchIcyTitle(StreamSession* owner, const std::string& url, std::string& title) {
    Http h; h.owner = owner; h.abortable = false;
    if (!HttpOpen(h, url) || h.metaint <= 0) return false;
    std::vector<char> skip(std::min(h.metaint, 1 << 16));
    for (int left = h.metaint; left > 0;) {
        int k = std::min(left, (int)skip.size());
        if (!ReadFull(h, skip.data(), (size_t)k)) return false;
        left -= k;
    }
    unsigned char len = 0;
    if (!ReadFull(h, &len, 1)) return false;
    std::string meta(len * 16, '\0');
    if (len && !ReadFull(h, &meta[0], meta.size())) return false;
    bool found = false;
    title = IcyTitle(meta, found);
    return found;
}

bool EndsWithI(std::string s, const std::string& suffix) {
    size_t q = s.find('?'); if (q != std::string::npos) s = s.substr(0, q);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// --- miniaudio decoder callbacks: WinHTTP wrapped in NetReader (rewindable start of stream) --------
ma_result McbRead(ma_decoder* d, void* out, size_t n, size_t* read) {
    *read = ((NetReader*)d->pUserData)->Read(out, n);
    return *read == 0 ? MA_AT_END : MA_SUCCESS;
}
ma_result McbSeek(ma_decoder* d, ma_int64 off, ma_seek_origin origin) {
    return ((NetReader*)d->pUserData)->Seek(off, (int)origin) ? MA_SUCCESS : MA_NOT_IMPLEMENTED;   // seek-to-end is refused: live stream
}
} // namespace

StreamSession::StreamSession(const std::string& url, int reconnectMaxSeconds, bool once) : url_(url), reconnectMax_(reconnectMaxSeconds), once_(once) {
    ma_pcm_rb_init(ma_format_f32, kChannels, kRate * 4, nullptr, nullptr, &rb_);   // 4 s buffer
    done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}
StreamSession::~StreamSession() {
    ma_pcm_rb_uninit(&rb_);
    if (thread_) CloseHandle(thread_);
    if (done_) CloseHandle(done_);
}

void StreamSession::Start() {
    HMODULE self = nullptr;   // add a reference to the DLL for the lifetime of the thread
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&StreamSession::ThreadProc, &self);
    auto* holder = new std::shared_ptr<StreamSession>(shared_from_this());
    thread_ = CreateThread(nullptr, 0, ThreadProc, holder, 0, nullptr);
    if (!thread_) { delete holder; FreeLibrary(self); SetEvent(done_); }
}

DWORD WINAPI StreamSession::ThreadProc(LPVOID p) {
    {
        std::shared_ptr<StreamSession> s = *(std::shared_ptr<StreamSession>*)p;
        delete (std::shared_ptr<StreamSession>*)p;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        s->Run();
        CoUninitialize();
        SetEvent(s->done_);
    }
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&StreamSession::ThreadProc, &self);
    FreeLibraryAndExitThread(self, 0);
}

void StreamSession::Stop() {
    stop_ = true;
    HANDLE r = req_.exchange(nullptr);
    if (r) WinHttpCloseHandle(r);   // aborts a blocking WinHttpReadData
}
bool StreamSession::WaitFinished(DWORD ms) { return WaitForSingleObject(done_, ms) == WAIT_OBJECT_0; }

bool StreamSession::Write(const float* frames, ma_uint32 count) {
    while (count > 0 && !stop_) {
        ma_uint32 n = count; void* buf;
        if (ma_pcm_rb_acquire_write(&rb_, &n, &buf) != MA_SUCCESS) return false;
        if (n == 0) { Sleep(20); continue; }
        memcpy(buf, frames, n * kChannels * sizeof(float));
        ma_pcm_rb_commit_write(&rb_, n);
        written_ += n;
        frames += n * kChannels; count -= n;
        if (!ready_ && ma_pcm_rb_available_read(&rb_) >= kRate) { ready_ = true; failures_ = 0; }   // 1 s pre-buffer
    }
    return !stop_;
}

ma_uint32 StreamSession::Read(float* out, ma_uint32 frames) {
    if (!ready_) { if (decoded_ && ma_pcm_rb_available_read(&rb_) == 0) ended_ = true; return 0; }
    ma_uint32 total = 0;
    while (total < frames) {
        ma_uint32 n = frames - total; void* buf;
        if (ma_pcm_rb_acquire_read(&rb_, &n, &buf) != MA_SUCCESS || n == 0) break;
        memcpy(out + total * kChannels, buf, n * kChannels * sizeof(float));
        ma_pcm_rb_commit_read(&rb_, n);
        total += n;
    }
    played_ += total;
    if (total < frames) { if (decoded_) ended_ = true; else ready_ = false; }   // end of file / underrun -> re-buffer
    return total;
}

void StreamSession::Run() {
    if (once_) {   // USB/CD track: decode once, then let the mixer drain the buffer
        bool ok = PlayOnce(url_, 0);
        if (!ok && !stop_) { failures_ = 1; Log::Write("Cannot play file: %s", url_.c_str()); }
        decoded_ = true;
        if (ma_pcm_rb_available_read(&rb_) > 0) ready_ = true;   // short files: release what is buffered
        return;
    }
    int backoff = 2;
    while (!stop_) {
        bool played = PlayOnce(url_, 0);
        if (stop_) break;
        if (played) backoff = 2; else ++failures_;
        Log::Write("Stream ended/failed, reconnecting in %d s: %s", backoff, url_.c_str());
        for (int i = 0; i < backoff * 10 && !stop_; ++i) Sleep(100);
        backoff = std::min(backoff * 2, std::max(2, reconnectMax_));
    }
}

bool StreamSession::PlayOnce(const std::string& url, int depth) {
    if (url.empty()) return false;
    std::string lower = url; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.rfind("http://", 0) != 0 && lower.rfind("https://", 0) != 0) return PlayLocalFile(url);
    if (lower.find(".m3u8") != std::string::npos) return PlayMediaFoundation(url);   // HLS -> Media Foundation

    Http h; h.owner = this;
    if (!HttpOpen(h, url)) return false;
    const std::string& ct = h.contentType;
    Log::Write("Connected: %s (Content-Type: %s)", url.c_str(), ct.c_str());
    bool isPlaylist = ct.find("mpegurl") != std::string::npos || ct.find("scpls") != std::string::npos ||
                      EndsWithI(url, ".m3u") || EndsWithI(url, ".pls");
    if (isPlaylist && depth < 3) {
        std::string body; char buf[4096];
        while (body.size() < 65536) { DWORD n = h.Read(buf, sizeof(buf)); if (!n) break; body.append(buf, n); }
        h.Close();
        if (body.find("#EXT-X-") != std::string::npos) return PlayMediaFoundation(url);   // it is HLS after all
        size_t p = body.find("http");
        if (p == std::string::npos) { Log::Write("Playlist without URL: %s", url.c_str()); return false; }
        size_t e = body.find_first_of("\r\n \t", p);
        return PlayOnce(body.substr(p, e == std::string::npos ? std::string::npos : e - p), depth + 1);
    }
    if (ct.find("mpeg") != std::string::npos || ct.find("mp3") != std::string::npos || (ct.empty() && EndsWithI(url, ".mp3")))
        return PlayMp3(&h);
    h.Close();
    return PlayMediaFoundation(url);   // AAC / AAC+ / anything else Windows can decode
}

bool StreamSession::PlayMp3(void* httpPtr) {
    Http* h = (Http*)httpPtr;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, kRate);
    cfg.encodingFormat = ma_encoding_format_mp3;
    ma_decoder dec;
    // ICY: every `metaint` audio bytes comes a metadata block (length byte * 16) with the song title. It is taken out of
    // the audio and timed by its byte position, so the title changes exactly when the song does (not when it arrives).
    const uint64_t base = written_.load();   // frames before this connection
    uint64_t audioBytes = 0;
    int untilMeta = h->metaint;
    bool firstTitle = true;
    if (h->metaint > 0) Log::Write("Song title: in the stream every %d bytes%s", h->metaint, h->kbps ? "" : " (bit rate measured)");
    NetReader reader([&, h](void* buf, size_t n) -> size_t {
        for (;;) {
            if (h->metaint > 0 && untilMeta == 0) {
                unsigned char len = 0;
                if (!ReadFull(*h, &len, 1)) return 0;
                std::string meta(len * 16, '\0');
                if (len && !ReadFull(*h, &meta[0], meta.size())) return 0;
                untilMeta = h->metaint;
                bool found = false;
                std::string t = IcyTitle(meta, found);
                if (found) {
                    // frames per audio byte: from the bit rate, else measured (decoded frames / bytes so far)
                    double fpb = h->kbps > 0 ? kRate / (h->kbps * 125.0)
                               : (audioBytes > 200000 ? (double)(written_.load() - base) / (double)audioBytes : 0.0);
                    uint64_t at = firstTitle ? base : fpb > 0 ? base + (uint64_t)(audioBytes * fpb) : written_.load();
                    firstTitle = false;
                    AddTitle(t, at);
                }
                continue;
            }
            size_t want = std::min<size_t>(n, 1 << 20);
            if (h->metaint > 0) want = std::min<size_t>(want, (size_t)untilMeta);
            DWORD got = h->Read(buf, (DWORD)want);
            if (!got) return 0;
            if (h->metaint > 0) untilMeta -= (int)got;
            audioBytes += got;
            return got;
        }
    });
    ma_result r0 = ma_decoder_init(McbRead, McbSeek, &reader, &cfg, &dec);
    if (r0 != MA_SUCCESS) { Log::Write("MP3 decoder init failed (%s)", ma_result_description(r0)); return false; }
    Log::Write("MP3 decoder ready");
    float buf[2048 * kChannels]; bool any = false;
    while (!stop_) {
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, buf, 2048, &got);
        if (got) { any = true; if (!Write(buf, (ma_uint32)got)) break; }
        if (r != MA_SUCCESS || got == 0) break;
    }
    ma_decoder_uninit(&dec);
    return any;
}

bool StreamSession::PlayLocalFile(const std::string& path) {
    std::wstring w = Utf8ToWide(path);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, kRate);
    ma_decoder dec;
    if (ma_decoder_init_file_w(w.c_str(), &cfg, &dec) != MA_SUCCESS) return PlayMediaFoundation(path);
    ma_uint64 len = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &len) == MA_SUCCESS) length_ = len;   // in output frames (48 kHz)
    float buf[2048 * kChannels]; bool any = false;
    while (!stop_) {   // radio: local files loop forever; USB/CD: once
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&dec, buf, 2048, &got);
        if (got == 0) { if (!any || once_) break; ma_decoder_seek_to_pcm_frame(&dec, 0); continue; }
        any = true;
        if (!Write(buf, (ma_uint32)got)) break;
    }
    ma_decoder_uninit(&dec);
    return any;
}

bool StreamSession::PlayMediaFoundation(const std::string& url) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    bool any = false;
    // AAC and other streams Media Foundation opens itself: ask the server for the title every 20 s on a short second
    // connection (the title then follows the audio that was decoded at that moment)
    std::atomic<bool> pollStop{false};
    std::thread poller;
    std::string lower = url; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (MetadataEnabled() && !once_ && (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0) && lower.find(".m3u8") == std::string::npos)
        poller = std::thread([this, url, &pollStop] {
            int fails = 0;
            while (!stop_ && !pollStop && MetadataEnabled() && fails < 3) {
                std::string t;
                if (FetchIcyTitle(this, url, t)) { fails = 0; AddTitle(t, written_.load()); } else ++fails;
                for (int i = 0; i < 200 && !stop_ && !pollStop; ++i) Sleep(100);
            }
        });
    IMFSourceReader* reader = nullptr; IMFMediaType* type = nullptr; IMFMediaType* actual = nullptr;
    ma_data_converter conv; bool convInit = false;
    do {
        HRESULT hr = MFCreateSourceReaderFromURL(Utf8ToWide(url).c_str(), nullptr, &reader);
        if (FAILED(hr)) { Log::Write("Media Foundation cannot open %s (hr=0x%08lx)", url.c_str(), (unsigned long)hr); break; }
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type))) { Log::Write("MF: no float output for %s", url.c_str()); break; }
        if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual))) break;
        UINT32 ch = 0, rate = 0;
        actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch); actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        if (!ch || !rate) break;
        Log::Write("Media Foundation playing %s (%u Hz, %u ch)", url.c_str(), rate, ch);
        {   // duration (100 ns units) for the USB/CD progress bar; live streams have none
            PROPVARIANT var; PropVariantInit(&var);
            if (SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
                length_ = var.uhVal.QuadPart * kRate / 10000000ULL;
            PropVariantClear(&var);
        }
        ma_data_converter_config cc = ma_data_converter_config_init(ma_format_f32, ma_format_f32, ch, kChannels, rate, kRate);
        if (ma_data_converter_init(&cc, nullptr, &conv) != MA_SUCCESS) break;
        convInit = true;
        std::vector<float> outBuf;
        while (!stop_) {
            DWORD flags = 0; IMFSample* sample = nullptr;
            hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))) { if (sample) sample->Release(); break; }
            if (!sample) continue;
            IMFMediaBuffer* mb = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&mb))) {
                BYTE* data = nullptr; DWORD len = 0;
                if (SUCCEEDED(mb->Lock(&data, nullptr, &len))) {
                    ma_uint64 inFrames = len / (sizeof(float) * ch), outFrames = 0;
                    ma_data_converter_get_expected_output_frame_count(&conv, inFrames, &outFrames);
                    outBuf.resize((size_t)(outFrames + 16) * kChannels);
                    ma_uint64 outCap = outFrames + 16;
                    ma_data_converter_process_pcm_frames(&conv, data, &inFrames, outBuf.data(), &outCap);
                    mb->Unlock();
                    if (outCap) { any = true; Write(outBuf.data(), (ma_uint32)outCap); }
                }
                mb->Release();
            }
            sample->Release();
        }
    } while (false);
    pollStop = true;
    if (poller.joinable()) poller.join();
    if (convInit) ma_data_converter_uninit(&conv, nullptr);
    if (actual) actual->Release();
    if (type) type->Release();
    if (reader) reader->Release();
    MFShutdown();
    return any;
}

// --- live song title --------------------------------------------------------------------------------------------
std::atomic<bool> StreamSession::metadata_{true};
void StreamSession::AddTitle(const std::string& title, uint64_t atFrame) {
    std::lock_guard<std::mutex> l(titleMtx_);
    const std::string& last = titles_.empty() ? title_ : titles_.back().second;
    if (title == last && (!titles_.empty() || haveTitle_)) return;
    titles_.push_back({atFrame, title});
    if (titles_.size() > 16) titles_.erase(titles_.begin());
}
bool StreamSession::Title(std::string& out) {
    std::lock_guard<std::mutex> l(titleMtx_);
    uint64_t played = played_.load();
    while (!titles_.empty() && titles_.front().first <= played) {
        title_ = titles_.front().second; haveTitle_ = true;
        titles_.erase(titles_.begin());
    }
    out = title_;
    return haveTitle_;
}
