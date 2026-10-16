// ETS2 Local Radio Native
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "D3D11Overlay.h"
#include "Logger.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcommon.h>

namespace {
typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

void** g_vtable = nullptr;
PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
bool g_hasPresent1 = false;
std::atomic<bool> g_enabled{false};
std::atomic<float> g_alpha{0.f};
std::atomic<int> g_anchor{1};
std::atomic<float> g_offX{0.f}, g_offY{0.07f};
std::atomic<long long> g_d3d11Frames{0}, g_otherFrames{0};
std::atomic<int> g_screenH{0};
std::vector<char> g_vsCode, g_psCode;

std::mutex g_imgMtx;
std::shared_ptr<const OverlayImage> g_img;
std::atomic<unsigned> g_imgVersion{0};
thread_local bool t_inHook = false;

// Render resources (only touched on the game's render thread, or after the hook is removed)
struct Res {
    ID3D11Device* dev = nullptr;   // not AddRef'd, identity only
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* samp = nullptr; ID3D11BlendState* blend = nullptr; ID3D11RasterizerState* rast = nullptr;
    ID3DDeviceContextState* state = nullptr;
    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; unsigned texVersion = 0; int texW = 0, texH = 0, texScreenH = 1080;
    bool failed = false;
    template <class T> static void R(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void ReleaseAll() { R(vs); R(ps); R(cb); R(samp); R(blend); R(rast); R(state); R(tex); R(srv); dev = nullptr; texVersion = 0; failed = false; }
} g_res;

const char* kShader = R"(
cbuffer CB : register(b0) { float4 rect; float4 alpha; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs_main(uint id : SV_VertexID) {
    VO o; float2 uv = float2(id & 1, id >> 1);
    o.pos = float4(lerp(rect.x, rect.z, uv.x), lerp(rect.y, rect.w, uv.y), 0, 1); o.uv = uv; return o; }
float4 ps_main(VO i) : SV_Target { return tex.Sample(smp, i.uv) * alpha.x; }
)";

bool CompileShaders() {
    if (!g_vsCode.empty()) return true;
    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");   // part of Windows 10/11
    if (!m) { Log::Write("Overlay: d3dcompiler_47.dll not found"); return false; }
    auto compile = (D3DCompileFn)GetProcAddress(m, "D3DCompile");
    if (!compile) return false;
    auto one = [&](const char* entry, const char* target, std::vector<char>& out) {
        ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
        HRESULT hr = compile(kShader, strlen(kShader), "overlay", nullptr, nullptr, entry, target, 0, 0, &code, &err);
        if (FAILED(hr)) { Log::Write("Overlay shader error: %s", err ? (const char*)err->GetBufferPointer() : "?"); if (err) err->Release(); return false; }
        out.assign((char*)code->GetBufferPointer(), (char*)code->GetBufferPointer() + code->GetBufferSize());
        code->Release(); if (err) err->Release(); return true;
    };
    return one("vs_main", "vs_4_0", g_vsCode) && one("ps_main", "ps_4_0", g_psCode);
}

bool EnsureResources(ID3D11Device* dev) {
    if (g_res.dev == dev && !g_res.failed && g_res.state) return true;
    if (g_res.dev == dev && g_res.failed) return false;
    g_res.ReleaseAll(); g_res.dev = dev;
    ID3D11Device1* dev1 = nullptr;
    bool ok = SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1));   // D3D 11.1 runtime (Windows 8+)
    if (ok) {
        D3D_FEATURE_LEVEL fl = dev->GetFeatureLevel();
        ok = SUCCEEDED(dev1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &g_res.state));
        dev1->Release();
    }
    ok = ok && SUCCEEDED(dev->CreateVertexShader(g_vsCode.data(), g_vsCode.size(), nullptr, &g_res.vs));
    ok = ok && SUCCEEDED(dev->CreatePixelShader(g_psCode.data(), g_psCode.size(), nullptr, &g_res.ps));
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &g_res.cb));
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sd, &g_res.samp));
    D3D11_BLEND_DESC bl{}; auto& rt = bl.RenderTarget[0];
    rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bl, &g_res.blend));
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &g_res.rast));
    if (!ok) { Log::Write("Overlay: could not create D3D11 resources"); g_res.ReleaseAll(); g_res.dev = dev; g_res.failed = true; return false; }
    Log::Write("Overlay: D3D11 renderer ready (feature level 0x%x)", (unsigned)dev->GetFeatureLevel());
    return true;
}

