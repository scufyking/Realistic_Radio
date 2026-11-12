// Realistic Radio Web-App
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Add-on for Realistic Radio (1.7.0 or newer): a web page as an app on the virtual phone - by default the Spotify web
// player, with login, playlists and search. Nothing extra to install and no window: the page runs in Microsoft Edge
// WebView2 (part of Windows 10 / 11) in a window that is never shown; its picture goes to the phone (which turns into
// a tablet for it), mouse and keyboard on the phone come back here and go to the page.
//
// Picture: the DevTools screencast of the page (JPEG frames whenever the page changes, only while the app is open).
// Input: DevTools Input.dispatchMouseEvent / dispatchKeyEvent / insertText - independent of window focus, so the game
// keeps its focus. The login is kept in realistic-radio\webapp\profile. The music keeps playing when the phone is
// closed; the sound comes from WebView2 directly (Windows volume mixer: "Microsoft Edge WebView2").
//
// It is an SCS telemetry plugin only so that the game loads it; it does not use the telemetry. Realistic Radio is found
// through its exported functions (rr_app_*), whatever its DLL file is called.
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <psapi.h>
#include <scssdk_telemetry.h>
#include "webview2/WebView2.h"
#include "webview2/WebView2Loader.h"
#include "json.hpp"
#include "../../sdk/addon/rr_addon.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#define WA_VERSION "1.0.0"
using json = nlohmann::json;

namespace {
// ---- interface ids (WebView2.h declares them, the MSVC import library would define them)
const IID kIID_EnvCompleted = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
const IID kIID_CtrlCompleted = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
const IID kIID_CdpCompleted = {0x5c4889f0, 0x5ef6, 0x4c5a, {0x95, 0x2c, 0xd8, 0xf1, 0xb9, 0x2d, 0x05, 0x74}};
const IID kIID_CdpEvent = {0xe2fda4be, 0x5456, 0x406c, {0xa2, 0x61, 0x3d, 0x45, 0x21, 0x38, 0x36, 0x2c}};
const IID kIID_ScriptCompleted = {0x49511172, 0xcc67, 0x4bca, {0x99, 0x23, 0x13, 0x71, 0x12, 0xf4, 0xc4, 0xcc}};
const IID kIID_NewWindow = {0xd4c185fe, 0xc81c, 0x4989, {0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51}};
const IID kIID_EnvOptions = {0x2fde08a8, 0x1e9a, 0x4766, {0x8c, 0x05, 0x95, 0xa9, 0xce, 0xb9, 0xd1, 0xc5}};

// a COM callback object around a lambda
template <class I, class... A>
struct Handler : I {
    std::atomic<ULONG> ref{1};
    IID iid;
    std::function<HRESULT(A...)> fn;
    Handler(const IID& i, std::function<HRESULT(A...)> f) : iid(i), fn(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** p) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid)) { *p = static_cast<I*>(this); AddRef(); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn(a...); }
};
// owns the new handler until the end of the call it is passed to (WebView2 keeps its own reference)
template <class I> struct Tmp {
    I* p;
    explicit Tmp(I* x) : p(x) {}
    Tmp(Tmp&& o) noexcept : p(o.p) { o.p = nullptr; }
    ~Tmp() { if (p) p->Release(); }
    operator I*() const { return p; }
};
template <class T> struct Id { using type = T; };   // keeps the lambda from taking part in the deduction
template <class I, class... A> Tmp<I> MakeHandler(const IID& iid, typename Id<std::function<HRESULT(A...)>>::type f) { return Tmp<I>(new Handler<I, A...>(iid, std::move(f))); }

LPWSTR CoStr(const std::wstring& s) {
    auto* p = (LPWSTR)CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t));
    if (p) memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    return p;
}
// environment options: extra browser arguments (keep rendering although the window is never visible)
struct Options : ICoreWebView2EnvironmentOptions {
    std::atomic<ULONG> ref{1};
    std::wstring args, lang, version = L"86.0.616.0";
    BOOL sso = FALSE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** p) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_EnvOptions)) { *p = static_cast<ICoreWebView2EnvironmentOptions*>(this); AddRef(); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* v) override { *v = CoStr(args); return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR v) override { args = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* v) override { *v = CoStr(lang); return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR v) override { lang = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* v) override { *v = CoStr(version); return S_OK; }
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR v) override { version = v ? v : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* v) override { *v = sso; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL v) override { sso = v; return S_OK; }
};

