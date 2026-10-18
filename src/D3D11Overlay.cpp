// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "D3D11Overlay.h"
#include "Logger.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcommon.h>
#include <map>
#include <algorithm>

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

// --- screen (render target) layer
typedef void(STDMETHODCALLTYPE* OMSetRTFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* OMSetRTUAVFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*,
                                              UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
// one entry per context vtable we patched (the runtime may use more than one context class)
struct VtHook { void** vt; OMSetRTFn rt; OMSetRTUAVFn rtuav; };
VtHook g_vts[8]; std::atomic<int> g_nVts{0};
inline const VtHook* FindVt(ID3D11DeviceContext* c) {
    void** vt = *(void***)c; int n = g_nVts.load();
    for (int i = 0; i < n; ++i) if (g_vts[i].vt == vt) return &g_vts[i];
    return n ? &g_vts[0] : nullptr;
}
bool g_wantScreenHook = false;
std::atomic<bool> g_screenHooked{false};
// Screen layers: 0 = Realistic Radio, 1 = Trucker Chat (combined DLL). Each has its own picture, target size and area;
// both are drawn into a matching render target in this order.
constexpr int kLayers = 2;
std::atomic<float> g_scrAlpha[kLayers];
std::atomic<int> g_scrW[kLayers], g_scrH[kLayers], g_scrFmt[kLayers];
// several textures of the screen's size can be rendered per frame (e.g. navigation and instrument display): which one
// to draw into, in the order the game renders them each frame (0 = all of them)
std::atomic<int> g_scrTargetIdx{1}, g_lastFrameTargets[kLayers];
ID3D11Resource* g_frameTargets[kLayers][8] = {}; int g_nFrameTargets[kLayers] = {};   // render thread only, identity only (no reference)
std::atomic<float> g_areaX[kLayers], g_areaY[kLayers], g_areaW[kLayers], g_areaH[kLayers];
std::atomic<bool> g_flipY[kLayers], g_flipX[kLayers];
// back-buffer picture of the chat (layer 1), drawn below the radio overlay / menu
std::mutex g_img2Mtx;
std::shared_ptr<const OverlayImage> g_img2;
std::atomic<unsigned> g_img2Version{0};
std::atomic<float> g_alpha2{0.f};
std::atomic<int> g_anchor2{1};
std::atomic<float> g_offX2{0.f}, g_offY2{0.07f};
std::atomic<bool> g_collect{false};
std::atomic<long long> g_rtCalls{0};
std::mutex g_scrImgMtx[kLayers];
std::shared_ptr<const OverlayImage> g_scrImg[kLayers];
std::atomic<unsigned> g_scrImgVersion[kLayers];
std::mutex g_statMtx;
struct StatEntry { int w, h, fmt; long long binds; };
std::map<long long, StatEntry> g_stats;
std::atomic<int> g_bbW{0};
// mouse cursor of the settings menu (drawn as its own small quad, position updated by the input thread)
std::mutex g_curImgMtx;
std::shared_ptr<const OverlayImage> g_curImg;
std::atomic<unsigned> g_curImgVersion{0};
std::atomic<bool> g_cursorOn{false};
std::atomic<float> g_cursorX{0}, g_cursorY{0};
std::atomic<float> g_imgX{0}, g_imgY{0}, g_imgW{0}, g_imgH{0};   // where the overlay image was drawn last (back-buffer pixels)
// auto-detection of the visible screen area (beta): one copy of the game's own picture in the screen target
std::atomic<int> g_capState{0};       // 0 idle, 1 requested, 2 done, 3 failed: multisampled, 4 failed: format, 5 failed: empty
std::atomic<float> g_capRect[4];
// render-thread only
ID3D11RenderTargetView* g_curRtv = nullptr;
int g_curW = 0, g_curH = 0;
int g_curMask = 0;   // layers that draw into g_curRtv

// Render resources (only touched on the game's render thread, or after the hook is removed)
struct Res {
    ID3D11Device* dev = nullptr;   // not AddRef'd, identity only
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* samp = nullptr; ID3D11BlendState* blend = nullptr; ID3D11RasterizerState* rast = nullptr;
    ID3DDeviceContextState* state = nullptr;
    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; unsigned texVersion = 0; int texW = 0, texH = 0, texScreenH = 1080;
    ID3D11Texture2D* stex[kLayers] = {}; ID3D11ShaderResourceView* ssrv[kLayers] = {}; unsigned stexVersion[kLayers] = {};
    ID3D11Texture2D* tex2 = nullptr; ID3D11ShaderResourceView* srv2 = nullptr; unsigned tex2Version = 0; int tex2W = 0, tex2H = 0, tex2ScreenH = 1080;
    ID3D11Texture2D* ctex = nullptr; ID3D11ShaderResourceView* csrv = nullptr; unsigned ctexVersion = 0; int cW = 0, cH = 0;
    bool failed = false;
    template <class T> static void R(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void ReleaseAll() {
        R(vs); R(ps); R(cb); R(samp); R(blend); R(rast); R(state); R(tex); R(srv); R(ctex); R(csrv); R(tex2); R(srv2);
        for (int l = 0; l < kLayers; ++l) { R(stex[l]); R(ssrv[l]); stexVersion[l] = 0; }
        dev = nullptr; texVersion = 0; ctexVersion = 0; tex2Version = 0; failed = false;
    }
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
    if (g_res.dev == dev && !g_res.failed && g_res.vs) return true;
    if (g_res.dev == dev && g_res.failed) return false;
    g_res.ReleaseAll(); g_res.dev = dev;
    bool ok = SUCCEEDED(dev->CreateVertexShader(g_vsCode.data(), g_vsCode.size(), nullptr, &g_res.vs));
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

// Draws one textured quad into rtv. The game's pipeline state is saved and restored by hand (like Dear ImGui's
// DX11 backend). SwapDeviceContextState is NOT used: it can switch the context's vtable and silently remove hooks.
struct StateBackup {
    UINT nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, nSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* rs = nullptr; ID3D11BlendState* bs = nullptr; FLOAT bf[4]; UINT mask = 0;
    ID3D11DepthStencilState* dss = nullptr; UINT stencilRef = 0;
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {}; ID3D11DepthStencilView* dsv = nullptr;
    ID3D11ShaderResourceView* psSrv = nullptr; ID3D11SamplerState* psSmp = nullptr;
    ID3D11PixelShader* ps = nullptr; ID3D11VertexShader* vs = nullptr; ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr; ID3D11DomainShader* ds = nullptr;
    ID3D11ClassInstance* psI[256]; ID3D11ClassInstance* vsI[256]; ID3D11ClassInstance* gsI[256]; ID3D11ClassInstance* hsI[256]; ID3D11ClassInstance* dsI[256];
    UINT psN = 256, vsN = 256, gsN = 256, hsN = 256, dsN = 256;
    D3D11_PRIMITIVE_TOPOLOGY topo; ID3D11InputLayout* il = nullptr;
    ID3D11Buffer* vsCb = nullptr; ID3D11Buffer* psCb = nullptr;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOffset = 0; ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt; UINT ibOffset = 0;
    void Save(ID3D11DeviceContext* c) {
        c->RSGetViewports(&nVp, vp); c->RSGetScissorRects(&nSc, sc); c->RSGetState(&rs);
        c->OMGetBlendState(&bs, bf, &mask); c->OMGetDepthStencilState(&dss, &stencilRef);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->PSGetShaderResources(0, 1, &psSrv); c->PSGetSamplers(0, 1, &psSmp);
        c->PSGetShader(&ps, psI, &psN); c->VSGetShader(&vs, vsI, &vsN); c->GSGetShader(&gs, gsI, &gsN);
        c->HSGetShader(&hs, hsI, &hsN); c->DSGetShader(&ds, dsI, &dsN);
        c->VSGetConstantBuffers(0, 1, &vsCb); c->PSGetConstantBuffers(0, 1, &psCb);
        c->IAGetPrimitiveTopology(&topo); c->IAGetIndexBuffer(&ib, &ibFmt, &ibOffset);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset); c->IAGetInputLayout(&il);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->RSSetScissorRects(nSc, sc); c->RSSetViewports(nVp, vp); c->RSSetState(rs); if (rs) rs->Release();
        c->OMSetBlendState(bs, bf, mask); if (bs) bs->Release();
        c->OMSetDepthStencilState(dss, stencilRef); if (dss) dss->Release();
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        for (auto*& r : rtv) if (r) r->Release(); if (dsv) dsv->Release();
        c->PSSetShaderResources(0, 1, &psSrv); if (psSrv) psSrv->Release();
        c->PSSetSamplers(0, 1, &psSmp); if (psSmp) psSmp->Release();
        c->PSSetShader(ps, psI, psN); if (ps) ps->Release(); for (UINT i = 0; i < psN; ++i) if (psI[i]) psI[i]->Release();
        c->VSSetShader(vs, vsI, vsN); if (vs) vs->Release(); for (UINT i = 0; i < vsN; ++i) if (vsI[i]) vsI[i]->Release();
        c->GSSetShader(gs, gsI, gsN); if (gs) gs->Release(); for (UINT i = 0; i < gsN; ++i) if (gsI[i]) gsI[i]->Release();
        c->HSSetShader(hs, hsI, hsN); if (hs) hs->Release(); for (UINT i = 0; i < hsN; ++i) if (hsI[i]) hsI[i]->Release();
        c->DSSetShader(ds, dsI, dsN); if (ds) ds->Release(); for (UINT i = 0; i < dsN; ++i) if (dsI[i]) dsI[i]->Release();
        c->VSSetConstantBuffers(0, 1, &vsCb); if (vsCb) vsCb->Release();
        c->PSSetConstantBuffers(0, 1, &psCb); if (psCb) psCb->Release();
        c->IASetPrimitiveTopology(topo);
        c->IASetIndexBuffer(ib, ibFmt, ibOffset); if (ib) ib->Release();
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset); if (vb) vb->Release();
        c->IASetInputLayout(il); if (il) il->Release();
    }
};

void DrawQuad(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, float vpW, float vpH, ID3D11ShaderResourceView* srv, const float cbdata[8]) {
    StateBackup* b = new StateBackup();   // ~10 KB, keep it off the game's stack
    b->Save(c);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(g_res.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, cbdata, 8 * sizeof(float)); c->Unmap(g_res.cb, 0); }
    D3D11_VIEWPORT vp{0, 0, vpW, vpH, 0, 1};
    c->OMSetRenderTargets(1, &rtv, nullptr);
    float bf[4] = {0, 0, 0, 0};
    c->OMSetBlendState(g_res.blend, bf, 0xFFFFFFFF);
    c->OMSetDepthStencilState(nullptr, 0);
    c->RSSetViewports(1, &vp); c->RSSetState(g_res.rast);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->IASetInputLayout(nullptr);
    c->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    ID3D11Buffer* noVb = nullptr; UINT zero = 0; c->IASetVertexBuffers(0, 1, &noVb, &zero, &zero);
    c->VSSetShader(g_res.vs, nullptr, 0); c->VSSetConstantBuffers(0, 1, &g_res.cb);
    c->PSSetShader(g_res.ps, nullptr, 0); c->PSSetConstantBuffers(0, 1, &g_res.cb);
    c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->PSSetShaderResources(0, 1, &srv); c->PSSetSamplers(0, 1, &g_res.samp);
    c->Draw(4, 0);
    b->Restore(c);
    delete b;
}