bool EnsureTexture(ID3D11Device* dev) {
    unsigned v = g_imgVersion.load();
    if (g_res.srv && g_res.texVersion == v) return true;
    std::shared_ptr<const OverlayImage> img;
    { std::unique_lock<std::mutex> l(g_imgMtx, std::try_to_lock); if (!l.owns_lock()) return g_res.srv != nullptr; img = g_img; }
    Res::R(g_res.srv); Res::R(g_res.tex);
    g_res.texVersion = v;
    if (!img || img->width <= 0) return false;
    std::vector<uint32_t> rgba(img->pixels.size());   // BGRA -> RGBA (R8G8B8A8 is mandatory for every D3D11 GPU)
    for (size_t i = 0; i < rgba.size(); ++i) { uint32_t p = img->pixels[i]; rgba[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16); }
    D3D11_TEXTURE2D_DESC td{}; td.Width = img->width; td.Height = img->height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba.data(), (UINT)img->width * 4, 0};
    if (FAILED(dev->CreateTexture2D(&td, &init, &g_res.tex)) || FAILED(dev->CreateShaderResourceView(g_res.tex, nullptr, &g_res.srv))) {
        Res::R(g_res.tex); Res::R(g_res.srv); return false;
    }
    g_res.texW = img->width; g_res.texH = img->height; g_res.texScreenH = img->renderedForScreenHeight;
    return true;
}

DXGI_FORMAT RtvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

void Draw(IDXGISwapChain* sc) {
    if (!g_enabled) return;
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) { ++g_otherFrames; return; }
    ++g_d3d11Frames;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) { dev->Release(); return; }
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (bd.Height >= 200) g_screenH = (int)bd.Height;
    float alpha = g_alpha.load();
    if (alpha > 0.003f && bd.Width >= 320 && bd.Height >= 200 && EnsureResources(dev) && EnsureTexture(dev) && g_res.srv) {
        D3D11_RENDER_TARGET_VIEW_DESC rvd{}; rvd.Format = RtvFormat(bd.Format);
        rvd.ViewDimension = bd.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        ID3D11DeviceContext1* ctx1 = nullptr;
        if (ctx && SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&ctx1)) &&
            SUCCEEDED(dev->CreateRenderTargetView(bb, &rvd, &rtv))) {
            ID3DDeviceContextState* prev = nullptr;
            ctx1->SwapDeviceContextState(g_res.state, &prev);   // our own clean pipeline state; game state restored below
            // geometry in pixels -> NDC
            float k = (float)bd.Height / (float)std::max(1, g_res.texScreenH);
            float w = g_res.texW * k, h = g_res.texH * k;
            float sw = (float)bd.Width, sh = (float)bd.Height, ox = g_offX * sh, oy = g_offY * sh;
            int a = g_anchor; float x, y;
            x = (a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox;
            y = (a < 3) ? oy : sh - h - oy;
            x = std::floor(x); y = std::floor(y);
            float cbdata[8] = {x / sw * 2 - 1, 1 - y / sh * 2, (x + w) / sw * 2 - 1, 1 - (y + h) / sh * 2, alpha, 0, 0, 0};
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx1->Map(g_res.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, cbdata, sizeof(cbdata)); ctx1->Unmap(g_res.cb, 0); }
            D3D11_VIEWPORT vp{0, 0, sw, sh, 0, 1};
            ctx1->OMSetRenderTargets(1, &rtv, nullptr);
            float bf[4] = {0, 0, 0, 0};
            ctx1->OMSetBlendState(g_res.blend, bf, 0xFFFFFFFF);
            ctx1->RSSetViewports(1, &vp); ctx1->RSSetState(g_res.rast);
            ctx1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx1->IASetInputLayout(nullptr);
            ctx1->VSSetShader(g_res.vs, nullptr, 0); ctx1->VSSetConstantBuffers(0, 1, &g_res.cb);
            ctx1->PSSetShader(g_res.ps, nullptr, 0); ctx1->PSSetConstantBuffers(0, 1, &g_res.cb);
            ctx1->PSSetShaderResources(0, 1, &g_res.srv); ctx1->PSSetSamplers(0, 1, &g_res.samp);
            ctx1->Draw(4, 0);
            ctx1->SwapDeviceContextState(prev, nullptr);
            if (prev) prev->Release();
        }
        if (rtv) rtv->Release();
        if (ctx1) ctx1->Release();
        if (ctx) ctx->Release();
    }
    bb->Release(); dev->Release();
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!t_inHook && !(flags & DXGI_PRESENT_TEST)) { t_inHook = true; Draw(sc); t_inHook = false; }
    return g_origPresent(sc, sync, flags);
}
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    if (!t_inHook && !(flags & DXGI_PRESENT_TEST)) { t_inHook = true; Draw(sc); t_inHook = false; }
    return g_origPresent1(sc, sync, flags, p);
}