// ---- Realistic Radio's interface for add-ons (sdk/addon/rr_addon.h)
rr_addon_api rr;
const char* kId = "webapp";

std::atomic<bool> g_quit{false};
HANDLE g_thread = nullptr;
DWORD g_threadId = 0;

void Log(const std::string& s) { if (rr.log) rr.log(("Web-App: " + s).c_str()); }
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// folder of this DLL (the game's plugins folder)
std::wstring PluginDir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&PluginDir, &self);
    wchar_t p[MAX_PATH]; GetModuleFileNameW(self, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

// ---- settings: realistic-radio\webapp.json
struct Settings {
    std::wstring name = L"Spotify";
    std::wstring url = L"https://open.spotify.com/";
    double zoom = 1.0;          // page zoom: smaller = more fits on the tablet
};
Settings LoadSettings(const std::wstring& file) {
    Settings s;
    std::ifstream f{std::filesystem::path(file)};
    if (!f) {   // first start: write the file with the defaults
        std::ofstream o{std::filesystem::path(file)};
        o << "{\n"
             "  \"// DE\": \"Name der App im Handy, Startseite und Zoom (1.0 = normal, 0.8 = kleiner). Nach Aenderungen das Spiel neu starten.\",\n"
             "  \"// EN\": \"Name of the app on the phone, start page and zoom (1.0 = normal, 0.8 = smaller). Restart the game after changes.\",\n"
             "  \"name\": \"Spotify\",\n"
             "  \"url\": \"https://open.spotify.com/\",\n"
             "  \"zoom\": 1.0\n"
             "}\n";
        return s;
    }
    std::stringstream ss; ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false, true);
    if (j.is_object()) {
        if (j.contains("name") && j["name"].is_string() && !j["name"].get<std::string>().empty()) s.name = Wide(j["name"].get<std::string>());
        if (j.contains("url") && j["url"].is_string() && j["url"].get<std::string>().rfind("http", 0) == 0) s.url = Wide(j["url"].get<std::string>());
        if (j.contains("zoom") && j["zoom"].is_number()) s.zoom = std::min(2.0, std::max(0.4, j["zoom"].get<double>()));
    }
    return s;
}

// ---- pictures
std::vector<uint8_t> Base64(const std::string& in) {
    static int T[256]; static bool init = false;
    if (!init) { for (int& t : T) t = -1; const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; ++i) T[(unsigned char)a[i]] = i; init = true; }
    std::vector<uint8_t> out; out.reserve(in.size() * 3 / 4);
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] < 0) continue;
        val = (val << 6) | T[c]; bits += 6;
        if (bits >= 0) { out.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}
bool DecodeImage(IWICImagingFactory* wic, const std::vector<uint8_t>& data, std::vector<uint32_t>& px, int& w, int& h) {
    if (!wic || data.empty()) return false;
    IWICStream* st = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    bool ok = false;
    if (SUCCEEDED(wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size())) &&
        SUCCEEDED(wic->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT W = 0, H = 0; cv->GetSize(&W, &H);
        if (W && H && W <= 4096 && H <= 4096) {
            px.resize((size_t)W * H);
            ok = SUCCEEDED(cv->CopyPixels(nullptr, W * 4, (UINT)(px.size() * 4), (BYTE*)px.data()));
            w = (int)W; h = (int)H;
        }
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); if (st) st->Release();
    return ok;
}
// a text on dark grey (loading / error) as the app's picture
void SendMessageFrame(int w, int h, const std::wstring& text) {
    w = std::max(64, w); h = std::max(64, h);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT r{0, 0, w, h};
    HBRUSH b = CreateSolidBrush(RGB(24, 24, 28)); FillRect(dc, &r, b); DeleteObject(b);
    HFONT f = CreateFontW(-std::max(14, h / 24), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220, 222, 228));
    RECT tr{w / 10, h / 3, w - w / 10, h};
    DrawTextW(dc, text.c_str(), -1, &tr, DT_CENTER | DT_WORDBREAK);
    SelectObject(dc, of); DeleteObject(f);
    GdiFlush();
    std::vector<uint32_t> px((uint32_t*)bits, (uint32_t*)bits + (size_t)w * h);
    for (auto& p : px) p |= 0xFF000000u;
    rr.app_frame(kId, px.data(), w, h);
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
}