bool MakeTexture(ID3D11Device* dev, const OverlayImage& img, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv) {
    std::vector<uint32_t> rgba(img.pixels.size());
    for (size_t i = 0; i < rgba.size(); ++i) { uint32_t p = img.pixels[i]; rgba[i] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16); }
    D3D11_TEXTURE2D_DESC td{}; td.Width = img.width; td.Height = img.height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba.data(), (UINT)img.width * 4, 0};
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex)) || FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) { Res::R(tex); Res::R(srv); return false; }
    return true;
}

bool EnsureScreenTexture(ID3D11Device* dev, int L) {
    unsigned v = g_scrImgVersion[L].load();
    if (g_res.ssrv[L] && g_res.stexVersion[L] == v) return true;
    std::shared_ptr<const OverlayImage> img;
    { std::unique_lock<std::mutex> l(g_scrImgMtx[L], std::try_to_lock); if (!l.owns_lock()) return g_res.ssrv[L] != nullptr; img = g_scrImg[L]; }
    Res::R(g_res.ssrv[L]); Res::R(g_res.stex[L]);
    g_res.stexVersion[L] = v;
    if (!img || img->width <= 0) return false;
    return MakeTexture(dev, *img, g_res.stex[L], g_res.ssrv[L]);
}

bool EnsureTexture2(ID3D11Device* dev) {   // chat picture on the back buffer
    unsigned v = g_img2Version.load();
    if (g_res.srv2 && g_res.tex2Version == v) return true;
    std::shared_ptr<const OverlayImage> img;
    { std::unique_lock<std::mutex> l(g_img2Mtx, std::try_to_lock); if (!l.owns_lock()) return g_res.srv2 != nullptr; img = g_img2; }
    Res::R(g_res.srv2); Res::R(g_res.tex2);
    g_res.tex2Version = v;
    if (!img || img->width <= 0) return false;
    g_res.tex2W = img->width; g_res.tex2H = img->height; g_res.tex2ScreenH = img->renderedForScreenHeight;
    return MakeTexture(dev, *img, g_res.tex2, g_res.srv2);
}