bool PatchSlot(void** vt, int idx, void* fn, void** orig) {
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    if (orig) *orig = vt[idx];
    InterlockedExchangePointer(&vt[idx], fn);
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), &vt[idx], sizeof(void*));
    return true;
}
} // namespace

namespace D3D11Overlay {
bool Install() {
    if (g_vtable) { g_enabled = true; return true; }
    if (!CompileShaders()) return false;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"LocalRadioDummyD3D";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount = 1; sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE t : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, t, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vt = *(void***)sc;
        IDXGISwapChain1* sc1 = nullptr;
        g_hasPresent1 = SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1));
        if (sc1) sc1->Release();
        if (vt[8] == (void*)&HookPresent) ok = true;   // already hooked (plugin re-initialised)
        else {
            ok = PatchSlot(vt, 8, (void*)&HookPresent, (void**)&g_origPresent);
            if (ok && g_hasPresent1) PatchSlot(vt, 22, (void*)&HookPresent1, (void**)&g_origPresent1);
        }
        if (ok) {
            g_vtable = vt;
            // The game may call our hook until the process ends, so keep this DLL loaded for the process lifetime.
            HMODULE self; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&HookPresent, &self);
        }
    } else Log::Write("Overlay: D3D11 dummy device failed (hr=0x%08lx)", (unsigned long)hr);
    if (ctx) ctx->Release(); if (dev) dev->Release(); if (sc) sc->Release();
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_enabled = ok;
    Log::Write(ok ? "Overlay: Present hook installed%s" : "Overlay: Present hook NOT installed%s", g_hasPresent1 ? " (incl. Present1)" : "");
    return ok;
}
void Uninstall() {
    g_enabled = false;   // module is pinned: the hook stays in place but draws nothing
    g_alpha = 0;
}
void SetImage(std::shared_ptr<const OverlayImage> img) { std::lock_guard<std::mutex> l(g_imgMtx); g_img = std::move(img); ++g_imgVersion; }
void SetAlpha(float a) { g_alpha = a; }
void SetPlacement(int anchor, float ox, float oy) { g_anchor = anchor; g_offX = ox; g_offY = oy; }
long long D3D11Frames() { return g_d3d11Frames; }
long long OtherFrames() { return g_otherFrames; }
int ScreenHeight() { return g_screenH; }
}