// ---- the web view
struct App {
    Settings set;
    std::wstring dir;            // realistic-radio\ folder
    HWND host = nullptr;
    ICoreWebView2Environment* env = nullptr;
    ICoreWebView2Controller* ctrl = nullptr;
    ICoreWebView2* web = nullptr;
    IWICImagingFactory* wic = nullptr;
    bool creating = false, failed = false, open = false, casting = false;
    int areaW = 0, areaH = 0;            // picture size the tablet shows
    int frameW = 0, frameH = 0;          // last screencast frame
    double cssW = 0, cssH = 0;           // page size in CSS pixels (screencast metadata)
    int buttons = 0;                     // pressed mouse buttons (bit 0 left, 1 right, 2 middle)
    DWORD lastReg = 0, castAt = 0, lastFrameAt = 0;
} g;

void Cdp(const wchar_t* method, const json& params) {
    if (!g.web) return;
    std::wstring p = Wide(params.dump());
    g.web->CallDevToolsProtocolMethod(method, p.c_str(),
        MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>(kIID_CdpCompleted, [](HRESULT, LPCWSTR) { return S_OK; }));
}

void StartCast() {
    if (!g.web || g.casting || g.areaW <= 0) return;
    g.casting = true; g.castAt = GetTickCount();
    Cdp(L"Page.startScreencast", {{"format", "jpeg"}, {"quality", 85}, {"maxWidth", g.areaW}, {"maxHeight", g.areaH}, {"everyNthFrame", 1}});
}
void StopCast() {
    if (!g.web || !g.casting) return;
    g.casting = false;
    Cdp(L"Page.stopScreencast", json::object());
}

void Resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    bool changed = w != g.areaW || h != g.areaH;
    g.areaW = w; g.areaH = h;
    if (!g.ctrl || !changed) return;
    RECT r{0, 0, w, h};
    g.ctrl->put_Bounds(r);
    SetWindowPos(g.host, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (g.casting) { StopCast(); StartCast(); }   // new size for the frames
}

void OnFrame(const json& p) {
    if (!p.contains("data") || !p.contains("sessionId")) return;
    Cdp(L"Page.screencastFrameAck", {{"sessionId", p["sessionId"]}});   // without the ack no further frames come
    if (p.contains("metadata")) {
        const json& m = p["metadata"];
        if (m.contains("deviceWidth")) g.cssW = m["deviceWidth"].get<double>();
        if (m.contains("deviceHeight")) g.cssH = m["deviceHeight"].get<double>();
    }
    std::vector<uint32_t> px; int w = 0, h = 0;
    if (DecodeImage(g.wic, Base64(p["data"].get<std::string>()), px, w, h)) {
        g.frameW = w; g.frameH = h; g.lastFrameAt = GetTickCount();
        rr.app_frame(kId, px.data(), w, h);
    }
}

void Fail(const std::wstring& text, const std::string& log) {
    g.failed = true; g.creating = false;
    Log(log);
    SendMessageFrame(g.areaW, g.areaH, text);
}