bool EnsureCursorTexture(ID3D11Device* dev) {
    unsigned v = g_curImgVersion.load();
    if (g_res.csrv && g_res.ctexVersion == v) return true;
    std::shared_ptr<const OverlayImage> img;
    { std::unique_lock<std::mutex> l(g_curImgMtx, std::try_to_lock); if (!l.owns_lock()) return g_res.csrv != nullptr; img = g_curImg; }
    Res::R(g_res.csrv); Res::R(g_res.ctex);
    g_res.ctexVersion = v;
    if (!img || img->width <= 0) return false;
    g_res.cW = img->width; g_res.cH = img->height;
    return MakeTexture(dev, *img, g_res.ctex, g_res.csrv);
}

// Copies the target the game has just finished (before our own picture is drawn on it) and finds the part the game
// actually drew into: rows and columns that differ from the background colour (the most common corner colour).
void CaptureTarget(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv) {
    if (g_capState.load() != 1) return;
    ID3D11Resource* res = nullptr; rtv->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) || !tex) { if (res) res->Release(); return; }
    D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
    static const int k4[] = {24, 25, 26, 27, 28, 29, 87, 88, 90, 91, 92, 93};
    bool fourBytes = std::find(std::begin(k4), std::end(k4), (int)d.Format) != std::end(k4);
    int state = 0;
    if (d.SampleDesc.Count > 1) state = 3;
    else if (!fourBytes) state = 4;
    else {
        D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
        ID3D11Device* dev = nullptr; ctx->GetDevice(&dev);
        ID3D11Texture2D* st = nullptr;
        if (dev && SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st)) && st) {
            ctx->CopyResource(st, tex);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
                const int W = d.Width, H = d.Height;
                auto px = [&](int x, int y) { return ((const uint32_t*)((const uint8_t*)m.pData + (size_t)y * m.RowPitch))[x]; };
                uint32_t corners[4] = {px(0, 0), px(W - 1, 0), px(0, H - 1), px(W - 1, H - 1)};
                uint32_t bg = corners[0]; int best = 0;
                for (uint32_t c : corners) { int n = 0; for (uint32_t o : corners) n += o == c; if (n > best) { best = n; bg = c; } }
                auto differs = [&](uint32_t p) {
                    int s = 0; for (int k = 0; k < 32; k += 8) s += std::abs((int)((p >> k) & 255) - (int)((bg >> k) & 255));
                    return s > 40;
                };
                std::vector<int> rows(H, 0), cols(W, 0);
                for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (differs(px(x, y))) { ++rows[y]; ++cols[x]; }
                int minR = std::max(2, W / 100), minC = std::max(2, H / 100);
                int y0 = 0, y1 = H - 1, x0 = 0, x1 = W - 1;
                while (y0 < H && rows[y0] < minR) ++y0;
                while (y1 > y0 && rows[y1] < minR) --y1;
                while (x0 < W && cols[x0] < minC) ++x0;
                while (x1 > x0 && cols[x1] < minC) --x1;
                ctx->Unmap(st, 0);
                if (y0 >= H || x0 >= W || (x1 - x0) < W / 10 || (y1 - y0) < H / 10) state = 5;
                else {
                    g_capRect[0] = (float)x0 / W; g_capRect[1] = (float)y0 / H;
                    g_capRect[2] = (float)(x1 + 1 - x0) / W; g_capRect[3] = (float)(y1 + 1 - y0) / H;
                    state = 2;
                }
                Log::Write("Screen detect: target %dx%d fmt %d, background 0x%08x, content x %d..%d y %d..%d", W, H, (int)d.Format, bg, x0, x1, y0, y1);
            }
            st->Release();
        }
        if (dev) dev->Release();
        if (!state) state = 4;
    }
    tex->Release(); res->Release();
    g_capState = state;
}

