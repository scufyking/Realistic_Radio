// Realistic Radio Web-App
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Add-on for Realistic Radio (1.7.0 or newer): web pages as apps on the virtual phone - by default the Spotify web
// player (login, playlists, search) and, since 1.1.0, a web browser with an address bar (back, forward, reload, home). Nothing extra to install and no window: the page runs in Microsoft Edge
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

#define WA_VERSION "1.1.0"
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
// 1.1.0: several apps. Old files ({"name", "url", "zoom"}) are one app; the browser is added to them.
struct AppSettings {
    std::string id;                       // id on the phone
    std::wstring name = L"Spotify";
    std::wstring url = L"https://open.spotify.com/";
    double zoom = 1.0;                    // page zoom: smaller = more fits on the tablet
    bool browser = false;                 // address bar, back / forward / reload / home
    std::wstring search = L"https://www.google.com/search?q=";   // browser: what is typed that is no address
    std::string color = "#1E9E58";
};
AppSettings SpotifyDefault() { AppSettings a; a.id = "webapp"; return a; }
AppSettings BrowserDefault() {
    AppSettings a; a.id = "webapp-browser"; a.name = L"Browser"; a.url = L"https://www.google.com/"; a.browser = true; a.color = "#2D6CDF";
    return a;
}
const char* kDefaultFile =
    "{\n"
    "  \"// DE\": \"Apps im Handy: Name, Startseite, Zoom (1.0 = normal, 0.8 = kleiner), browser = mit Adressleiste. Nach Aenderungen das Spiel neu starten.\",\n"
    "  \"// EN\": \"Apps on the phone: name, start page, zoom (1.0 = normal, 0.8 = smaller), browser = with an address bar. Restart the game after changes.\",\n"
    "  \"apps\": [\n"
    "    {\"name\": \"Spotify\", \"url\": \"https://open.spotify.com/\", \"zoom\": 1.0},\n"
    "    {\"name\": \"Browser\", \"url\": \"https://www.google.com/\", \"zoom\": 1.0, \"browser\": true, \"search\": \"https://www.google.com/search?q=\"}\n"
    "  ]\n"
    "}\n";
void ReadApp(const json& j, AppSettings& s) {
    if (j.contains("name") && j["name"].is_string() && !j["name"].get<std::string>().empty()) s.name = Wide(j["name"].get<std::string>());
    if (j.contains("url") && j["url"].is_string() && j["url"].get<std::string>().rfind("http", 0) == 0) s.url = Wide(j["url"].get<std::string>());
    if (j.contains("zoom") && j["zoom"].is_number()) s.zoom = std::min(2.0, std::max(0.4, j["zoom"].get<double>()));
    if (j.contains("browser") && j["browser"].is_boolean()) s.browser = j["browser"].get<bool>();
    if (j.contains("search") && j["search"].is_string() && j["search"].get<std::string>().rfind("http", 0) == 0) s.search = Wide(j["search"].get<std::string>());
    if (j.contains("color") && j["color"].is_string() && j["color"].get<std::string>().size() == 7) s.color = j["color"].get<std::string>();
}
std::vector<AppSettings> LoadSettings(const std::wstring& file) {
    std::vector<AppSettings> apps;
    std::ifstream f{std::filesystem::path(file)};
    if (!f) {   // first start: write the file with the defaults
        std::ofstream o{std::filesystem::path(file)};
        o << kDefaultFile;
        return {SpotifyDefault(), BrowserDefault()};
    }
    std::stringstream ss; ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false, true);
    if (j.is_object() && j.contains("apps") && j["apps"].is_array()) {
        for (const json& a : j["apps"]) {
            if (!a.is_object() || apps.size() >= 6) continue;
            AppSettings s = SpotifyDefault();
            s.color = "#1E9E58";
            ReadApp(a, s);
            if (s.browser && !a.contains("color")) s.color = "#2D6CDF";
            // the first keeps the id of 1.0 (its place on the phone), the first browser gets "webapp-browser"
            bool browserTaken = false;
            for (auto& x : apps) if (x.id == "webapp-browser") browserTaken = true;
            s.id = apps.empty() ? "webapp" : (s.browser && !browserTaken) ? "webapp-browser" : "webapp-" + std::to_string(apps.size() + 1);
            apps.push_back(s);
        }
    } else if (j.is_object()) {   // 1.0 file: one app, plus the browser of 1.1.0
        AppSettings s = SpotifyDefault();
        ReadApp(j, s);
        apps.push_back(s);
        apps.push_back(BrowserDefault());
    }
    if (apps.empty()) apps = {SpotifyDefault(), BrowserDefault()};
    return apps;
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