void CreateView() {
    if (g.creating || g.ctrl || g.failed) return;
    g.creating = true;
    SendMessageFrame(g.areaW, g.areaH, g.set.name + L" wird geladen … / loading …");
    auto* opt = new Options();
    // the window is never shown: keep the page rendering and playing as if it were in front
    opt->args = L"--disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows --disable-renderer-backgrounding "
                L"--disable-background-timer-throttling --autoplay-policy=no-user-gesture-required";
    std::wstring profile = g.dir + L"webapp\\profile";
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(profile), ec);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), opt,
        MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(kIID_EnvCompleted,
        [](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(r) || !env) {
                Fail(L"Microsoft Edge WebView2 fehlt oder startet nicht.\nWebView2 Runtime installieren: go.microsoft.com/fwlink/p/?LinkId=2124703",
                     "WebView2 environment could not be created (hr 0x" + [&] { char b[16]; snprintf(b, 16, "%08lx", (unsigned long)r); return std::string(b); }() + ")");
                return S_OK;
            }
            g.env = env; env->AddRef();
            env->CreateCoreWebView2Controller(g.host, MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(kIID_CtrlCompleted,
                [](HRESULT r2, ICoreWebView2Controller* c) -> HRESULT {
                    g.creating = false;
                    if (FAILED(r2) || !c) { Fail(L"WebView2 konnte nicht gestartet werden.", "WebView2 controller could not be created"); return S_OK; }
                    g.ctrl = c; c->AddRef();
                    c->get_CoreWebView2(&g.web);
                    RECT r{0, 0, g.areaW, g.areaH};
                    c->put_Bounds(r);
                    c->put_IsVisible(TRUE);
                    c->put_ZoomFactor(g.set.zoom);
                    ICoreWebView2Settings* s = nullptr;
                    if (SUCCEEDED(g.web->get_Settings(&s)) && s) {
                        s->put_AreDefaultContextMenusEnabled(FALSE); s->put_AreDevToolsEnabled(FALSE); s->put_IsStatusBarEnabled(FALSE);
                        s->put_IsZoomControlEnabled(FALSE); s->put_AreDefaultScriptDialogsEnabled(TRUE);
                        s->Release();
                    }
                    // links that want a new window (login with Google / Facebook ...) open in the same view
                    EventRegistrationToken tok{};
                    g.web->add_NewWindowRequested(MakeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(kIID_NewWindow,
                        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                            LPWSTR uri = nullptr;
                            if (SUCCEEDED(a->get_Uri(&uri)) && uri) { g.web->Navigate(uri); CoTaskMemFree(uri); }
                            a->put_Handled(TRUE);
                            return S_OK;
                        }), &tok);
                    // screencast frames
                    ICoreWebView2DevToolsProtocolEventReceiver* rcv = nullptr;
                    if (SUCCEEDED(g.web->GetDevToolsProtocolEventReceiver(L"Page.screencastFrame", &rcv)) && rcv) {
                        rcv->add_DevToolsProtocolEventReceived(MakeHandler<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(kIID_CdpEvent,
                            [](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
                                LPWSTR js = nullptr;
                                if (SUCCEEDED(a->get_ParameterObjectAsJson(&js)) && js) {
                                    json p = json::parse(Utf8(js), nullptr, false);
                                    CoTaskMemFree(js);
                                    if (p.is_object()) OnFrame(p);
                                }
                                return S_OK;
                            }), &tok);
                        rcv->Release();
                    }
                    Cdp(L"Page.enable", json::object());
                    g.web->Navigate(g.set.url.c_str());
                    Log("WebView2 started, loading " + Utf8(g.set.url));
                    if (g.open) StartCast();
                    return S_OK;
                }));
            return S_OK;
        }));
    opt->Release();
    if (FAILED(hr)) {
        char b[16]; snprintf(b, 16, "%08lx", (unsigned long)hr);
        Fail(L"Microsoft Edge WebView2 ist nicht installiert.\nWebView2 Runtime installieren: go.microsoft.com/fwlink/p/?LinkId=2124703",
             std::string("WebView2 runtime not found (hr 0x") + b + ")");
    }
}

// ---- input from the phone
double CssX(int x) { return g.frameW > 0 && g.cssW > 0 ? x * g.cssW / g.frameW : x / g.set.zoom; }
double CssY(int y) { return g.frameH > 0 && g.cssH > 0 ? y * g.cssH / g.frameH : y / g.set.zoom; }
const char* ButtonName(int b) { return b == 1 ? "right" : b == 2 ? "middle" : "left"; }