// Draws the screen image into the given render target (one the game has just finished rendering into)
void DrawIntoTarget(ID3D11DeviceContext* ctx, int L, ID3D11RenderTargetView* rtv, int w, int h) {
    float alpha = g_scrAlpha[L].load();
    if (alpha <= 0.003f || !rtv) return;
    ID3D11Device* dev = nullptr; ctx->GetDevice(&dev);
    if (!dev) return;
    if (EnsureResources(dev) && EnsureScreenTexture(dev, L) && g_res.ssrv[L]) {
        float ax = g_areaX[L], ay = g_areaY[L], aw = g_areaW[L], ah = g_areaH[L];
        float x0 = ax * 2 - 1, x1 = (ax + aw) * 2 - 1, yTop = 1 - ay * 2, yBot = 1 - (ay + ah) * 2;
        if (g_flipY[L]) std::swap(yTop, yBot);
        if (g_flipX[L]) std::swap(x0, x1);
        float cbdata[8] = {x0, yTop, x1, yBot, alpha, 0, 0, 0};
        DrawQuad(ctx, rtv, (float)w, (float)h, g_res.ssrv[L], cbdata);
    }
    dev->Release();
}

// Called for every render-target change of the immediate context (hot path: keep cheap when inactive)
void OnRenderTargetChange(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views) {
    ++g_rtCalls;
    bool drawL[kLayers];
    bool drawing = false;
    for (int l = 0; l < kLayers; ++l) { drawL[l] = g_scrAlpha[l].load() > 0.003f && g_scrW[l].load() > 0; drawing = drawing || drawL[l]; }
    bool collect = g_collect.load();
    bool capture = g_capState.load() == 1 && g_scrW[0].load() > 0;
    if (!drawing && !collect && !capture && !g_curRtv) return;
    ID3D11RenderTargetView* next = (num > 0 && views) ? views[0] : nullptr;
    if (next == g_curRtv) return;
    if (g_curRtv) {
        if ((g_curMask & 1) && capture) CaptureTarget(ctx, g_curRtv);   // the game's own picture, before ours
        for (int l = 0; l < kLayers; ++l)
            if ((g_curMask & (1 << l)) && drawL[l]) DrawIntoTarget(ctx, l, g_curRtv, g_curW, g_curH);   // the game is done with it
        g_curRtv->Release(); g_curRtv = nullptr; g_curMask = 0;
    }
    if (!next || (!drawing && !collect && !capture)) return;
    ID3D11Resource* res = nullptr; next->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex) {
        D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
        // skip the back buffer and full-scene buffers (render scaling can make them larger than the back buffer)
        long long bbArea = (long long)g_bbW.load() * g_screenH.load();
        long long limit = std::max(bbArea * 9 / 10, 2600000LL);   // never drop 2048x1024-class textures (screen atlases)
        bool sceneSized = bbArea > 0 && (long long)d.Width * d.Height >= limit;
        bool backBufferSized = (int)d.Width == g_bbW.load() && (int)d.Height == g_screenH.load();   // the game image itself
        if (bbArea > 0 && !sceneSized && !backBufferSized && d.Width >= 64 && d.Height >= 64) {
            if (collect) {
                long long key = ((long long)d.Width << 32) | ((long long)d.Height << 12) | (long long)d.Format;
                std::lock_guard<std::mutex> l(g_statMtx);
                auto& e = g_stats[key]; e.w = d.Width; e.h = d.Height; e.fmt = d.Format; ++e.binds;
            }
            int mask = 0;
            for (int l = 0; l < kLayers; ++l) {
                int fmt = g_scrFmt[l].load();
                if (!(drawL[l] || (l == 0 && capture)) || (int)d.Width != g_scrW[l].load() || (int)d.Height != g_scrH[l].load() || (fmt != 0 && fmt != (int)d.Format)) continue;
                int pos = 0;
                while (pos < g_nFrameTargets[l] && g_frameTargets[l][pos] != res) ++pos;
                if (pos == g_nFrameTargets[l] && g_nFrameTargets[l] < 8) g_frameTargets[l][g_nFrameTargets[l]++] = res;
                int want = g_scrTargetIdx.load();
                if (want <= 0 || pos + 1 == want) mask |= 1 << l;
            }
            if (mask) { g_curRtv = next; g_curRtv->AddRef(); g_curW = d.Width; g_curH = d.Height; g_curMask = mask; }
        }
        tex->Release();
    }
    if (res) res->Release();
}