// a GDI picture to draw into (top-down BGRA)
struct Dib {
    HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; uint32_t* px = nullptr; int w = 0, h = 0;
    Dib(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
        if (bmp) old = SelectObject(dc, bmp);
    }
    ~Dib() { if (bmp) { SelectObject(dc, old); DeleteObject(bmp); } if (dc) DeleteDC(dc); }
    bool ok() const { return bmp && px; }
    void Fill(RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
};

// ---- one app = one web view
struct View {
    AppSettings set;
    HWND host = nullptr;      // parent of the web view: created the first time the app is opened (1.0.1)
    ICoreWebView2Controller* ctrl = nullptr;
    ICoreWebView2* web = nullptr;
    bool creating = false, failed = false, open = false, casting = false;
    int areaW = 0, areaH = 0;            // picture size the tablet shows
    int frameW = 0, frameH = 0;          // last screencast frame
    double cssW = 0, cssH = 0;           // page size in CSS pixels (screencast metadata)
    int buttons = 0;                     // pressed mouse buttons (bit 0 left, 1 right, 2 middle)
    DWORD lastReg = 0, castAt = 0, lastFrameAt = 0;
    std::vector<uint32_t> page;          // last page picture (the browser draws its bar above it)
    // browser (1.1.0)
    std::wstring source, title;          // current address and page title
    bool editing = false, selectAll = false;
    std::wstring edit; size_t caret = 0;
    bool canBack = false, canForward = false, loading = false;
    int pressedBtn = -1;                 // toolbar button under the mouse button
    int Bar() const { return set.browser ? std::max(36, std::min(64, areaH / 14)) : 0; }   // height of the address bar
    int PageH() const { return std::max(1, areaH - Bar()); }
};
struct Shared {
    std::wstring dir;            // realistic-radio\ folder
    HWND timer = nullptr;        // message-only window for the 16 ms tick (never visible)
    HWND game = nullptr;         // the game's own window (focus goes back there)
    ICoreWebView2Environment* env = nullptr;
    bool envCreating = false, envFailed = false;
    IWICImagingFactory* wic = nullptr;
    std::vector<View*> views;
} g;

void Cdp(View& v, const wchar_t* method, const json& params) {
    if (!v.web) return;
    std::wstring p = Wide(params.dump());
    v.web->CallDevToolsProtocolMethod(method, p.c_str(),
        MakeHandler<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>(kIID_CdpCompleted, [](HRESULT, LPCWSTR) { return S_OK; }));
}

// a text on dark grey (loading / error) as the app's picture
void SendMessageFrame(View& v, const std::wstring& text) {
    int w = std::max(64, v.areaW), h = std::max(64, v.areaH);
    Dib d(w, h);
    if (!d.ok()) return;
    d.Fill({0, 0, w, h}, RGB(24, 24, 28));
    HFONT f = CreateFontW(-std::max(14, h / 24), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(d.dc, f);
    SetBkMode(d.dc, TRANSPARENT); SetTextColor(d.dc, RGB(220, 222, 228));
    RECT tr{w / 10, h / 3, w - w / 10, h};
    DrawTextW(d.dc, text.c_str(), -1, &tr, DT_CENTER | DT_WORDBREAK);
    SelectObject(d.dc, of); DeleteObject(f);
    GdiFlush();
    std::vector<uint32_t> px(d.px, d.px + (size_t)w * h);
    for (auto& p : px) p |= 0xFF000000u;
    rr.app_frame(v.set.id.c_str(), px.data(), w, h);
}

// ---- browser bar (1.1.0): ◀ ▶ ⟳ ⌂ [ address ]
enum { BtnBack, BtnForward, BtnReload, BtnHome, BtnCount };
RECT ButtonRect(const View& v, int i) { int b = v.Bar(); return RECT{4 + i * (b + 2), 3, 4 + i * (b + 2) + b, b - 3}; }
RECT AddressRect(const View& v) { int b = v.Bar(); RECT last = ButtonRect(v, BtnCount - 1); return RECT{last.right + 8, 5, v.areaW - 8, b - 5}; }

void DrawBar(View& v, Dib& d) {
    const int b = v.Bar(), w = v.areaW;
    d.Fill({0, 0, w, b}, RGB(32, 34, 40));
    d.Fill({0, b - 1, w, b}, RGB(58, 61, 70));
    SetBkMode(d.dc, TRANSPARENT);
    HFONT sym = CreateFontW(-(b * 52 / 100), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
    HGDIOBJ of = SelectObject(d.dc, sym);
    const wchar_t* glyph[BtnCount] = {L"←", L"→", v.loading ? L"✕" : L"↻", L"⌂"};
    bool enabled[BtnCount] = {v.canBack, v.canForward, true, true};
    for (int i = 0; i < BtnCount; ++i) {
        RECT r = ButtonRect(v, i);
        if (v.pressedBtn == i) d.Fill(r, RGB(70, 74, 86));
        COLORREF col = enabled[i] ? RGB(226, 228, 234) : RGB(100, 104, 114);
        if (i == BtnReload && !v.loading) {   // drawn, not a font sign: the same on every PC
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, rad = (r.bottom - r.top) * 26 / 100;
            HPEN pen = CreatePen(PS_SOLID, std::max(2, b / 22), col); HGDIOBJ op = SelectObject(d.dc, pen);
            HGDIOBJ ob = SelectObject(d.dc, GetStockObject(NULL_BRUSH));
            Arc(d.dc, cx - rad, cy - rad, cx + rad, cy + rad, cx + rad / 3, cy - rad, cx + rad, cy - rad / 3);   // most of a circle (counter-clockwise), open at the top right
            SelectObject(d.dc, ob); SelectObject(d.dc, op); DeleteObject(pen);
            int ax = cx + rad, ay = cy - rad / 3 - 1, s = std::max(4, rad * 6 / 10);
            POINT tri[3] = {{ax - s * 6 / 10, ay + 1}, {ax + s * 6 / 10, ay + 1}, {ax, ay - s}};   // the arrow head points on (up)
            HBRUSH tb = CreateSolidBrush(col); HGDIOBJ obb = SelectObject(d.dc, tb); HGDIOBJ np = SelectObject(d.dc, GetStockObject(NULL_PEN));
            Polygon(d.dc, tri, 3);
            SelectObject(d.dc, np); SelectObject(d.dc, obb); DeleteObject(tb);
            continue;
        }
        SetTextColor(d.dc, col);
        DrawTextW(d.dc, glyph[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(d.dc, of); DeleteObject(sym);
    RECT a = AddressRect(v);
    HRGN rgn = CreateRoundRectRgn(a.left, a.top, a.right + 1, a.bottom + 1, b / 2, b / 2);
    HBRUSH br = CreateSolidBrush(v.editing ? RGB(18, 19, 23) : RGB(48, 51, 60)); FillRgn(d.dc, rgn, br); DeleteObject(br);
    if (v.editing) { HBRUSH fr = CreateSolidBrush(RGB(45, 108, 223)); FrameRgn(d.dc, rgn, fr, 2, 2); DeleteObject(fr); }
    DeleteObject(rgn);
    HFONT tf = CreateFontW(-(b * 40 / 100), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    of = SelectObject(d.dc, tf);
    RECT t{a.left + b / 3, a.top, a.right - b / 3, a.bottom};
    if (v.editing) {
        std::wstring s = v.edit;
        if (v.selectAll && !s.empty()) {   // all selected: blue behind the text
            SIZE sz{}; GetTextExtentPoint32W(d.dc, s.c_str(), (int)s.size(), &sz);
            RECT sel{t.left, a.top + 4, std::min<LONG>(t.right, t.left + sz.cx), a.bottom - 4};
            d.Fill(sel, RGB(45, 108, 223));
        }
        SetTextColor(d.dc, RGB(240, 242, 246));
        // keep the caret in view: cut the text from the left when it is too long
        SIZE full{}; GetTextExtentPoint32W(d.dc, s.c_str(), (int)v.caret, &full);
        size_t from = 0;
        while (from < v.caret && full.cx > (t.right - t.left) - 8) {
            ++from; GetTextExtentPoint32W(d.dc, s.c_str() + from, (int)(v.caret - from), &full);
        }
        std::wstring shown = s.substr(from);
        DrawTextW(d.dc, shown.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!v.selectAll && (GetTickCount() / 530) % 2 == 0) {   // blinking caret
            SIZE cs{}; GetTextExtentPoint32W(d.dc, shown.c_str(), (int)(v.caret - from), &cs);
            int cx = t.left + cs.cx;
            d.Fill({cx, a.top + 6, cx + 2, a.bottom - 6}, RGB(240, 242, 246));
        }
    } else {
        std::wstring s = v.source;
        for (const wchar_t* p : {L"https://", L"http://"}) if (s.rfind(p, 0) == 0) { s = s.substr(wcslen(p)); break; }
        if (s.rfind(L"www.", 0) == 0) s = s.substr(4);
        if (!s.empty() && s.back() == L'/') s.pop_back();
        SetTextColor(d.dc, RGB(200, 203, 212));
        DrawTextW(d.dc, s.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(d.dc, of); DeleteObject(tf);
}

// sends the app's picture: the page, and for the browser its bar above it
void Present(View& v) {
    if (v.areaW <= 0 || v.areaH <= 0) return;
    if (!v.set.browser) {
        if (!v.page.empty()) rr.app_frame(v.set.id.c_str(), v.page.data(), v.frameW, v.frameH);
        return;
    }
    const int w = v.areaW, h = v.areaH, b = v.Bar();
    Dib d(w, h);
    if (!d.ok()) return;
    d.Fill({0, 0, w, h}, RGB(255, 255, 255));
    DrawBar(v, d);
    GdiFlush();
    if (!v.page.empty())
        for (int y = 0; y < v.frameH && b + y < h; ++y)
            memcpy(d.px + (size_t)(b + y) * w, v.page.data() + (size_t)y * v.frameW, sizeof(uint32_t) * std::min(w, v.frameW));
    std::vector<uint32_t> px(d.px, d.px + (size_t)w * h);
    for (int y = 0; y < b; ++y) for (int x = 0; x < w; ++x) px[(size_t)y * w + x] |= 0xFF000000u;   // GDI leaves alpha at 0
    rr.app_frame(v.set.id.c_str(), px.data(), w, h);
}

void StartCast(View& v) {
    if (!v.web || v.casting || v.areaW <= 0) return;
    v.casting = true; v.castAt = GetTickCount();
    Cdp(v, L"Page.startScreencast", {{"format", "jpeg"}, {"quality", 85}, {"maxWidth", v.areaW}, {"maxHeight", v.PageH()}, {"everyNthFrame", 1}});
}
void StopCast(View& v) {
    if (!v.web || !v.casting) return;
    v.casting = false;
    Cdp(v, L"Page.stopScreencast", json::object());
}

void Resize(View& v, int w, int h) {
    if (w <= 0 || h <= 0) return;
    bool changed = w != v.areaW || h != v.areaH;
    v.areaW = w; v.areaH = h;
    if (!v.ctrl || !changed) return;
    RECT r{0, 0, w, v.PageH()};
    v.ctrl->put_Bounds(r);
    if (v.host) SetWindowPos(v.host, nullptr, 0, 0, w, v.PageH(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (v.casting) { StopCast(v); StartCast(v); }   // new size for the frames
}

void OnFrame(View& v, const json& p) {
    if (!p.contains("data") || !p.contains("sessionId")) return;
    Cdp(v, L"Page.screencastFrameAck", {{"sessionId", p["sessionId"]}});   // without the ack no further frames come
    if (p.contains("metadata")) {
        const json& m = p["metadata"];
        if (m.contains("deviceWidth")) v.cssW = m["deviceWidth"].get<double>();
        if (m.contains("deviceHeight")) v.cssH = m["deviceHeight"].get<double>();
    }
    std::vector<uint32_t> px; int w = 0, h = 0;
    if (DecodeImage(g.wic, Base64(p["data"].get<std::string>()), px, w, h)) {
        v.frameW = w; v.frameH = h; v.lastFrameAt = GetTickCount();
        v.page = std::move(px);
        Present(v);
    }
}

void Fail(View& v, const std::wstring& text, const std::string& log) {
    v.failed = true; v.creating = false;
    Log(Utf8(v.set.name) + ": " + log);
    SendMessageFrame(v, text);
}

// browser: address, title, back / forward after every navigation
void UpdateNav(View& v) {
    if (!v.web) return;
    LPWSTR s = nullptr;
    if (SUCCEEDED(v.web->get_Source(&s)) && s) { v.source = s; CoTaskMemFree(s); }
    BOOL b = FALSE;
    v.web->get_CanGoBack(&b); v.canBack = b != FALSE;
    v.web->get_CanGoForward(&b); v.canForward = b != FALSE;
    if (v.set.browser && v.open) Present(v);
}

bool EnsureHost(View& v);
void CreateController(View& v);
void CreateEnvironment() {
    if (g.env || g.envCreating || g.envFailed) return;
    g.envCreating = true;
    auto* opt = new Options();
    // the window is never shown: keep the page rendering and playing as if it were in front
    opt->args = L"--disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows --disable-renderer-backgrounding "
                L"--disable-background-timer-throttling --autoplay-policy=no-user-gesture-required";
    std::wstring profile = g.dir + L"webapp\\profile";
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(profile), ec);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), opt,
        MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(kIID_EnvCompleted,
        [](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
            g.envCreating = false;
            if (FAILED(r) || !env) {
                g.envFailed = true;
                char b[16]; snprintf(b, 16, "%08lx", (unsigned long)r);
                for (View* v : g.views) if (v->creating)
                    Fail(*v, L"Microsoft Edge WebView2 fehlt oder startet nicht.\nWebView2 Runtime installieren: go.microsoft.com/fwlink/p/?LinkId=2124703",
                         std::string("WebView2 environment could not be created (hr 0x") + b + ")");
                return S_OK;
            }
            g.env = env; env->AddRef();
            for (View* v : g.views) if (v->creating) CreateController(*v);
            return S_OK;
        }));
    opt->Release();
    if (FAILED(hr)) {
        g.envCreating = false; g.envFailed = true;
        char b[16]; snprintf(b, 16, "%08lx", (unsigned long)hr);
        for (View* v : g.views) if (v->creating)
            Fail(*v, L"Microsoft Edge WebView2 ist nicht installiert.\nWebView2 Runtime installieren: go.microsoft.com/fwlink/p/?LinkId=2124703",
                 std::string("WebView2 runtime not found (hr 0x") + b + ")");
    }
}

void CreateView(View& v) {
    if (v.creating || v.ctrl || v.failed) return;
    if (!EnsureHost(v)) { Fail(v, L"Web-App: Fenster konnte nicht angelegt werden / could not create its window", "could not create the host window"); return; }
    v.creating = true;
    SendMessageFrame(v, v.set.name + L" wird geladen … / loading …");
    if (g.env) CreateController(v);
    else CreateEnvironment();
}

void CreateController(View& v) {
    View* pv = &v;
    g.env->CreateCoreWebView2Controller(v.host, MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(kIID_CtrlCompleted,
        [pv](HRESULT r2, ICoreWebView2Controller* c) -> HRESULT {
            View& v = *pv;
            v.creating = false;
            if (FAILED(r2) || !c) { Fail(v, L"WebView2 konnte nicht gestartet werden.", "WebView2 controller could not be created"); return S_OK; }
            v.ctrl = c; c->AddRef();
            c->get_CoreWebView2(&v.web);
            RECT r{0, 0, v.areaW, v.PageH()};
            c->put_Bounds(r);
            c->put_IsVisible(TRUE);
            c->put_ZoomFactor(v.set.zoom);
            ICoreWebView2Settings* s = nullptr;
            if (SUCCEEDED(v.web->get_Settings(&s)) && s) {
                s->put_AreDefaultContextMenusEnabled(FALSE); s->put_AreDevToolsEnabled(FALSE); s->put_IsStatusBarEnabled(FALSE);
                s->put_IsZoomControlEnabled(FALSE); s->put_AreDefaultScriptDialogsEnabled(TRUE);
                s->Release();
            }
            EventRegistrationToken tok{};
            // links that want a new window (login with Google / Facebook, target=_blank ...) open in the same view
            v.web->add_NewWindowRequested(MakeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(kIID_NewWindow,
                [pv](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                    LPWSTR uri = nullptr;
                    if (SUCCEEDED(a->get_Uri(&uri)) && uri) { pv->web->Navigate(uri); CoTaskMemFree(uri); }
                    a->put_Handled(TRUE);
                    return S_OK;
                }), &tok);
            // screencast frames
            ICoreWebView2DevToolsProtocolEventReceiver* rcv = nullptr;
            if (SUCCEEDED(v.web->GetDevToolsProtocolEventReceiver(L"Page.screencastFrame", &rcv)) && rcv) {
                rcv->add_DevToolsProtocolEventReceived(MakeHandler<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(kIID_CdpEvent,
                    [pv](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
                        LPWSTR js = nullptr;
                        if (SUCCEEDED(a->get_ParameterObjectAsJson(&js)) && js) {
                            json p = json::parse(Utf8(js), nullptr, false);
                            CoTaskMemFree(js);
                            if (p.is_object()) OnFrame(*pv, p);
                        }
                        return S_OK;
                    }), &tok);
                rcv->Release();
            }
            if (v.set.browser) {   // address bar: follow every navigation (DevTools events: no extra WebView2 interfaces needed)
                for (const wchar_t* ev : {L"Page.frameStartedLoading", L"Page.frameStoppedLoading", L"Page.frameNavigated", L"Page.navigatedWithinDocument"}) {
                    ICoreWebView2DevToolsProtocolEventReceiver* r = nullptr;
                    std::wstring name = ev;
                    if (SUCCEEDED(v.web->GetDevToolsProtocolEventReceiver(ev, &r)) && r) {
                        r->add_DevToolsProtocolEventReceived(MakeHandler<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(kIID_CdpEvent,
                            [pv, name](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*) -> HRESULT {
                                if (name == L"Page.frameStartedLoading") pv->loading = true;
                                else if (name == L"Page.frameStoppedLoading") pv->loading = false;
                                UpdateNav(*pv);
                                return S_OK;
                            }), &tok);
                        r->Release();
                    }
                }
            }
            Cdp(v, L"Page.enable", json::object());
            v.web->Navigate(v.set.url.c_str());
            Log(Utf8(v.set.name) + ": WebView2 started, loading " + Utf8(v.set.url));
            if (v.open) StartCast(v);
            return S_OK;
        }));
}

// ---- input from the phone
double CssX(const View& v, int x) { return v.frameW > 0 && v.cssW > 0 ? x * v.cssW / v.frameW : x / v.set.zoom; }
double CssY(const View& v, int y) { return v.frameH > 0 && v.cssH > 0 ? y * v.cssH / v.frameH : y / v.set.zoom; }
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
    for (auto& c : t) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return t;
}

// what was typed into the address bar: an address, or else a search
std::wstring ToUrl(const View& v, std::wstring t) {
    while (!t.empty() && t.back() == L' ') t.pop_back();
    while (!t.empty() && t.front() == L' ') t.erase(t.begin());
    if (t.empty()) return v.set.url;
    if (t.rfind(L"http://", 0) == 0 || t.rfind(L"https://", 0) == 0 || t.rfind(L"about:", 0) == 0) return t;
    bool space = t.find(L' ') != std::wstring::npos, dot = t.find(L'.') != std::wstring::npos;
    if (!space && (dot || t.rfind(L"localhost", 0) == 0)) return L"https://" + t;
    std::string q = Utf8(t), enc;   // search
    for (unsigned char c : q) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') enc += (char)c;
        else if (c == ' ') enc += '+';
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); enc += b; }
    }
    return v.set.search + Wide(enc);
}
void StartEditing(View& v) {
    v.editing = true; v.selectAll = true; v.edit = v.source; v.caret = v.edit.size();
}
void Type(View& v, const std::wstring& text) {
    if (v.selectAll) { v.edit.clear(); v.caret = 0; v.selectAll = false; }
    v.edit.insert(v.caret, text); v.caret += text.size();
}
// a key in the address bar; false = not used there
bool EditKey(View& v, int vk, bool ctrl) {
    if (ctrl && vk == 'V') { std::wstring t = ClipboardText(); if (!t.empty()) Type(v, t); return true; }
    if (ctrl && vk == 'A') { v.selectAll = true; v.caret = v.edit.size(); return true; }
    if (ctrl && vk == 'C') {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            size_t bytes = (v.edit.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes)) { memcpy(GlobalLock(hg), v.edit.c_str(), bytes); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); }
            CloseClipboard();
        }
        return true;
    }
    switch (vk) {
    case VK_RETURN: {
        std::wstring url = ToUrl(v, v.edit);
        v.editing = false;
        if (v.web) { v.web->Navigate(url.c_str()); Log("Browser: " + Utf8(url)); }
        return true;
    }
    case VK_BACK:
        if (v.selectAll) { v.edit.clear(); v.caret = 0; v.selectAll = false; }
        else if (v.caret > 0) { v.edit.erase(v.caret - 1, 1); --v.caret; }
        return true;
    case VK_DELETE:
        if (v.selectAll) { v.edit.clear(); v.caret = 0; v.selectAll = false; }
        else if (v.caret < v.edit.size()) v.edit.erase(v.caret, 1);
        return true;
    case VK_LEFT: v.selectAll = false; if (v.caret > 0) --v.caret; return true;
    case VK_RIGHT: v.selectAll = false; if (v.caret < v.edit.size()) ++v.caret; return true;
    case VK_HOME: v.selectAll = false; v.caret = 0; return true;
    case VK_END: v.selectAll = false; v.caret = v.edit.size(); return true;
    case VK_TAB: v.editing = false; return true;
    default: return false;
    }
}
int BarButtonAt(const View& v, int x, int y) {
    for (int i = 0; i < BtnCount; ++i) { RECT r = ButtonRect(v, i); if (x >= r.left && x < r.right && y >= 0 && y < v.Bar()) return i; }
    return -1;
}
void BarButton(View& v, int i) {
    if (!v.web) return;
    v.editing = false;
    if (i == BtnBack && v.canBack) v.web->GoBack();
    else if (i == BtnForward && v.canForward) v.web->GoForward();
    else if (i == BtnReload) { if (v.loading) v.web->Stop(); else v.web->Reload(); }
    else if (i == BtnHome) v.web->Navigate(v.set.url.c_str());
}

void HandleInput(View& v) {
    static std::vector<char> buf(64 * 1024);
    int n = rr.app_events(v.set.id.c_str(), buf.data(), (int)buf.size());
    if (n < 0) { buf.resize((size_t)-n + 1024); n = rr.app_events(v.set.id.c_str(), buf.data(), (int)buf.size()); }
    if (n <= 0) return;
    json evs = json::parse(std::string(buf.data(), n), nullptr, false);
    if (!evs.is_array()) return;
    const int bar = v.Bar();
    bool redraw = false;
    for (const json& e : evs) {
        const std::string type = e.value("type", std::string());
        const int x = e.value("x", 0), y = e.value("y", 0) - bar;   // y on the page
        const bool onBar = bar > 0 && e.value("y", 0) < bar;
        if (type == "open") { v.open = true; if (v.ctrl) { StartCast(v); Present(v); } else CreateView(v); }   // the app was opened on the phone
        else if (type == "close") { v.open = false; StopCast(v); v.buttons = 0; v.editing = false; }
        else if (type == "move") { if (v.web && !onBar) Cdp(v, L"Input.dispatchMouseEvent", {{"type", "mouseMoved"}, {"x", CssX(v, x)}, {"y", CssY(v, y)}, {"buttons", v.buttons}, {"button", (v.buttons & 1) ? "left" : "none"}}); }
        else if (type == "down" || type == "up") {
            int bt = std::min(2, std::max(0, e.value("button", 0)));
            if (bar > 0 && (onBar || v.pressedBtn >= 0) && bt == 0 && !(v.buttons & 1)) {   // the browser bar
                int hit = onBar ? BarButtonAt(v, x, e.value("y", 0)) : -1;
                if (type == "down") {
                    v.pressedBtn = hit;
                    RECT a = AddressRect(v);
                    if (hit < 0 && x >= a.left && x < a.right) { if (!v.editing) StartEditing(v); else { v.selectAll = false; v.caret = v.edit.size(); } }
                } else {
                    if (hit >= 0 && hit == v.pressedBtn) BarButton(v, hit);
                    v.pressedBtn = -1;
                }
                redraw = true;
                continue;
            }
            if (type == "down" && v.editing) { v.editing = false; redraw = true; }   // a click on the page ends typing in the bar
            if (type == "down") v.buttons |= 1 << bt; else v.buttons &= ~(1 << bt);
            if (v.web) Cdp(v, L"Input.dispatchMouseEvent", {{"type", type == "down" ? "mousePressed" : "mouseReleased"}, {"x", CssX(v, x)}, {"y", CssY(v, y)},
                                                          {"button", ButtonName(bt)}, {"buttons", v.buttons}, {"clickCount", 1}});
        }
        else if (type == "wheel") { if (v.web && !onBar) Cdp(v, L"Input.dispatchMouseEvent", {{"type", "mouseWheel"}, {"x", CssX(v, x)}, {"y", CssY(v, y)}, {"deltaX", 0}, {"deltaY", -e.value("delta", 0)}}); }
        else if (type == "char") {
            if (v.editing) { std::wstring t = Wide(e.value("text", std::string())); if (!t.empty() && t[0] >= 32) Type(v, t); redraw = true; }
            else if (v.web) Cdp(v, L"Input.insertText", {{"text", e.value("text", std::string())}});
        }
        else if (type == "key") {
            const int vk = e.value("vk", 0);
            const bool ctrl = e.value("ctrl", false), shift = e.value("shift", false), alt = e.value("alt", false);
            if (v.editing) { if (EditKey(v, vk, ctrl)) redraw = true; continue; }
            if (v.set.browser && ctrl && vk == 'L') { StartEditing(v); redraw = true; continue; }   // Ctrl+L: to the address bar, like in every browser
            if (v.set.browser && alt && (vk == VK_LEFT || vk == VK_RIGHT)) { BarButton(v, vk == VK_LEFT ? BtnBack : BtnForward); continue; }
            if (v.set.browser && vk == VK_F5) { BarButton(v, BtnReload); continue; }
            if (!v.web) continue;
            const int mods = (ctrl ? 2 : 0) | (shift ? 8 : 0) | (alt ? 1 : 0);   // CDP: alt 1, ctrl 2, meta 4, shift 8
            if (ctrl && vk == 'V') { std::wstring t = ClipboardText(); if (!t.empty()) Cdp(v, L"Input.insertText", {{"text", Utf8(t)}}); continue; }
            if (ctrl && (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'Z')) {
                const char* cmd = vk == 'A' ? "selectAll" : vk == 'C' ? "copy" : vk == 'X' ? "cut" : "undo";
                char key[2] = {(char)(vk + 32), 0}, code[5] = {'K', 'e', 'y', (char)vk, 0};
                Cdp(v, L"Input.dispatchKeyEvent", {{"type", "keyDown"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", key}, {"code", code}, {"commands", json::array({cmd})}});
                Cdp(v, L"Input.dispatchKeyEvent", {{"type", "keyUp"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", key}, {"code", code}});
                continue;
            }
            KeyInfo k;
            if (!SpecialKey(vk, k)) continue;   // letters come as "char"
            json down = {{"type", k.text ? "keyDown" : "rawKeyDown"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", k.key}, {"code", k.code}};
            if (k.text) down["text"] = k.text;
            Cdp(v, L"Input.dispatchKeyEvent", down);
            Cdp(v, L"Input.dispatchKeyEvent", {{"type", "keyUp"}, {"modifiers", mods}, {"windowsVirtualKeyCode", vk}, {"key", k.key}, {"code", k.code}});
        }
    }
    if (redraw && v.open) Present(v);
}

// 1.0.1: the web view must never take the focus from the game. Before, its window could become the active window: the
// game then no longer got its keys, and Realistic Radio (which checks for "a window of the game process in front")
// could hook the wrong window.
bool OwnWindow(HWND w) {
    if (!w) return false;
    for (View* v : g.views)
        if (v->host && (w == v->host || GetAncestor(w, GA_ROOT) == v->host || GetWindow(w, GW_OWNER) == v->host)) return true;
    return false;
}
void KeepFocusOnGame() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD pid = 0; GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId() && !OwnWindow(fg)) { g.game = fg; return; }   // the game (or another window of it) in front
    if (OwnWindow(fg) && g.game && IsWindow(g.game)) {
        SetForegroundWindow(g.game);
        static DWORD logged = 0;
        if (!logged || GetTickCount() - logged > 10000) { logged = GetTickCount(); Log("the web view took the focus - given back to the game"); }
    }
}

// every 16 ms on the web views' thread
void Tick() {
    DWORD now = GetTickCount();
    KeepFocusOnGame();
    for (View* pv : g.views) {
        View& v = *pv;
        if (now - v.lastReg >= 1000) {   // keep the app on the phone
            v.lastReg = now;
            rr.app_register(json{{"id", v.set.id}, {"name", Utf8(v.set.name)}, {"mode", "tablet"}, {"color", v.set.color}}.dump().c_str());
        }
        int w = 0, h = 0;
        bool open = rr.app_area(v.set.id.c_str(), &w, &h) != 0;
        Resize(v, w, h);
        if (open && !v.ctrl && !v.creating && !v.failed) { v.open = true; CreateView(v); }
        HandleInput(v);
        if (v.casting && v.lastFrameAt + 3000 < now && v.castAt + 3000 < now && v.web) {   // no frame for a while (page idle): ask for one picture
            v.castAt = now;
            StopCast(v); StartCast(v);
        }
        static DWORD blinkAt = 0;   // the caret blinks while typing in the address bar
        if (v.editing && v.open && now - blinkAt >= 265) { blinkAt = now; Present(v); }
    }
}

LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) { Tick(); return 0; }
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

bool EnsureHost(View& v) {
    if (v.host) return true;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&HostProc, &self);
    // never seen: far outside every monitor, no taskbar button, never activated
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int w = v.areaW > 0 ? v.areaW : 1280, h = v.areaH > 0 ? v.PageH() : 800;
    v.host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"RealisticRadioWebApp", L"Realistic Radio Web-App", WS_POPUP,
                             vx + vw + 4000, 0, w, h, nullptr, nullptr, self, nullptr);
    if (!v.host) return false;
    ShowWindow(v.host, SW_SHOWNOACTIVATE);   // WebView2 only renders into a shown window
    Log(Utf8(v.set.name) + ": browser window created (app opened on the phone)");
    return true;
}

DWORD WINAPI Worker(LPVOID) {
    bool found = false;
    for (int i = 0; i < 600 && !g_quit && !found; ++i) { found = rr_addon_load(&rr) != 0; if (!found) Sleep(500); }
    if (!found) return 0;   // Realistic Radio older than 1.7.0 or missing
    g.dir = PluginDir() + L"realistic-radio\\";
    std::vector<AppSettings> apps = LoadSettings(g.dir + L"webapp.json");
    std::string names;
    for (auto& a : apps) {
        g.views.push_back(new View());
        g.views.back()->set = a;
        names += (names.empty() ? "" : ", ") + Utf8(a.name) + (a.browser ? " (browser)" : "") + " " + Utf8(a.url);
    }
    Log("add-on " WA_VERSION " started: " + names);
    // physical pixels for the window and the page (no Windows scaling in between)
    using SetCtx = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto f = (SetCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext")) f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&g.wic);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&HostProc, &self);
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = self; wc.lpszClassName = L"RealisticRadioWebApp";
    RegisterClassW(&wc);
    // 1.0.1: until an app is opened on the phone there is no visible window at all, only this message-only one
    g.timer = CreateWindowExW(0, wc.lpszClassName, L"Realistic Radio Web-App timer", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g.timer) { Log("could not create the timer window"); CoUninitialize(); return 0; }
    SetTimer(g.timer, 1, 16, nullptr);
    MSG msg;
    while (!g_quit && GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    KillTimer(g.timer, 1);
    for (View* v : g.views) {
        if (v->ctrl) { v->ctrl->Close(); v->ctrl->Release(); v->ctrl = nullptr; }
        if (v->web) { v->web->Release(); v->web = nullptr; }
        if (v->host) { DestroyWindow(v->host); v->host = nullptr; }
        delete v;
    }
    g.views.clear();
    if (g.env) { g.env->Release(); g.env = nullptr; }
    if (g.wic) { g.wic->Release(); g.wic = nullptr; }
    DestroyWindow(g.timer); g.timer = nullptr;
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