struct KeyInfo { const char* key; const char* code; const char* text; };
bool SpecialKey(int vk, KeyInfo& k) {
    switch (vk) {
    case VK_BACK: k = {"Backspace", "Backspace", nullptr}; return true;
    case VK_TAB: k = {"Tab", "Tab", nullptr}; return true;
    case VK_RETURN: k = {"Enter", "Enter", "\r"}; return true;
    case VK_DELETE: k = {"Delete", "Delete", nullptr}; return true;
    case VK_LEFT: k = {"ArrowLeft", "ArrowLeft", nullptr}; return true;
    case VK_RIGHT: k = {"ArrowRight", "ArrowRight", nullptr}; return true;
    case VK_UP: k = {"ArrowUp", "ArrowUp", nullptr}; return true;
    case VK_DOWN: k = {"ArrowDown", "ArrowDown", nullptr}; return true;
    case VK_HOME: k = {"Home", "Home", nullptr}; return true;
    case VK_END: k = {"End", "End", nullptr}; return true;
    case VK_PRIOR: k = {"PageUp", "PageUp", nullptr}; return true;
    case VK_NEXT: k = {"PageDown", "PageDown", nullptr}; return true;
    default: return false;
    }
}
std::wstring ClipboardText() {
    std::wstring t;
    if (!OpenClipboard(nullptr)) return t;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) { if (auto* p = (const wchar_t*)GlobalLock(h)) { t = p; GlobalUnlock(h); } }
    CloseClipboard();
    return t;
}