void STDMETHODCALLTYPE HookOMSetRT(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* dsv) {
    if (!t_inHook && ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) { t_inHook = true; OnRenderTargetChange(ctx, num, views); t_inHook = false; }
    FindVt(ctx)->rt(ctx, num, views, dsv);
}
void STDMETHODCALLTYPE HookOMSetRTUAV(ID3D11DeviceContext* ctx, UINT num, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* dsv,
                                      UINT uavStart, UINT numUav, ID3D11UnorderedAccessView* const* uavs, const UINT* counts) {
    if (num != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL && !t_inHook && ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        t_inHook = true; OnRenderTargetChange(ctx, num, views); t_inHook = false;
    }
    FindVt(ctx)->rtuav(ctx, num, views, dsv, uavStart, numUav, uavs, counts);
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

bool PatchSlot(void** vt, int idx, void* fn, void** orig);
// Hooks the immediate context the game really uses. Its vtable can differ from a self-created device
// (e.g. single-threaded vs. multithreaded devices use different context classes), so take it from the game.
void HookGameContext(IDXGISwapChain* sc) {
    if (!g_wantScreenHook) return;
    ID3D11Device* d = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) || !d) return;
    ID3D11DeviceContext* c = nullptr; d->GetImmediateContext(&c);
    if (c) {
        void** cvt = *(void***)c;
        bool known = false;
        for (int i = 0; i < g_nVts.load(); ++i) if (g_vts[i].vt == cvt) known = true;
        if (!known && cvt[33] != (void*)&HookOMSetRT && g_nVts.load() < 8) {   // new context class: hook it (checked every frame)
            VtHook h{cvt, nullptr, nullptr};
            if (PatchSlot(cvt, 33, (void*)&HookOMSetRT, (void**)&h.rt) && PatchSlot(cvt, 34, (void*)&HookOMSetRTUAV, (void**)&h.rtuav)) {
                g_vts[g_nVts.load()] = h; ++g_nVts; g_screenHooked = true;
                Log::Write("Screen: hooked the game's render context #%d (device flags 0x%x)", g_nVts.load(), d->GetCreationFlags());
            }
        }
        c->Release();
    }
    d->Release();
}

void Draw(IDXGISwapChain* sc) {
    HookGameContext(sc);
    for (int l = 0; l < kLayers; ++l)   // end of frame
        if (g_nFrameTargets[l] > 0) { g_lastFrameTargets[l] = g_nFrameTargets[l]; g_nFrameTargets[l] = 0; }
    if (g_curRtv) {   // a matching screen target was still bound at the end of the frame
        ID3D11Device* d = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) && d) {
            ID3D11DeviceContext* c = nullptr; d->GetImmediateContext(&c);
            if (c) {
                if (g_curMask & 1) CaptureTarget(c, g_curRtv);
                for (int l = 0; l < kLayers; ++l) if ((g_curMask & (1 << l)) && g_scrAlpha[l].load() > 0.003f) DrawIntoTarget(c, l, g_curRtv, g_curW, g_curH);
                c->Release();
            }
            d->Release();
        }
        g_curRtv->Release(); g_curRtv = nullptr; g_curMask = 0;
    }
    if (!g_enabled) return;
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) { ++g_otherFrames; return; }
    ++g_d3d11Frames;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) { dev->Release(); return; }
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (bd.Height >= 200) { g_screenH = (int)bd.Height; g_bbW = (int)bd.Width; }
    float alpha2 = g_alpha2.load();   // chat (combined DLL), below the radio overlay and the menu
    if (alpha2 > 0.003f && bd.Width >= 320 && bd.Height >= 200 && EnsureResources(dev) && EnsureTexture2(dev) && g_res.srv2) {
        D3D11_RENDER_TARGET_VIEW_DESC rvd{}; rvd.Format = RtvFormat(bd.Format);
        rvd.ViewDimension = bd.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx && SUCCEEDED(dev->CreateRenderTargetView(bb, &rvd, &rtv))) {
            float k = (float)bd.Height / (float)std::max(1, g_res.tex2ScreenH);
            float w = g_res.tex2W * k, h = g_res.tex2H * k;
            float sw = (float)bd.Width, sh = (float)bd.Height, ox = g_offX2 * sh, oy = g_offY2 * sh;
            int a = g_anchor2; float x, y;
            x = (a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox;
            y = (a < 3) ? oy : sh - h - oy;
            x = std::floor(x); y = std::floor(y);
            float cbdata[8] = {x / sw * 2 - 1, 1 - y / sh * 2, (x + w) / sw * 2 - 1, 1 - (y + h) / sh * 2, alpha2, 0, 0, 0};
            DrawQuad(ctx, rtv, sw, sh, g_res.srv2, cbdata);
        }
        if (rtv) rtv->Release();
        if (ctx) ctx->Release();
    }
    float alpha = g_alpha.load();
    if (alpha > 0.003f && bd.Width >= 320 && bd.Height >= 200 && EnsureResources(dev) && EnsureTexture(dev) && g_res.srv) {
        D3D11_RENDER_TARGET_VIEW_DESC rvd{}; rvd.Format = RtvFormat(bd.Format);
        rvd.ViewDimension = bd.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx && SUCCEEDED(dev->CreateRenderTargetView(bb, &rvd, &rtv))) {
            float k = (float)bd.Height / (float)std::max(1, g_res.texScreenH);
            float w = g_res.texW * k, h = g_res.texH * k;
            float sw = (float)bd.Width, sh = (float)bd.Height, ox = g_offX * sh, oy = g_offY * sh;
            int a = g_anchor; float x, y;
            x = (a % 3 == 0) ? ox : (a % 3 == 1) ? (sw - w) / 2 + ox : sw - w - ox;
            y = (a < 3) ? oy : sh - h - oy;
            x = std::floor(x); y = std::floor(y);
            g_imgX = x; g_imgY = y; g_imgW = w; g_imgH = h;
            float cbdata[8] = {x / sw * 2 - 1, 1 - y / sh * 2, (x + w) / sw * 2 - 1, 1 - (y + h) / sh * 2, alpha, 0, 0, 0};
            DrawQuad(ctx, rtv, sw, sh, g_res.srv, cbdata);
            if (g_cursorOn && EnsureCursorTexture(dev) && g_res.csrv) {   // menu cursor on top
                float cx = std::floor(g_cursorX.load()), cy = std::floor(g_cursorY.load()), cw = (float)g_res.cW, ch = (float)g_res.cH;
                float cc[8] = {cx / sw * 2 - 1, 1 - cy / sh * 2, (cx + cw) / sw * 2 - 1, 1 - (cy + ch) / sh * 2, 1.f, 0, 0, 0};
                DrawQuad(ctx, rtv, sw, sh, g_res.csrv, cc);
            }
        }
        if (rtv) rtv->Release();
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
    static std::mutex installMtx;   // radio and chat thread (combined DLL) may call this at the same time
    std::lock_guard<std::mutex> lock(installMtx);
    if (g_vtable) { g_enabled = true; return true; }
    if (!CompileShaders()) return false;
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"RealisticRadioDummyD3D";
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
        if (ok && g_wantScreenHook) Log::Write("Screen: waiting for the game's first frame to hook its render context");
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
void EnableScreenHook(bool on) { if (on) g_wantScreenHook = true; }   // only switched on (the chat of the combined DLL may need it too)
void SetLayerScreenImage(int L, std::shared_ptr<const OverlayImage> img) { std::lock_guard<std::mutex> l(g_scrImgMtx[L]); g_scrImg[L] = std::move(img); ++g_scrImgVersion[L]; }
void SetLayerScreenTarget(int L, int w, int h, int format) { g_scrW[L] = w; g_scrH[L] = h; g_scrFmt[L] = format; }
void SetLayerScreenAlpha(int L, float a) { g_scrAlpha[L] = a; }
void SetLayerScreenArea(int L, float x, float y, float w, float h, bool flipY, bool flipX) {
    g_areaX[L] = x; g_areaY[L] = y; g_areaW[L] = w; g_areaH[L] = h; g_flipY[L] = flipY; g_flipX[L] = flipX;
}
void SetScreenImage(std::shared_ptr<const OverlayImage> img) { SetLayerScreenImage(0, std::move(img)); }
void SetScreenTarget(int w, int h, int format) { SetLayerScreenTarget(0, w, h, format); }
void SetScreenAlpha(float a) { SetLayerScreenAlpha(0, a); }
void SetScreenTargetIndex(int i) { g_scrTargetIdx = i; }
int ScreenTargetsSeen() { return g_lastFrameTargets[0]; }
void SetScreenArea(float x, float y, float w, float h, bool flipY, bool flipX) { SetLayerScreenArea(0, x, y, w, h, flipY, flipX); }
void SetChatImage(std::shared_ptr<const OverlayImage> img) { std::lock_guard<std::mutex> l(g_img2Mtx); g_img2 = std::move(img); ++g_img2Version; }
void SetChatAlpha(float a) { g_alpha2 = a; }
void SetChatPlacement(int anchor, float offsetX, float offsetY) { g_anchor2 = anchor; g_offX2 = offsetX; g_offY2 = offsetY; }
void SetCollectTargets(bool on) { g_collect = on; }
std::vector<RenderTargetStat> TakeTargetStats() {
    std::lock_guard<std::mutex> l(g_statMtx);
    std::vector<RenderTargetStat> v;
    for (auto& [k, e] : g_stats) v.push_back({e.w, e.h, e.fmt, e.binds});
    g_stats.clear();
    return v;
}
bool ScreenHookActive() { return g_wantScreenHook && g_vtable != nullptr; }
long long RenderTargetCalls() { return g_rtCalls; }
void SetImage(std::shared_ptr<const OverlayImage> img) { std::lock_guard<std::mutex> l(g_imgMtx); g_img = std::move(img); ++g_imgVersion; }
void SetAlpha(float a) { g_alpha = a; }
void SetPlacement(int anchor, float ox, float oy) { g_anchor = anchor; g_offX = ox; g_offY = oy; }
long long D3D11Frames() { return g_d3d11Frames; }
long long OtherFrames() { return g_otherFrames; }
int ScreenHeight() { return g_screenH; }
int ScreenWidth() { return g_bbW; }
void RequestScreenCapture() { g_capState = 1; }
int ScreenCaptureState(float rect[4]) { int s = g_capState.load(); if (s == 2) for (int i = 0; i < 4; ++i) rect[i] = g_capRect[i]; return s; }
void ResetScreenCapture() { g_capState = 0; }
void SetCursorImage(std::shared_ptr<const OverlayImage> img) { std::lock_guard<std::mutex> l(g_curImgMtx); g_curImg = std::move(img); ++g_curImgVersion; }
void SetCursorVisible(bool on) { g_cursorOn = on; }
void SetCursorPos(float x, float y) { g_cursorX = x; g_cursorY = y; }
void CursorPos(float& x, float& y) { x = g_cursorX; y = g_cursorY; }
bool ImageRect(float& x, float& y, float& w, float& h) { x = g_imgX; y = g_imgY; w = g_imgW; h = g_imgH; return w > 0; }
}