void HandleInput() {
    static std::vector<char> buf(64 * 1024);
    int n = rr.app_events(kId, buf.data(), (int)buf.size());
    if (n < 0) { buf.resize((size_t)-n + 1024); n = rr.app_events(kId, buf.data(), (int)buf.size()); }
    if (n <= 0) return;
    json evs = json::parse(std::string(buf.data(), n), nullptr, false);
    if (!evs.is_array()) return;
    for (const json& e : evs) {
        const std::string type = e.value("type", std::string());
        const int x = e.value("x", 0), y = e.value("y", 0);
        if (type == "open") { g.open = true; if (g.ctrl) StartCast(); else CreateView(); }   // the app was opened on the phone
        else if (type == "close") { g.open = false; StopCast(); g.buttons = 0; }
        else if (type == "move") { if (g.web) Cdp(L"Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", CssX(x)}, {"y", CssY(y)}, {"buttons", g.buttons}, {"button", (g.buttons & 1) ? "left" : "none"}}); }
        else if (type == "down" || type == "up") {
            int bt = std::min(2, std::max(0, e.value("button", 0)));
            if (type == "down") g.buttons |= 1 << bt; else g.buttons &= ~(1 << bt);
            if (g.web) Cdp(L"Input.dispatchMouseEvent", {{"type", type == "down" ? "mousePressed" : "mouseReleased"}, {"x", CssX(x)}, {"y", CssY(y)},
                                                       {"button", ButtonName(bt)}, {"buttons", g.buttons}, {"clickCount", 1}});
        }
        else if (type == "wheel") { if (g.web) Cdp(L"Input.dispatchMouseEvent", {{"type", "mouseWheel"}, {"x", CssX(x)}, {"y", CssY(y)}, {"deltaX", 0}, {"deltaY", -e.value("delta", 0)}}); }
        else if (type == "char") { if (g.web) Cdp(L"Input.insertText", {{"text", e.value("text", std::string())}}); }
        else if (type == "key" && g.web) {
            const int vk = e.value("vk", 0);
            const bool ctrl = e.value("ctrl", false), shift = e.value("shift", false), alt = e.value("alt", false);
            const int mods = (ctrl ? 2 : 0) | (shift ? 8 : 0) | (alt ? 1 : 0);   // CDP: alt 1, ctrl 2, meta 4, shift 8
            if (ctrl && vk == 'V') { std::wstring t = ClipboardText(); if (!t.empty()) Cdp(L"Input.insertText", {{"text", Utf8(t)}}); continue; }
            if (ctrl && (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'Z')) {
                const char* cmd = vk == 'A' ? "selectAll" : vk == 'C' ? "copy" : vk == 'X' ? "cut" : "undo";
                char key[2] = {(char)(vk + 32), 0}, code[5] = {'K', 'e', 'y', (char)vk, 0};
                Cdp(L"Input.dispatchKeyEvent", {{"type", "keyDown"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", key}, {"code", code}, {"commands", json::array({cmd})}});
                Cdp(L"Input.dispatchKeyEvent", {{"type", "keyUp"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", key}, {"code", code}});
                continue;
            }
            KeyInfo k;
            if (!SpecialKey(vk, k)) continue;   // letters come as "char"
            json down = {{"type", k.text ? "keyDown" : "rawKeyDown"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", k.key}, {"code", k.code}};
            if (k.text) down["text"] = k.text;
            Cdp(L"Input.dispatchKeyEvent", down);
            Cdp(L"Input.dispatchKeyEvent", {{"type", "keyUp"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", k.key}, {"code", k.code}});
        }
    }
}

// every 16 ms on the web view's thread
void Tick() {
    DWORD now = GetTickCount();
    if (now - g.lastReg >= 1000) {   // keep the app on the phone
        g.lastReg = now;
        rr.app_register(json{{"id", kId}, {"name", Utf8(g.set.name)}, {"mode", "tablet"}, {"color", "#1E9E58"}}.dump().c_str());
    }
    int w = 0, h = 0;
    bool open = rr.app_area(kId, &w, &h) != 0;
    Resize(w, h);
    if (open && !g.ctrl && !g.creating && !g.failed) { g.open = true; CreateView(); }
    HandleInput();
    if (g.casting && g.lastFrameAt + 3000 < now && g.castAt + 3000 < now && g.web) {   // no frame for a while (page idle): ask for one picture
        g.castAt = now;
        StopCast(); StartCast();
    }
}

LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) { Tick(); return 0; }
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

DWORD WINAPI Worker(LPVOID) {
    bool found = false;
    for (int i = 0; i < 600 && !g_quit && !found; ++i) { found = rr_addon_load(&rr) != 0; if (!found) Sleep(500); }
    if (!found) return 0;   // Realistic Radio older than 1.7.0 or missing
    g.dir = PluginDir() + L"realistic-radio\\";
    g.set = LoadSettings(g.dir + L"webapp.json");
    Log("add-on " WA_VERSION " started: app '" + Utf8(g.set.name) + "', " + Utf8(g.set.url));
    // physical pixels for the window and the page (no Windows scaling in between)
    using SetCtx = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto f = (SetCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext")) f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&g.wic);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&HostProc, &self);
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = self; wc.lpszClassName = L"RealisticRadioWebApp";
    RegisterClassW(&wc);
    // never visible: far outside every monitor, no taskbar button, never activated
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g.host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Realistic Radio Web-App", WS_POPUP,
                             vx + vw + 4000, 0, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g.host) { Log("could not create the host window"); CoUninitialize(); return 0; }
    ShowWindow(g.host, SW_SHOWNOACTIVATE);
    SetTimer(g.host, 1, 16, nullptr);
    MSG msg;
    while (!g_quit && GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    KillTimer(g.host, 1);
    if (g.ctrl) { g.ctrl->Close(); g.ctrl->Release(); g.ctrl = nullptr; }
    if (g.web) { g.web->Release(); g.web = nullptr; }
    if (g.env) { g.env->Release(); g.env = nullptr; }
    if (g.wic) { g.wic->Release(); g.wic = nullptr; }
    DestroyWindow(g.host); g.host = nullptr;
    CoUninitialize();
    return 0;
}
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto* p = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if (g_thread) return SCS_RESULT_ok;
    g_quit = false;
    g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, &g_threadId);
    p->common.log(SCS_LOG_TYPE_message, "[realistic-radio-webapp] Realistic Radio Web-App " WA_VERSION " loaded (needs Realistic Radio 1.7.0 or newer)");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    g_quit = true;
    if (g_thread) {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, 5000); CloseHandle(g_thread); g_thread = nullptr;
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
