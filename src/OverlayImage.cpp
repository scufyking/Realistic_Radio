// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "OverlayImage.h"
#include "Logger.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
inline uint32_t Pack(float a, float r, float g, float b) {   // premultiplied input 0..1
    auto c = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.f), 0, 255); };
    return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
}
inline void Over(uint32_t& dst, float a, float r, float g, float b) {   // src premultiplied
    float da = ((dst >> 24) & 255) / 255.f, dr = ((dst >> 16) & 255) / 255.f, dg = ((dst >> 8) & 255) / 255.f, db = (dst & 255) / 255.f;
    float k = 1.f - a;
    dst = Pack(a + da * k, r + dr * k, g + dg * k, b + db * k);
}

struct Gdi {
    HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; uint32_t* bits = nullptr; int w = 0, h = 0;
    Gdi(int W, int H) : w(W), h(H) {
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        old = SelectObject(dc, bmp);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    }
    ~Gdi() { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); }
    void Clear() { GdiFlush(); std::fill(bits, bits + w * h, 0u); }
};

const wchar_t* g_fontName = L"Segoe UI";   // renderers run on the radio thread only
struct FontScope {   // a design may use another font for one picture
    const wchar_t* old;
    explicit FontScope(const wchar_t* f) : old(g_fontName) { g_fontName = f; }
    ~FontScope() { g_fontName = old; }
};
HFONT MakeFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, g_fontName);
}
SIZE Measure(HDC dc, HFONT f, const std::wstring& s) {
    SIZE sz{0, 0}; HGDIOBJ o = SelectObject(dc, f);
    RECT r{0, 0, 4000, 1000}; DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, o); sz.cx = r.right; sz.cy = r.bottom; return sz;
}
// Text is drawn white into the mask (DrawTextW clips to the rectangle) and blended with the given colour into out.
// Only the rectangle is cleared and blended, so many small texts on a large image stay cheap.
RECT ClipRect(const Gdi& m, RECT r) {
    r.left = std::max<LONG>(0, r.left); r.top = std::max<LONG>(0, r.top);
    r.right = std::min<LONG>(m.w, r.right); r.bottom = std::min<LONG>(m.h, r.bottom);
    return r;
}
void BlendMask(Gdi& mask, std::vector<uint32_t>& out, RECT c, float R, float G, float B) {
    for (int y = c.top; y < c.bottom; ++y) for (int x = c.left; x < c.right; ++x) {
        size_t i = (size_t)y * mask.w + x;
        uint32_t p = mask.bits[i];
        float cov = std::max({(p >> 16) & 255u, (p >> 8) & 255u, p & 255u}) / 255.f;
        if (cov > 0) Over(out[i], cov, R * cov, G * cov, B * cov);
    }
}
void ClearMask(Gdi& mask, RECT c) {
    GdiFlush();
    for (int y = c.top; y < c.bottom; ++y) std::fill(mask.bits + (size_t)y * mask.w + c.left, mask.bits + (size_t)y * mask.w + c.right, 0u);
}
void DrawText(Gdi& mask, std::vector<uint32_t>& out, HFONT f, const std::wstring& s, RECT r, float R, float G, float B, UINT align = 0) {
    if (s.empty()) return;
    RECT c = ClipRect(mask, r);
    if (c.right <= c.left || c.bottom <= c.top) return;
    ClearMask(mask, c);
    HGDIOBJ o = SelectObject(mask.dc, f);
    DrawTextW(mask.dc, s.c_str(), (int)s.size(), &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER | align);
    SelectObject(mask.dc, o); GdiFlush();
    BlendMask(mask, out, c, R, G, B);
}
float RoundedCoverage(int x, int y, int w, int h, float rad) {
    float px = x + 0.5f, py = y + 0.5f;
    float cx = std::clamp(px, rad, w - rad), cy = std::clamp(py, rad, h - rad);
    float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    return std::clamp(rad - d + 0.5f, 0.f, 1.f);
}
} // namespace

std::shared_ptr<OverlayImage> RenderOverlay(const OverlayInfo& info, int screenH, double scale, const OverlayImage* logo) {
    const float s = (float)std::clamp(screenH / 1080.0 * scale, 0.3, 4.0);
    auto S = [&](float v) { return (int)std::lround(v * s); };
    HFONT fHead = MakeFont(S(17), FW_NORMAL), fTitle = MakeFont(S(28), FW_BOLD), fSub = MakeFont(S(17), FW_NORMAL);
    HDC tmp = CreateCompatibleDC(nullptr);
    std::wstring sub = info.subtitle + (info.favourite ? L"  ♥" : L"");
    int textW = std::max({Measure(tmp, fHead, info.headline).cx, Measure(tmp, fTitle, info.title).cx, Measure(tmp, fSub, sub).cx});
    textW = std::clamp(textW, S(180), S(520));
    DeleteDC(tmp);

    const int pad = S(14), lineHead = S(22), lineTitle = S(36), lineSub = S(22);
    const int textH = (info.headline.empty() ? 0 : lineHead) + lineTitle + (sub.empty() ? 0 : lineSub);
    int logoW = 0, logoH = 0;
    if (logo && logo->width > 0 && logo->height > 0) {   // same fitting rule as Station.cs (90 high or 220 wide)
        float maxH = S(64), maxW = S(150);
        float k = std::min(maxH / logo->height, maxW / logo->width);
        logoW = std::max(1, (int)(logo->width * k)); logoH = std::max(1, (int)(logo->height * k));
    }
    const int barsW = info.bars >= 0 ? S(5 * 6 + 4 * 3) : 0;
    const int accent = S(4);
    const int W = accent + pad + (logoW ? logoW + pad : 0) + textW + (barsW ? pad + barsW : 0) + pad;
    const int H = std::max(textH, logoH) + 2 * pad;

    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = screenH; img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;
    // panel: dark, slightly transparent, rounded corners, orange accent bar on the left (orange = original #FFAE00)
    const float rad = (float)S(10);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float cov = RoundedCoverage(x, y, W, H, rad);
        if (cov <= 0) continue;
        float a = 0.82f * cov;
        if (x < accent) px[y * W + x] = Pack(cov, 1.f * cov, 0.682f * cov, 0.f);
        else px[y * W + x] = Pack(a, 0.07f * a, 0.07f * a, 0.085f * a);
    }
    int x0 = accent + pad;
    if (logoW) {   // nearest-neighbour is too ugly -> simple box filter downscale
        int ly = (H - logoH) / 2;
        for (int y = 0; y < logoH; ++y) for (int x = 0; x < logoW; ++x) {
            int sx0 = x * logo->width / logoW, sx1 = std::max(sx0 + 1, (x + 1) * logo->width / logoW);
            int sy0 = y * logo->height / logoH, sy1 = std::max(sy0 + 1, (y + 1) * logo->height / logoH);
            float a = 0, r = 0, g = 0, b = 0; int n = 0;
            for (int yy = sy0; yy < sy1; ++yy) for (int xx = sx0; xx < sx1; ++xx) {
                uint32_t p = logo->pixels[(size_t)yy * logo->width + xx];
                a += (p >> 24) & 255; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; ++n;
            }
            Over(px[(size_t)(ly + y) * W + x0 + x], a / n / 255.f, r / n / 255.f, g / n / 255.f, b / n / 255.f);
        }
        x0 += logoW + pad;
    }
    Gdi mask(W, H);
    int y = (H - textH) / 2;
    if (!info.headline.empty()) { DrawText(mask, px, fHead, info.headline, RECT{x0, y, x0 + textW, y + lineHead}, 0.92f, 0.92f, 0.92f); y += lineHead; }
    DrawText(mask, px, fTitle, info.title, RECT{x0, y, x0 + textW, y + lineTitle}, 1.f, 0.682f, 0.f); y += lineTitle;
    if (!sub.empty()) DrawText(mask, px, fSub, sub, RECT{x0, y, x0 + textW, y + lineSub}, 0.75f, 0.75f, 0.78f);
    if (barsW) {   // 5 signal bars
        int bx = x0 + textW + pad, base = H / 2 + S(14);
        for (int i = 0; i < 5; ++i) {
            int bh = S(8 + i * 5), bw = S(6), xs = bx + i * S(9);
            bool on = i < info.bars;
            for (int yy = base - bh; yy < base; ++yy) for (int xx = xs; xx < xs + bw; ++xx)
                if (on) Over(px[(size_t)yy * W + xx], 1.f, 1.f, 0.682f, 0.f); else Over(px[(size_t)yy * W + xx], 0.35f, 0.35f * 0.6f, 0.35f * 0.6f, 0.35f * 0.6f);
        }
    }
    DeleteObject(fHead); DeleteObject(fTitle); DeleteObject(fSub);
    return img;
}

std::shared_ptr<OverlayImage> LoadImageFile(const std::wstring& path) {
    IWICImagingFactory* f = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* frame = nullptr; IWICFormatConverter* conv = nullptr;
    std::shared_ptr<OverlayImage> img;
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) break;
        if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) break;
        if (FAILED(dec->GetFrame(0, &frame))) break;
        if (FAILED(f->CreateFormatConverter(&conv))) break;
        if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) break;
        UINT w = 0, h = 0; conv->GetSize(&w, &h);
        if (!w || !h || w > 4096 || h > 4096) break;
        img = std::make_shared<OverlayImage>(); img->width = w; img->height = h; img->pixels.resize((size_t)w * h);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)img->pixels.data()))) img.reset();
    } while (false);
    if (conv) conv->Release(); if (frame) frame->Release(); if (dec) dec->Release(); if (f) f->Release();
    return img;
}

namespace {
void FillRect(std::vector<uint32_t>& px, int W, int H, int x0, int y0, int x1, int y1, float a, float r, float g, float b) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(W, x1); y1 = std::min(H, y1);
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) Over(px[(size_t)y * W + x], a, r * a, g * a, b * a);
}
void BlitLogo(std::vector<uint32_t>& px, int W, const OverlayImage* logo, int bx, int by, int bw, int bh) {
    if (!logo || logo->width <= 0 || logo->height <= 0) return;
    float k = std::min((float)bw / logo->width, (float)bh / logo->height);
    int lw = std::max(1, (int)(logo->width * k)), lh = std::max(1, (int)(logo->height * k));
    int ox = bx + (bw - lw) / 2, oy = by + (bh - lh) / 2;
    for (int y = 0; y < lh; ++y) for (int x = 0; x < lw; ++x) {
        int sx0 = x * logo->width / lw, sx1 = std::max(sx0 + 1, (x + 1) * logo->width / lw);
        int sy0 = y * logo->height / lh, sy1 = std::max(sy0 + 1, (y + 1) * logo->height / lh);
        float a = 0, r = 0, g = 0, b = 0; int n = 0;
        for (int yy = sy0; yy < sy1; ++yy) for (int xx = sx0; xx < sx1; ++xx) {
            uint32_t p = logo->pixels[(size_t)yy * logo->width + xx];
            a += (p >> 24) & 255; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; ++n;
        }
        Over(px[(size_t)(oy + y) * W + ox + x], a / n / 255.f, r / n / 255.f, g / n / 255.f, b / n / 255.f);
    }
}
}

std::shared_ptr<OverlayImage> RenderDisplay(const DisplayState& st, int W, int H, const OverlayImage* logo) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, 0xFF000000u);   // opaque black = screen off
    if (!st.powered) return img;
    auto& px = img->pixels;
    const float s = H / 720.f;
    auto S = [&](float v) { return (int)std::lround(v * s); };
    // background: subtle vertical gradient
    for (int y = 0; y < H; ++y) {
        float t = (float)y / H, c0 = 0.075f + 0.04f * t;
        uint32_t v = Pack(1.f, c0, c0, c0 + 0.012f);
        std::fill(px.begin() + (size_t)y * W, px.begin() + (size_t)(y + 1) * W, v);
    }
    const float OR = 1.f, OG = 0.682f, OB = 0.f;   // accent orange #FFAE00
    HFONT fSmall = MakeFont(S(26), FW_NORMAL), fHead = MakeFont(S(30), FW_NORMAL), fTitle = MakeFont(S(64), FW_BOLD),
          fCountry = MakeFont(S(34), FW_NORMAL), fFoot = MakeFont(S(28), FW_SEMIBOLD);
    Gdi mask(W, H);
    // header
    const int headH = S(64);
    FillRect(px, W, H, 0, 0, W, headH, 1.f, 0.05f, 0.05f, 0.06f);
    FillRect(px, W, H, 0, headH - S(3), W, headH, 1.f, OR, OG, OB);
    DrawText(mask, px, fHead, L"Realistic Radio", RECT{S(28), 0, W / 2, headH - S(3)}, 0.95f, 0.95f, 0.95f);
    std::wstring vol = st.muted ? std::wstring(L"Stumm") : std::to_wstring(st.volume) + L" %";
    DrawText(mask, px, fSmall, vol, RECT{W / 2, 0, W - S(28), headH - S(3)}, 0.85f, 0.85f, 0.88f, DT_RIGHT);
    if (st.volume >= 0 && !st.muted) {   // volume bar under header text
        int bw = S(160), bx = W - S(28) - bw - S(90), by = headH / 2 - S(2);
        FillRect(px, W, H, bx, by, bx + bw, by + S(4), 1.f, 0.25f, 0.25f, 0.28f);
        FillRect(px, W, H, bx, by, bx + bw * std::clamp(st.volume, 0, 100) / 100, by + S(4), 1.f, OR, OG, OB);
    }
    // footer: previous | favourite | next
    const int footH = S(96), footY = H - footH;
    FillRect(px, W, H, 0, footY, W, H, 1.f, 0.05f, 0.05f, 0.06f);
    int third = W / 3;
    if (!st.prevName.empty()) DrawText(mask, px, fFoot, L"\u25C0  " + st.prevName, RECT{S(28), footY, third - S(10), H}, 0.8f, 0.8f, 0.82f);
    DrawText(mask, px, fFoot, st.footer, RECT{third, footY, 2 * third, H}, st.favourite ? 0.96f : 0.6f, st.favourite ? 0.33f : 0.6f, st.favourite ? 0.33f : 0.62f, DT_CENTER);
    if (!st.nextName.empty()) DrawText(mask, px, fFoot, st.nextName + L"  \u25B6", RECT{2 * third + S(10), footY, W - S(28), H}, 0.8f, 0.8f, 0.82f, DT_RIGHT);
    // main area
    const int top = headH + S(30), bottom = footY - S(30), mainH = bottom - top;
    int logoBox = logo ? std::min(mainH, W * 36 / 100) : 0;
    int x0 = S(40);
    if (logoBox) {
        // light card behind the logo so dark logos stay readable
        int cx = x0, cy = top + (mainH - logoBox) / 2;
        for (int y = 0; y < logoBox; ++y) for (int x = 0; x < logoBox; ++x) {
            float cov = RoundedCoverage(x, y, logoBox, logoBox, (float)S(22));
            if (cov > 0) Over(px[(size_t)(cy + y) * W + cx + x], cov, 0.93f * cov, 0.93f * cov, 0.94f * cov);
        }
        BlitLogo(px, W, logo, cx + S(18), cy + S(18), logoBox - S(36), logoBox - S(36));
        x0 += logoBox + S(44);
    }
    const int tx1 = W - S(40);
    int y = top + mainH / 2 - S(110);
    DrawText(mask, px, fHead, st.headline, RECT{x0, y, tx1, y + S(44)}, 0.75f, 0.75f, 0.78f); y += S(48);
    DrawText(mask, px, fTitle, st.title, RECT{x0, y, tx1, y + S(84)}, OR, OG, OB); y += S(92);
    int barsW = st.bars >= 0 ? S(5 * 14 + 4 * 6) : 0;
    DrawText(mask, px, fCountry, st.country, RECT{x0, y, tx1 - barsW - S(16), y + S(48)}, 0.92f, 0.92f, 0.94f);
    if (st.bars >= 0) {
        int bx = tx1 - barsW, base = y + S(44);
        for (int i = 0; i < 5; ++i) {
            int bh = S(12 + i * 7), xs = bx + i * S(20);
            if (i < st.bars) FillRect(px, W, H, xs, base - bh, xs + S(14), base, 1.f, OR, OG, OB);
            else FillRect(px, W, H, xs, base - bh, xs + S(14), base, 1.f, 0.22f, 0.22f, 0.25f);
        }
    }
    y += S(56);
    if (!st.status.empty()) DrawText(mask, px, fCountry, st.status, RECT{x0, y, tx1, y + S(46)}, 0.95f, 0.45f, 0.35f);
    DeleteObject(fSmall); DeleteObject(fHead); DeleteObject(fTitle); DeleteObject(fCountry); DeleteObject(fFoot);
    return img;
}

// ------------------------------------------------------------------------------------------------
// Car-radio style dot-matrix display (character LCD look: 5x7 dot cells with small gaps between them)
#include "font5x7.h"
namespace {
// Unicode -> index in the CP437 5x7 font, or -1
int Cp437Index(wchar_t c) {
    if (c >= 0x20 && c < 0x7F) return (int)c;
    switch (c) {
    case L'Ç': return 0x80; case L'ü': return 0x81; case L'é': return 0x82; case L'â': return 0x83;
    case L'ä': return 0x84; case L'à': return 0x85; case L'å': return 0x86; case L'ç': return 0x87;
    case L'ê': return 0x88; case L'ë': return 0x89; case L'è': return 0x8A; case L'ï': return 0x8B;
    case L'î': return 0x8C; case L'ì': return 0x8D; case L'Ä': return 0x8E; case L'Å': return 0x8F;
    case L'É': return 0x90; case L'æ': return 0x91; case L'Æ': return 0x92; case L'ô': return 0x93;
    case L'ö': return 0x94; case L'ò': return 0x95; case L'û': return 0x96; case L'ù': return 0x97;
    case L'ÿ': return 0x98; case L'Ö': return 0x99; case L'Ü': return 0x9A; case L'¢': return 0x9B;
    case L'£': return 0x9C; case L'¥': return 0x9D; case L'ƒ': return 0x9F; case L'á': return 0xA0;
    case L'í': return 0xA1; case L'ó': return 0xA2; case L'ú': return 0xA3; case L'ñ': return 0xA4;
    case L'Ñ': return 0xA5; case L'ª': return 0xA6; case L'º': return 0xA7; case L'¿': return 0xA8;
    case L'¬': return 0xAA; case L'½': return 0xAB; case L'¼': return 0xAC; case L'¡': return 0xAD;
    case L'«': return 0xAE; case L'»': return 0xAF; case L'ß': return 0xE1; case L'µ': return 0xE6;
    case L'±': return 0xF1; case L'÷': return 0xF6; case L'°': return 0xF8; case L'·': return 0xFA;
    case L'²': return 0xFD; case L'♥': return 0x03; case L'•': return 0x07;
    case L'–': case L'—': return '-'; case L'‘': case L'’': return '\''; case L'“': case L'”': case L'„': return '"';
    case L'…': return '.';
    }
    return -1;
}
// letters with accents the font lacks -> base letter (e.g. A-grave -> A, S-caron -> S)
int FontIndexFor(wchar_t c) {
    int i = Cp437Index(c);
    if (i >= 0) return i;
    wchar_t buf[8] = {0};
    if (NormalizeString(NormalizationD, &c, 1, buf, 8) > 0) {
        i = Cp437Index(buf[0]);
        if (i >= 0x20) return i;
    }
    if (c == L'Ø' || c == L'ø') return 'O';
    if (c == L'Ł' || c == L'ł') return 'L';
    if (c == L'Đ' || c == L'đ') return 'D';
    return -1;
}
}

std::shared_ptr<OverlayImage> RenderRadioLcd(const std::wstring& text, int scroll, int columns, int bars, bool favourite,
                                             int screenH, double scale, uint32_t color) {
    const float s = (float)std::clamp(screenH / 1080.0 * scale, 0.3, 4.0);
    auto S = [&](float v) { return std::max(1, (int)std::lround(v * s)); };
    const float CR = ((color >> 16) & 255) / 255.f, CG = ((color >> 8) & 255) / 255.f, CB = (color & 255) / 255.f;
    const int CW = 5, CH = 8;   // dots per character cell (7 rows + descender row)

    // text for the visible window: centred if it fits, otherwise a wrapping marquee
    std::wstring visible;
    if ((int)text.size() <= columns) visible = std::wstring((columns - (int)text.size()) / 2, L' ') + text;
    else { std::wstring loop = text + L"     "; for (int i = 0; i < columns; ++i) visible += loop[(scroll + i) % (int)loop.size()]; }
    visible.resize(columns, L' ');

    // layout: `columns` text cells + 1 empty cell + 1 indicator cell
    const int totalCells = columns + 2;
    const int pitch = S(4.2f), dot = std::max(1, pitch - std::max(1, (int)std::lround(pitch * 0.22f)));
    const int cellGap = std::max(2, pitch - dot + S(2.5f));   // visible gap between character cells
    const int cellW = CW * pitch - (pitch - dot) + cellGap, cellH = CH * pitch;
    const int padX = S(16), padY = S(11), bezel = S(7);
    const int lcdW = totalCells * cellW - cellGap + 2 * padX, lcdH = cellH - (pitch - dot) + 2 * padY;
    const int W = lcdW + 2 * bezel, H = lcdH + 2 * bezel;
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = screenH; img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;

    // bezel and tinted glass
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float cov = RoundedCoverage(x, y, W, H, (float)S(12));
        if (cov <= 0) continue;
        float sh = 0.035f + 0.03f * (1.f - (float)y / H);
        px[(size_t)y * W + x] = Pack(0.94f * cov, sh * 0.94f * cov, sh * 0.94f * cov, (sh + 0.005f) * 0.94f * cov);
    }
    for (int y = 0; y < lcdH; ++y) for (int x = 0; x < lcdW; ++x) {
        float cov = RoundedCoverage(x, y, lcdW, lcdH, (float)S(5));
        if (cov <= 0) continue;
        float g = 0.045f + 0.02f * (1.f - (float)y / lcdH);
        Over(px[(size_t)(y + bezel) * W + x + bezel], cov, (g + CR * 0.04f) * cov, (g + CG * 0.04f) * cov, (g + CB * 0.04f) * cov);
    }
    auto putDot = [&](int cell, int col, int row, bool lit) {
        int x0 = bezel + padX + cell * cellW + col * pitch, y0 = bezel + padY + row * pitch;
        float a = lit ? 1.f : 0.055f;
        for (int y = 0; y < dot; ++y) for (int x = 0; x < dot; ++x) {
            float cov = dot >= 3 ? RoundedCoverage(x, y, dot, dot, dot * 0.3f) : 1.f;
            if (cov > 0) Over(px[(size_t)(y0 + y) * W + x0 + x], a * cov, CR * a * cov, CG * a * cov, CB * a * cov);
        }
        if (lit) for (int y = -1; y <= dot; ++y) for (int x = -1; x <= dot; ++x) {   // soft glow
            if (y >= 0 && y < dot && x >= 0 && x < dot) continue;
            int X = x0 + x, Y = y0 + y; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            Over(px[(size_t)Y * W + X], 0.16f, CR * 0.16f, CG * 0.16f, CB * 0.16f);
        }
    };

    // GDI fallback for characters the dot font does not have (Cyrillic, Greek, ...): rendered into one cell
    HFONT fb = nullptr; Gdi* fbDc = nullptr;
    for (int c = 0; c < totalCells; ++c) {
        bool on[CW][CH] = {};
        if (c < columns) {
            wchar_t ch = visible[c];
            int fi = FontIndexFor(ch);
            if (fi >= 0) {
                for (int x = 0; x < CW; ++x) { unsigned char colBits = kFont5x7[fi * 5 + x]; for (int y = 0; y < CH; ++y) on[x][y] = (colBits >> y) & 1; }
            } else if (ch != L' ') {
                if (!fbDc) {
                    fbDc = new Gdi(CW, CH);
                    fb = CreateFontW(-8, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     NONANTIALIASED_QUALITY, FIXED_PITCH, L"Lucida Console");
                }
                fbDc->Clear();
                HGDIOBJ o = SelectObject(fbDc->dc, fb);
                RECT r{0, 0, CW, CH}; DrawTextW(fbDc->dc, &ch, 1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_TOP | DT_NOCLIP);
                SelectObject(fbDc->dc, o); GdiFlush();
                for (int x = 0; x < CW; ++x) for (int y = 0; y < CH; ++y) { uint32_t p = fbDc->bits[y * CW + x]; on[x][y] = ((p >> 8) & 255) > 100; }
            }
        } else if (c == columns + 1) {   // indicator cell
            if (bars >= 0) {
                // 5 bars, 1 dot wide, rising: heights 1..7 rows (bottom row 6)
                static const int hgt[5] = {1, 2, 4, 5, 7};
                for (int b = 0; b < 5 && b < bars; ++b) for (int y = 7 - hgt[b]; y < 7; ++y) on[b][y] = true;
            } else if (favourite) {
                for (int x = 0; x < CW; ++x) { unsigned char colBits = kFont5x7[0x03 * 5 + x]; for (int y = 0; y < CH; ++y) on[x][y] = (colBits >> y) & 1; }
            }
        }
        for (int x = 0; x < CW; ++x) for (int y = 0; y < CH; ++y) putDot(c, x, y, on[x][y]);
    }
    if (fbDc) { delete fbDc; DeleteObject(fb); }
    return img;
}

// ------------------------------------------------------------------------------------------------
// In-cab screen card (1.1.0 design)
namespace {
struct Canvas {
    std::vector<uint32_t>& px; int W, H;
    void Blend(int x, int y, float cov, float r, float g, float b, float a = 1.f) {
        if (x < 0 || y < 0 || x >= W || y >= H || cov <= 0) return;
        float k = std::min(1.f, cov) * a;
        Over(px[(size_t)y * W + x], k, r * k, g * k, b * k);
    }
    void Round(int x, int y, int w, int h, float rad, float r, float g, float b, float a = 1.f) {
        if (w <= 0 || h <= 0) return;
        rad = std::min({rad, w / 2.f, h / 2.f});
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) Blend(x + xx, y + yy, RoundedCoverage(xx, yy, w, h, rad), r, g, b, a);
    }
    void Circle(float cx, float cy, float rad, float r, float g, float b, float a = 1.f) {
        for (int y = (int)(cy - rad - 1); y <= (int)(cy + rad + 1); ++y) for (int x = (int)(cx - rad - 1); x <= (int)(cx + rad + 1); ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            Blend(x, y, std::clamp(rad - d + 0.5f, 0.f, 1.f), r, g, b, a);
        }
    }
    // ring segment between angles a0..a1 (radians, 0 = right, counter-clockwise on screen = negative y)
    void Arc(float cx, float cy, float rad, float th, float a0, float a1, float r, float g, float b) {
        int R = (int)(rad + th + 2);
        for (int y = (int)cy - R; y <= (int)cy + R; ++y) for (int x = (int)cx - R; x <= (int)cx + R; ++x) {
            float dx = x + 0.5f - cx, dy = cy - (y + 0.5f);
            float ang = std::atan2(dy, dx);
            if (ang < a0 || ang > a1) continue;
            float d = std::hypot(dx, dy);
            Blend(x, y, std::clamp(th / 2 - std::abs(d - rad) + 0.5f, 0.f, 1.f), r, g, b);
        }
    }
    void Line(float x0, float y0, float x1, float y1, float th, float r, float g, float b) {   // capsule
        int minx = (int)(std::min(x0, x1) - th), maxx = (int)(std::max(x0, x1) + th), miny = (int)(std::min(y0, y1) - th), maxy = (int)(std::max(y0, y1) + th);
        float vx = x1 - x0, vy = y1 - y0, len2 = std::max(1e-6f, vx * vx + vy * vy);
        for (int y = miny; y <= maxy; ++y) for (int x = minx; x <= maxx; ++x) {
            float px_ = x + 0.5f - x0, py_ = y + 0.5f - y0;
            float t = std::clamp((px_ * vx + py_ * vy) / len2, 0.f, 1.f);
            float d = std::hypot(px_ - t * vx, py_ - t * vy);
            Blend(x, y, std::clamp(th / 2 - d + 0.5f, 0.f, 1.f), r, g, b);
        }
    }
    void Poly(const std::vector<std::pair<float, float>>& p, float r, float g, float b, float a = 1.f) {   // 4x4 supersampled
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (auto& q : p) { minx = std::min(minx, q.first); maxx = std::max(maxx, q.first); miny = std::min(miny, q.second); maxy = std::max(maxy, q.second); }
        auto inside = [&](float x, float y) {
            bool in = false;
            for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
                if ((p[i].second > y) != (p[j].second > y) && x < (p[j].first - p[i].first) * (y - p[i].second) / (p[j].second - p[i].second) + p[i].first) in = !in;
            return in;
        };
        for (int y = (int)miny; y <= (int)maxy; ++y) for (int x = (int)minx; x <= (int)maxx; ++x) {
            int n = 0;
            for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) n += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
            Blend(x, y, n / 16.f, r, g, b, a);
        }
    }
    void Glow(float cx, float cy, float rad, float r, float g, float b, float a) {
        for (int y = std::max(0, (int)(cy - rad)); y < std::min(H, (int)(cy + rad)); ++y)
            for (int x = std::max(0, (int)(cx - rad)); x < std::min(W, (int)(cx + rad)); ++x) {
                float d = std::hypot(x - cx, y - cy) / rad;
                if (d < 1) Blend(x, y, (1 - d) * (1 - d), r, g, b, a);
            }
    }
};

std::wstring Upper(std::wstring s) { if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size()); return s; }
std::wstring Clock(int s) {
    if (s < 0) return L"--:--";
    wchar_t b[32];
    if (s >= 3600) swprintf(b, 32, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60); else swprintf(b, 32, L"%d:%02d", s / 60, s % 60);
    return b;
}
std::wstring Initials(const std::wstring& name) {
    std::wstring r;
    bool start = true;
    for (wchar_t c : name) {
        if (iswalnum(c)) { if (start && r.size() < 2) r.push_back(c); start = false; } else start = true;
    }
    return Upper(r.empty() ? L"♪" : r);
}
// text that wraps onto at most two lines (DT_EDITCONTROL hides a partly visible third line)
void DrawText2(Gdi& mask, std::vector<uint32_t>& out, HFONT f, const std::wstring& s, RECT r, float R, float G, float B) {
    if (s.empty()) return;
    RECT c = ClipRect(mask, r);
    if (c.right <= c.left || c.bottom <= c.top) return;
    ClearMask(mask, c);
    HGDIOBJ o = SelectObject(mask.dc, f);
    DrawTextW(mask.dc, s.c_str(), (int)s.size(), &r, DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(mask.dc, o); GdiFlush();
    BlendMask(mask, out, c, R, G, B);
}
} // namespace

std::shared_ptr<OverlayImage> ShrinkImage(const std::shared_ptr<OverlayImage>& src, int maxSide) {
    if (!src || (src->width <= maxSide && src->height <= maxSide)) return src;
    float k = std::min((float)maxSide / src->width, (float)maxSide / src->height);
    auto img = std::make_shared<OverlayImage>();
    img->width = std::max(1, (int)(src->width * k)); img->height = std::max(1, (int)(src->height * k));
    img->pixels.assign((size_t)img->width * img->height, 0u);
    BlitLogo(img->pixels, img->width, src.get(), 0, 0, img->width, img->height);
    return img;
}

// ------------------------------------------------------------------------------------------------
// Designs of the in-cab card (screen.design)
namespace {
std::shared_ptr<CardTheme> MakeTheme(const char* layout, std::initializer_list<float> c, float glow, const wchar_t* font) {
    auto t = std::make_shared<CardTheme>();
    t->layout = layout; t->glow = glow; t->font = font;
    float* dst[] = {t->bgTop, t->bgBottom, t->accent, t->chipText, t->text, t->sub, t->muted, t->tile, t->inactive, t->art};
    auto it = c.begin();
    for (float* d : dst) for (int k = 0; k < 3 && it != c.end(); ++k) d[k] = *it++;
    return t;
}
struct BuiltinThemes {
    std::shared_ptr<const CardTheme> modern, ocean, sport, light, retro, classic, smart, presets;
    BuiltinThemes() {
        // modern (default): dark blue-grey, amber
        modern = MakeTheme("standard", {0.105f, 0.115f, 0.14f, 0.045f, 0.05f, 0.065f, 1.f, 0.682f, 0.f, 0.07f, 0.07f, 0.09f, 0.97f, 0.97f, 0.98f, 0.86f, 0.87f, 0.90f,
                                        0.60f, 0.63f, 0.69f, 0.17f, 0.18f, 0.22f, 0.26f, 0.27f, 0.31f, 0.80f, 0.82f, 0.86f}, 0.10f, L"Segoe UI");
        // ocean: navy, light blue
        ocean = MakeTheme("standard", {0.05f, 0.10f, 0.18f, 0.02f, 0.04f, 0.09f, 0.25f, 0.72f, 1.f, 0.02f, 0.05f, 0.10f, 0.97f, 0.98f, 1.f, 0.80f, 0.87f, 0.95f,
                                       0.52f, 0.63f, 0.76f, 0.08f, 0.15f, 0.26f, 0.15f, 0.25f, 0.38f, 0.78f, 0.86f, 0.95f}, 0.12f, L"Segoe UI");
        // sport: black-red
        sport = MakeTheme("standard", {0.12f, 0.05f, 0.06f, 0.04f, 0.02f, 0.02f, 0.95f, 0.22f, 0.20f, 1.f, 1.f, 1.f, 0.98f, 0.97f, 0.97f, 0.90f, 0.83f, 0.83f,
                                       0.66f, 0.56f, 0.56f, 0.20f, 0.10f, 0.11f, 0.33f, 0.19f, 0.20f, 0.86f, 0.82f, 0.82f}, 0.10f, L"Segoe UI");
        // light: white, blue
        light = MakeTheme("standard", {0.96f, 0.97f, 0.98f, 0.86f, 0.88f, 0.91f, 0.f, 0.46f, 0.86f, 1.f, 1.f, 1.f, 0.08f, 0.09f, 0.11f, 0.24f, 0.26f, 0.30f,
                                       0.42f, 0.45f, 0.50f, 0.80f, 0.83f, 0.88f, 0.70f, 0.73f, 0.79f, 0.36f, 0.39f, 0.45f}, 0.f, L"Segoe UI");
        // retro: green LCD on black, monospace
        retro = MakeTheme("standard", {0.02f, 0.035f, 0.02f, 0.01f, 0.02f, 0.01f, 0.35f, 1.f, 0.45f, 0.01f, 0.05f, 0.02f, 0.55f, 1.f, 0.60f, 0.42f, 0.86f, 0.47f,
                                       0.26f, 0.58f, 0.31f, 0.04f, 0.10f, 0.05f, 0.08f, 0.22f, 0.10f, 0.42f, 0.86f, 0.47f}, 0.05f, L"Consolas");
        // classic truck radio: dark plastic faceplate, red source button, green-amber LCD with dot-matrix text
        classic = MakeTheme("classic", {0.17f, 0.175f, 0.185f, 0.085f, 0.09f, 0.095f, 0.80f, 0.15f, 0.13f, 0.98f, 0.96f, 0.94f, 0.88f, 0.88f, 0.89f, 0.70f, 0.71f, 0.73f,
                                        0.50f, 0.51f, 0.53f, 0.21f, 0.215f, 0.225f, 0.11f, 0.115f, 0.12f, 0.75f, 0.76f, 0.78f}, 0.f, L"Segoe UI");
        // smart dashboard: near black, amber, icon bar on the left
        smart = MakeTheme("smart", {0.095f, 0.10f, 0.115f, 0.045f, 0.048f, 0.055f, 1.f, 0.70f, 0.f, 0.08f, 0.07f, 0.04f, 0.97f, 0.97f, 0.98f, 0.80f, 0.81f, 0.84f,
                                    0.58f, 0.60f, 0.65f, 0.13f, 0.135f, 0.15f, 0.25f, 0.26f, 0.29f, 0.78f, 0.80f, 0.84f}, 0.06f, L"Segoe UI");
        // station list (infotainment): black, white text, logo tiles of the station buttons, light blue marks
        presets = MakeTheme("presets", {0.06f, 0.065f, 0.075f, 0.015f, 0.017f, 0.02f, 0.30f, 0.62f, 1.f, 0.02f, 0.03f, 0.05f, 0.97f, 0.97f, 0.98f, 0.84f, 0.86f, 0.90f,
                                        0.55f, 0.58f, 0.64f, 0.16f, 0.17f, 0.19f, 0.24f, 0.25f, 0.28f, 0.78f, 0.80f, 0.84f}, 0.f, L"Segoe UI");
    }
};
const BuiltinThemes& Builtins() { static BuiltinThemes b; return b; }
std::shared_ptr<const CardTheme> ThemeFor(const std::string& d) { return CardThemeFor(d); }
} // namespace

std::shared_ptr<const CardTheme> CardThemeFor(const std::string& d) {
    const BuiltinThemes& b = Builtins();
    if (d == "ocean") return b.ocean;
    if (d == "sport") return b.sport;
    if (d == "light") return b.light;
    if (d == "retro") return b.retro;
    if (d == "classic") return b.classic;
    if (d == "smart") return b.smart;
    if (d == "presets") return b.presets;
    if (d.rfind("style:", 0) == 0) if (auto t = CustomCardTheme(d)) return t;
    return b.modern;
}

// Logo / cover / picture of the medium on its tile (T x T at tileX, tileY)
static void DrawArtTile(Canvas& cv, Gdi& mask, std::vector<uint32_t>& px, int W, const CardTheme& th, const OverlayInfo& info, const OverlayImage* logo,
                        int tileX, int tileY, int T, float u) {
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const float AR = th.accent[0], AG = th.accent[1], AB = th.accent[2];
    const float IR = th.inactive[0], IG = th.inactive[1], IB = th.inactive[2];
    if (logo && !info.media) {   // station logo on a light tile (dark logos stay visible)
        cv.Round(tileX, tileY, T, T, (float)U(18), th.logoTile[0], th.logoTile[1], th.logoTile[2]);
        BlitLogo(px, W, logo, tileX + U(16), tileY + U(16), T - U(32), T - U(32));
    } else if (logo) {           // album cover
        cv.Round(tileX, tileY, T, T, (float)U(18), th.tile[0], th.tile[1], th.tile[2]);
        BlitLogo(px, W, logo, tileX + U(6), tileY + U(6), T - U(12), T - U(12));
    } else {
        cv.Round(tileX, tileY, T, T, (float)U(18), th.tile[0], th.tile[1], th.tile[2]);
        if (th.glow > 0) cv.Glow(tileX + T * 0.3f, tileY + T * 0.25f, T * 0.9f, AR, AG, AB, th.glow * 1.2f);
        const float cx = tileX + T / 2.f, cy = tileY + T / 2.f, L = th.art[0], LG = th.art[1], LB = th.art[2], DR = th.tile[0], DG = th.tile[1], DB = th.tile[2];
        const std::string& mt = info.mediaType;
        auto R = [&](float x0, float y0, float x1, float y1, float rad, float r, float g, float b) {
            cv.Round((int)(cx + x0 * T), (int)(cy + y0 * T), (int)((x1 - x0) * T), (int)((y1 - y0) * T), rad * T, r, g, b); };
        if (info.media && mt == "usb") {            // USB stick
            R(-0.11f, -0.36f, 0.11f, -0.14f, 0.02f, 0.62f, 0.64f, 0.68f);
            R(-0.06f, -0.30f, -0.02f, -0.24f, 0.005f, DR, DG, DB); R(0.02f, -0.30f, 0.06f, -0.24f, 0.005f, DR, DG, DB);
            R(-0.17f, -0.15f, 0.17f, 0.36f, 0.06f, L, LG, LB);
            R(-0.17f, 0.10f, 0.17f, 0.17f, 0.0f, AR, AG, AB);
        } else if (info.media && mt == "sd") {      // SD card
            float x0 = cx - 0.24f * T, x1 = cx + 0.24f * T, y0 = cy - 0.32f * T, y1 = cy + 0.32f * T, cut = 0.13f * T;
            cv.Poly({{x0, y0}, {x1 - cut, y0}, {x1, y0 + cut}, {x1, y1}, {x0, y1}}, L, LG, LB);
            for (int k = 0; k < 5; ++k) R(-0.19f + k * 0.07f, -0.29f, -0.15f + k * 0.07f, -0.16f, 0.008f, AR, AG, AB);
            R(-0.17f, 0.02f, 0.17f, 0.25f, 0.03f, DR, DG, DB);
        } else if (info.media && mt == "cassette") {  // cassette
            R(-0.38f, -0.24f, 0.38f, 0.24f, 0.05f, L, LG, LB);
            R(-0.30f, -0.17f, 0.30f, 0.06f, 0.03f, AR, AG, AB);
            R(-0.18f, -0.08f, 0.18f, 0.03f, 0.03f, DR, DG, DB);
            for (float sx : {-0.10f, 0.10f}) { cv.Circle(cx + sx * T, cy - 0.025f * T, 0.045f * T, 1.f, 1.f, 1.f); cv.Circle(cx + sx * T, cy - 0.025f * T, 0.018f * T, DR, DG, DB); }
            cv.Poly({{cx - 0.22f * T, cy + 0.24f * T}, {cx - 0.16f * T, cy + 0.13f * T}, {cx + 0.16f * T, cy + 0.13f * T}, {cx + 0.22f * T, cy + 0.24f * T}}, 0.62f, 0.64f, 0.68f);
        } else if (info.media && mt == "bluetooth") {  // bluetooth rune on a round badge
            cv.Circle(cx, cy, 0.30f * T, 0.20f, 0.45f, 0.95f);
            float w = 0.035f * T, t = -0.17f * T, b = 0.17f * T, rx = 0.09f * T, lx = -0.09f * T;
            cv.Line(cx, cy + t, cx, cy + b, w, 1.f, 1.f, 1.f);
            cv.Line(cx, cy + t, cx + rx, cy + t / 2, w, 1.f, 1.f, 1.f); cv.Line(cx + rx, cy + t / 2, cx + lx, cy + b / 2, w, 1.f, 1.f, 1.f);
            cv.Line(cx, cy + b, cx + rx, cy + b / 2, w, 1.f, 1.f, 1.f); cv.Line(cx + rx, cy + b / 2, cx + lx, cy + t / 2, w, 1.f, 1.f, 1.f);
        } else if (info.media && mt == "aux") {        // 3.5 mm plug
            R(-0.035f, -0.36f, 0.035f, -0.04f, 0.03f, 0.75f, 0.77f, 0.80f);
            R(-0.04f, -0.25f, 0.04f, -0.23f, 0.0f, DR, DG, DB); R(-0.04f, -0.16f, 0.04f, -0.14f, 0.0f, DR, DG, DB);
            R(-0.09f, -0.04f, 0.09f, 0.30f, 0.04f, AR, AG, AB);
            R(-0.025f, 0.30f, 0.025f, 0.40f, 0.01f, L, LG, LB);
        } else if (info.media && mt == "none") {       // no medium: empty slot with a question mark
            R(-0.30f, -0.07f, 0.30f, 0.07f, 0.035f, IR * 0.5f, IG * 0.5f, IB * 0.5f);
            HFONT fQ = MakeFont((int)(T * 0.30f), FW_BOLD);
            DrawText(mask, px, fQ, L"?", RECT{tileX, tileY + (int)(T * 0.06f), tileX + T, tileY + (int)(T * 0.44f)}, AR, AG, AB, DT_CENTER);
            DeleteObject(fQ);
        } else if (info.media) {   // disc (CD)
            float r = T * 0.36f;
            cv.Circle(cx, cy, r, L, LG, LB);
            cv.Arc(cx, cy, r * 0.72f, r * 0.10f, 0.35f, 1.25f, 1.f, 1.f, 1.f);
            cv.Circle(cx, cy, r * 0.42f, AR, AG, AB);
            cv.Circle(cx, cy, r * 0.12f, DR, DG, DB);
        } else {
            HFONT fIni = MakeFont((int)(T * 0.36f), FW_BOLD);
            DrawText(mask, px, fIni, Initials(info.title), RECT{tileX, tileY, tileX + T, tileY + T}, AR, AG, AB, DT_CENTER);
            DeleteObject(fIni);
        }
    }

}

// Title (one or two lines), subtitle, live song title (accent), detail - centred vertically in areaY .. areaY + areaH
static void DrawTextColumn(Gdi& mask, std::vector<uint32_t>& px, const CardTheme& th, const OverlayInfo& info, int tx0, int tx1, int areaY, int areaH,
                           float u, int H, SongLine* songOut, float k = 1.f) {
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u * k)); };
    HDC mdc = mask.dc;
    const float AR = th.accent[0], AG = th.accent[1], AB = th.accent[2];
    const float TR = th.text[0], TG = th.text[1], TB = th.text[2];
    const float MR = th.muted[0], MG = th.muted[1], MB = th.muted[2];
    const int tileY = areaY, T = areaH;
    const int tw = std::max(10, tx1 - tx0);
    HFONT fTitle = MakeFont(U(40), FW_SEMIBOLD), fTitle2 = MakeFont(U(31), FW_SEMIBOLD), fSub = MakeFont(U(24), FW_NORMAL), fDet = MakeFont(U(19), FW_NORMAL);
    bool twoLines = Measure(mdc, fTitle, info.title).cx > tw;
    HFONT ft = twoLines ? fTitle2 : fTitle;
    int titleH = twoLines ? U(80) : U(52);
    if (twoLines) {   // check whether it really needs two lines at the smaller size
        RECT r{0, 0, tw, 1000}; HGDIOBJ o = SelectObject(mdc, ft);
        DrawTextW(mdc, info.title.c_str(), (int)info.title.size(), &r, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        SelectObject(mdc, o);
        titleH = std::min((int)r.bottom, U(80)) + U(4);
    }
    const bool song = !info.media && !info.song.empty();
    int subH = info.subtitle.empty() ? 0 : U(34), detH = info.detail.empty() ? 0 : U(28), songH = song ? U(32) : 0;
    int blockH = titleH + subH + songH + detH;
    int y = tileY + (T - blockH) / 2;
    if (twoLines) DrawText2(mask, px, ft, info.title, RECT{tx0, y, tx1, y + titleH}, TR, TG, TB);
    else DrawText(mask, px, ft, info.title, RECT{tx0, y, tx1, y + titleH}, TR, TG, TB);
    y += titleH;
    DrawText(mask, px, fSub, info.subtitle, RECT{tx0, y, tx1, y + subH}, th.sub[0], th.sub[1], th.sub[2]); y += subH;
    if (song) {   // live song title in the accent colour
        HFONT fSong = MakeFont(U(23), FW_SEMIBOLD);
        std::wstring text = L"\u266A  " + info.song;
        if (songOut) {   // as a strip: the caller lets it scroll
            int sw = Measure(mdc, fSong, text).cx + U(4);
            auto strip = std::make_shared<OverlayImage>();
            strip->width = std::max(1, sw); strip->height = songH; strip->renderedForScreenHeight = H;
            strip->pixels.assign((size_t)strip->width * songH, 0u);
            Gdi sm(strip->width, songH);
            DrawText(sm, strip->pixels, fSong, text, RECT{0, 0, strip->width, songH}, AR, AG, AB);
            songOut->x = tx0; songOut->y = y; songOut->w = tw; songOut->h = songH; songOut->strip = strip;
        } else DrawText(mask, px, fSong, text, RECT{tx0, y, tx1, y + songH}, AR, AG, AB);
        DeleteObject(fSong);
        y += songH;
    }
    DrawText(mask, px, fDet, info.detail, RECT{tx0, y, tx1, y + detH}, MR, MG, MB);
    DeleteObject(fTitle); DeleteObject(fTitle2); DeleteObject(fSub); DeleteObject(fDet);

}

// ------------------------------------------------------------------------------------------------
// 1.5.0 layouts: classic truck radio, smart dashboard, station list - and what they share
namespace {
// background of a card: the design's gradient, then (own style) its picture, filling the area
void CardBackground(std::vector<uint32_t>& px, int W, int H, const CardTheme& th) {
    for (int y = 0; y < H; ++y) {
        float t = (float)y / std::max(1, H - 1);
        uint32_t v = Pack(1.f, th.bgTop[0] + (th.bgBottom[0] - th.bgTop[0]) * t, th.bgTop[1] + (th.bgBottom[1] - th.bgTop[1]) * t,
                          th.bgTop[2] + (th.bgBottom[2] - th.bgTop[2]) * t);
        std::fill(px.begin() + (size_t)y * W, px.begin() + (size_t)(y + 1) * W, v);
    }
    const OverlayImage* bg = th.background.get();
    if (!bg || bg->width <= 0 || bg->height <= 0 || th.backgroundOpacity <= 0.f) return;
    // "cover": scaled so it fills the area, centred, cut at the edges; box filter when shrinking, bilinear when enlarging
    float k = std::max((float)W / bg->width, (float)H / bg->height);
    float ox = (bg->width * k - W) / 2, oy = (bg->height * k - H) / 2, op = std::clamp(th.backgroundOpacity, 0.f, 1.f);
    int box = std::max(1, (int)std::floor(1.f / k));
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float sx = (x + 0.5f + ox) / k - 0.5f, sy = (y + 0.5f + oy) / k - 0.5f;
        float a = 0, r = 0, g = 0, b = 0;
        if (box > 1) {
            int x0 = std::clamp((int)sx - box / 2, 0, bg->width - 1), y0 = std::clamp((int)sy - box / 2, 0, bg->height - 1);
            int x1 = std::min(bg->width, x0 + box), y1 = std::min(bg->height, y0 + box), n = 0;
            for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) {
                uint32_t p = bg->pixels[(size_t)yy * bg->width + xx];
                a += (p >> 24) & 255; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; ++n;
            }
            a /= n; r /= n; g /= n; b /= n;
        } else {
            int x0 = std::clamp((int)std::floor(sx), 0, bg->width - 1), y0 = std::clamp((int)std::floor(sy), 0, bg->height - 1);
            int x1 = std::min(bg->width - 1, x0 + 1), y1 = std::min(bg->height - 1, y0 + 1);
            float fx = std::clamp(sx - x0, 0.f, 1.f), fy = std::clamp(sy - y0, 0.f, 1.f);
            auto at = [&](int xx, int yy, int sh) { return (float)((bg->pixels[(size_t)yy * bg->width + xx] >> sh) & 255); };
            auto bil = [&](int sh) { return (at(x0, y0, sh) * (1 - fx) + at(x1, y0, sh) * fx) * (1 - fy) + (at(x0, y1, sh) * (1 - fx) + at(x1, y1, sh) * fx) * fy; };
            a = bil(24); r = bil(16); g = bil(8); b = bil(0);
        }
        Over(px[(size_t)y * W + x], a / 255.f * op, r / 255.f * op, g / 255.f * op, b / 255.f * op);
    }
}
// rectangle with anti-aliased (fractional) edges
void BoxAA(Canvas& cv, float x0, float y0, float w, float h, float r, float g, float b, float a = 1.f) {
    int xa = (int)std::floor(x0), xb = (int)std::ceil(x0 + w), ya = (int)std::floor(y0), yb = (int)std::ceil(y0 + h);
    for (int y = ya; y < yb; ++y) {
        float cy = std::min(y + 1.f, y0 + h) - std::max((float)y, y0);
        if (cy <= 0) continue;
        for (int x = xa; x < xb; ++x) {
            float cx = std::min(x + 1.f, x0 + w) - std::max((float)x, x0);
            if (cx > 0) cv.Blend(x, y, cx * cy, r, g, b, a);
        }
    }
}
void Signal(Canvas& cv, int x0, int base, int bars, float u, const float on[3], const float off[3]) {   // 5 rising bars
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    int bw = U(6), gap = U(4);
    for (int i = 0; i < 5; ++i) {
        int bh = U(7 + i * 4), xs = x0 + i * (bw + gap);
        const float* c = i < bars ? on : off;
        cv.Round(xs, base - bh, bw, bh, U(1.5f), c[0], c[1], c[2]);
    }
}
int SignalWidth(float u) { auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); }; return 5 * U(6) + 4 * U(4); }

// ---- small line icons (s = size in pixels, centred at cx, cy) ----
enum class Icon { PlayPause, Skip, Back, Home, Gear, List, Radio, Prev, Next };
void DrawIcon(Canvas& cv, Icon ic, float cx, float cy, float s, const float c[3]) {
    const float r = c[0], g = c[1], b = c[2], w = std::max(1.5f, s * 0.11f);
    switch (ic) {
    case Icon::PlayPause:
        cv.Poly({{cx - s * 0.45f, cy - s * 0.38f}, {cx + s * 0.05f, cy}, {cx - s * 0.45f, cy + s * 0.38f}}, r, g, b);
        cv.Round((int)(cx + s * 0.16f), (int)(cy - s * 0.36f), std::max(1, (int)(s * 0.11f)), (int)(s * 0.72f), 1.f, r, g, b);
        cv.Round((int)(cx + s * 0.36f), (int)(cy - s * 0.36f), std::max(1, (int)(s * 0.11f)), (int)(s * 0.72f), 1.f, r, g, b);
        break;
    case Icon::Skip:
        cv.Poly({{cx - s * 0.42f, cy - s * 0.36f}, {cx, cy}, {cx - s * 0.42f, cy + s * 0.36f}}, r, g, b);
        cv.Poly({{cx + s * 0.42f, cy - s * 0.36f}, {cx, cy}, {cx + s * 0.42f, cy + s * 0.36f}}, r, g, b);
        break;
    case Icon::Back: case Icon::Prev:
        cv.Line(cx + s * 0.14f, cy - s * 0.36f, cx - s * 0.20f, cy, w, r, g, b); cv.Line(cx - s * 0.20f, cy, cx + s * 0.14f, cy + s * 0.36f, w, r, g, b);
        break;
    case Icon::Next:
        cv.Line(cx - s * 0.14f, cy - s * 0.36f, cx + s * 0.20f, cy, w, r, g, b); cv.Line(cx + s * 0.20f, cy, cx - s * 0.14f, cy + s * 0.36f, w, r, g, b);
        break;
    case Icon::Home:
        cv.Line(cx - s * 0.42f, cy - s * 0.02f, cx, cy - s * 0.40f, w, r, g, b); cv.Line(cx, cy - s * 0.40f, cx + s * 0.42f, cy - s * 0.02f, w, r, g, b);
        cv.Line(cx - s * 0.30f, cy - s * 0.10f, cx - s * 0.30f, cy + s * 0.38f, w, r, g, b); cv.Line(cx + s * 0.30f, cy - s * 0.10f, cx + s * 0.30f, cy + s * 0.38f, w, r, g, b);
        cv.Line(cx - s * 0.30f, cy + s * 0.38f, cx + s * 0.30f, cy + s * 0.38f, w, r, g, b);
        break;
    case Icon::Gear: {
        float R = s * 0.30f;
        for (int k = 0; k < 8; ++k) {   // teeth
            float a = k * 3.14159265f / 4, tx = cx + std::cos(a) * R * 1.32f, ty = cy + std::sin(a) * R * 1.32f;
            cv.Circle(tx, ty, s * 0.09f, r, g, b);
        }
        cv.Arc(cx, cy, R, w * 1.3f, -3.1416f, 3.1416f, r, g, b);
        cv.Arc(cx, cy, R * 0.38f, w, -3.1416f, 3.1416f, r, g, b);
        break;
    }
    case Icon::List:
        for (int k = -1; k <= 1; ++k) {
            cv.Circle(cx - s * 0.34f, cy + k * s * 0.30f, w * 0.75f, r, g, b);
            cv.Line(cx - s * 0.14f, cy + k * s * 0.30f, cx + s * 0.42f, cy + k * s * 0.30f, w, r, g, b);
        }
        break;
    case Icon::Radio:
        cv.Line(cx - s * 0.42f, cy - s * 0.20f, cx + s * 0.42f, cy - s * 0.20f, w, r, g, b); cv.Line(cx + s * 0.42f, cy - s * 0.20f, cx + s * 0.42f, cy + s * 0.34f, w, r, g, b);
        cv.Line(cx + s * 0.42f, cy + s * 0.34f, cx - s * 0.42f, cy + s * 0.34f, w, r, g, b); cv.Line(cx - s * 0.42f, cy + s * 0.34f, cx - s * 0.42f, cy - s * 0.20f, w, r, g, b);
        cv.Line(cx - s * 0.30f, cy - s * 0.22f, cx + s * 0.22f, cy - s * 0.44f, w, r, g, b);
        cv.Circle(cx + s * 0.16f, cy + s * 0.07f, s * 0.13f, r, g, b);
        cv.Line(cx - s * 0.30f, cy + s * 0.00f, cx - s * 0.06f, cy + s * 0.00f, w * 0.8f, r, g, b);
        cv.Line(cx - s * 0.30f, cy + s * 0.14f, cx - s * 0.06f, cy + s * 0.14f, w * 0.8f, r, g, b);
        break;
    }
}

// ---- dot-matrix LCD text (the 5x7 font of the car-radio display) ----
bool DotFontCovers(const std::wstring& s) {
    for (wchar_t c : s) if (c != L' ' && FontIndexFor(c) < 0) return false;
    return true;
}
float DotWidth(size_t chars, float pitch) { return chars ? (chars * 6 - 1) * pitch : 0.f; }
// lit dots in ink, unlit dots as a faint shadow (ghost > 0) like a real LCD
void DotText(Canvas& cv, const std::wstring& s, float x, float y, float pitch, const float ink[3], float ghost) {
    const float dot = pitch * 0.84f;
    for (size_t i = 0; i < s.size(); ++i) {
        int fi = s[i] == L' ' ? 0x20 : FontIndexFor(s[i]);
        if (fi < 0) fi = '?';
        for (int cx = 0; cx < 5; ++cx) {
            unsigned char bits = kFont5x7[fi * 5 + cx];
            for (int cy = 0; cy < 8; ++cy) {
                bool on = (bits >> cy) & 1;
                if (!on && ghost <= 0) continue;
                BoxAA(cv, x + (i * 6 + cx) * pitch, y + cy * pitch, dot, dot, ink[0], ink[1], ink[2], on ? 0.92f : ghost);
            }
        }
    }
}
// one LCD line: dot font when it has all characters (else a bold monospace font), centred or left-aligned in x0..x1,
// shortened with ".." when even the smallest pitch is too wide. Returns the pitch used.
float LcdLine(Canvas& cv, Gdi& mask, std::vector<uint32_t>& px, std::wstring s, int x0, int x1, float y, float maxPitch, float minPitch,
              const float ink[3], float ghost, bool centre) {
    if (s.empty()) return maxPitch;
    const float w = (float)(x1 - x0);
    float pitch = std::clamp(w / std::max(1.f, DotWidth(s.size(), 1.f)), minPitch, maxPitch);
    if (!DotFontCovers(s)) {
        HFONT f = MakeFont((int)std::lround(pitch * 8.6f), FW_BOLD);
        DrawText(mask, px, f, s, RECT{x0, (int)y, x1, (int)(y + pitch * 8)}, ink[0], ink[1], ink[2], centre ? DT_CENTER : 0);
        DeleteObject(f);
        return pitch;
    }
    if (DotWidth(s.size(), pitch) > w) {
        size_t fit = (size_t)std::max(1.f, (w / pitch + 1) / 6);
        if (fit < s.size()) s = s.substr(0, fit > 2 ? fit - 2 : fit) + (fit > 2 ? L".." : L"");
    }
    float tw = DotWidth(s.size(), pitch);
    DotText(cv, s, centre ? x0 + (w - tw) / 2 : (float)x0, y, pitch, ink, ghost);
    return pitch;
}
// a dot-matrix strip (transparent) for a scrolling LCD line
std::shared_ptr<OverlayImage> LcdStrip(const std::wstring& s, float pitch, const float ink[3], int H) {
    auto strip = std::make_shared<OverlayImage>();
    int h = (int)std::ceil(pitch * 8), w = std::max(1, (int)std::ceil(DotWidth(s.size(), pitch) + pitch));
    strip->width = w; strip->height = h; strip->renderedForScreenHeight = H; strip->pixels.assign((size_t)w * h, 0u);
    Canvas sc{strip->pixels, w, h};
    if (DotFontCovers(s)) DotText(sc, s, 0, 0, pitch, ink, 0.f);
    else {
        Gdi sm(w, h); HFONT f = MakeFont((int)std::lround(pitch * 8.6f), FW_BOLD);
        int tw = Measure(sm.dc, f, s).cx + 4;
        if (tw > w) {   // the font is wider than the dots: new strip
            strip->width = w = tw; strip->pixels.assign((size_t)w * h, 0u);
            Gdi sm2(w, h); DrawText(sm2, strip->pixels, f, s, RECT{0, 0, w, h}, ink[0], ink[1], ink[2]);
        } else DrawText(sm, strip->pixels, f, s, RECT{0, 0, w, h}, ink[0], ink[1], ink[2]);
        DeleteObject(f);
    }
    return strip;
}
std::wstring BandOf(const std::wstring& source) {   // "Radio DAB+" -> "DAB+", "USB 2" -> "USB 2"
    size_t sp = source.rfind(L' ');
    std::wstring last = sp == std::wstring::npos ? source : source.substr(sp + 1);
    if (last == L"FM" || last == L"DAB+" || last == L"AM") return last;
    return source;
}
std::shared_ptr<OverlayImage> NewCard(int W, int H) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, 0u);
    return img;
}
} // namespace

// ---- classic truck radio: faceplate with CD slot, two knobs, source button, LCD and station buttons ----
static std::shared_ptr<OverlayImage> ClassicCard(const OverlayInfo& info, int W, int H, const CardTheme& th, float u, SongLine* songOut) {
    auto img = NewCard(W, H);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const float Wu = W / u;
    CardBackground(px, W, H, th);
    cv.Round(0, 0, W, std::max(1, U(1.5f)), 0, 1.f, 1.f, 1.f, 0.10f);   // top edge highlight
    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const float* ink = th.lcdText;
    const float panelDark[3] = {th.bgBottom[0] * 0.55f, th.bgBottom[1] * 0.55f, th.bgBottom[2] * 0.55f};

    // knobs (left: volume, right: tuning) - only when there is room for them
    const bool knobs = Wu >= 470;
    const float R = knobs ? std::min(46.f, (Wu - 380) / 4.4f) : 0.f;
    const int side = U(20);
    const int lcdX0 = knobs ? side + U(2 * R) + U(20) : side, lcdX1 = W - lcdX0;
    auto Knob = [&](float cx, float cy) {
        float r = R * u;
        cv.Circle(cx, cy + U(3), r + U(4), 0.f, 0.f, 0.f, 0.45f);                    // shadow
        cv.Circle(cx, cy, r + U(3), panelDark[0], panelDark[1], panelDark[2]);       // ring in the faceplate
        for (int k = 0; k < 24; ++k) {                                                // knurled edge
            float a = k * 3.14159265f / 12;
            cv.Circle(cx + std::cos(a) * r * 0.97f, cy + std::sin(a) * r * 0.97f, r * 0.07f, th.tile[0] * 0.8f, th.tile[1] * 0.8f, th.tile[2] * 0.8f);
        }
        cv.Circle(cx, cy, r * 0.92f, th.tile[0], th.tile[1], th.tile[2]);
        cv.Glow(cx - r * 0.3f, cy - r * 0.35f, r * 0.9f, 1.f, 1.f, 1.f, 0.10f);    // light from above
        cv.Circle(cx, cy, r * 0.62f, th.tile[0] * 0.75f, th.tile[1] * 0.75f, th.tile[2] * 0.75f);
        cv.Line(cx, cy - r * 0.86f, cx, cy - r * 0.62f, (float)U(3), th.sub[0], th.sub[1], th.sub[2]);   // marker
    };
    const float knobY = 196 * u;
    if (knobs) { Knob(side + R * u, knobY); Knob(W - side - R * u, knobY); }

    // top row: eject / play buttons, CD slot, source button
    HFONT fBtn = MakeFont(U(15), FW_BOLD);
    const int topY = U(14), topH = U(28);
    std::wstring src = Upper(info.source.empty() ? L"Radio" : info.source);
    int srcW = Measure(mdc, fBtn, src).cx + U(30);
    int srcX = W - side - srcW;
    cv.Round(srcX, topY + U(2), srcW, topH, (float)U(6), 0.f, 0.f, 0.f, 0.5f);
    cv.Round(srcX, topY, srcW, topH, (float)U(6), th.accent[0], th.accent[1], th.accent[2]);
    cv.Round(srcX + U(2), topY + U(2), srcW - U(4), topH / 2 - U(2), (float)U(5), 1.f, 1.f, 1.f, 0.12f);
    DrawText(mask, px, fBtn, src, RECT{srcX, topY, srcX + srcW, topY + topH}, th.chipText[0], th.chipText[1], th.chipText[2], DT_CENTER);
    DeleteObject(fBtn);
    int bx = side;
    for (int k = 0; k < 2; ++k) {   // eject, play
        int bw = U(44);
        cv.Round(bx, topY + U(2), bw, topH, (float)U(5), 0.f, 0.f, 0.f, 0.5f);
        cv.Round(bx, topY, bw, topH, (float)U(5), th.tile[0], th.tile[1], th.tile[2]);
        float cx = bx + bw / 2.f, cy = topY + topH / 2.f, s = topH * 0.5f;
        if (k == 0) { cv.Poly({{cx - s * 0.5f, cy + s * 0.1f}, {cx, cy - s * 0.45f}, {cx + s * 0.5f, cy + s * 0.1f}}, th.sub[0], th.sub[1], th.sub[2]);
                      BoxAA(cv, cx - s * 0.5f, cy + s * 0.25f, s, s * 0.16f, th.sub[0], th.sub[1], th.sub[2]); }
        else cv.Poly({{cx - s * 0.3f, cy - s * 0.45f}, {cx + s * 0.4f, cy}, {cx - s * 0.3f, cy + s * 0.45f}}, th.sub[0], th.sub[1], th.sub[2]);
        bx += bw + U(8);
    }
    int slotX0 = bx + U(14), slotX1 = srcX - U(18);
    if (slotX1 - slotX0 > U(60)) {
        cv.Round(slotX0, topY + topH / 2 - U(5), slotX1 - slotX0, U(10), (float)U(5), 0.f, 0.f, 0.f, 0.85f);
        cv.Round(slotX0 + U(6), topY + topH / 2 - U(1), slotX1 - slotX0 - U(12), U(2), 1.f, 1.f, 1.f, 1.f, 0.06f);
        cv.Circle(slotX0 - U(6), topY + topH / 2.f, (float)U(2.5f), th.lcdGlow[0], th.lcdGlow[1], th.lcdGlow[2]);
        cv.Circle(slotX1 + U(6), topY + topH / 2.f, (float)U(2.5f), th.lcdGlow[0], th.lcdGlow[1], th.lcdGlow[2]);
    }

    // LCD: dark bezel, backlit glass (green, warm towards the right), dot-matrix text
    const int lcdY0 = U(56), lcdY1 = U(262);
    cv.Round(lcdX0 - U(7), lcdY0 - U(7), lcdX1 - lcdX0 + U(14), lcdY1 - lcdY0 + U(14), (float)U(10), panelDark[0], panelDark[1], panelDark[2]);
    {
        int lw = lcdX1 - lcdX0, lh = lcdY1 - lcdY0;
        for (int y = 0; y < lh; ++y) for (int x = 0; x < lw; ++x) {
            float cov = RoundedCoverage(x, y, lw, lh, (float)U(6));
            if (cov <= 0) continue;
            float tx = (float)x / lw, ty = (float)y / lh;
            float warm = std::clamp((tx - 0.45f) * 1.2f + (ty - 0.5f) * 0.4f, 0.f, 1.f) * 0.75f;
            float dim = 1.f - 0.10f * ty - 0.12f * std::abs(tx - 0.45f);
            float r = (th.lcd[0] * (1 - warm) + th.lcdGlow[0] * warm) * dim, g = (th.lcd[1] * (1 - warm) + th.lcdGlow[1] * warm) * dim,
                  b = (th.lcd[2] * (1 - warm) + th.lcdGlow[2] * warm) * dim;
            px[(size_t)(lcdY0 + y) * W + lcdX0 + x] = Pack(1.f, r, g, b);
        }
        for (int y = 0; y < U(10); ++y) cv.Round(lcdX0, lcdY0 + y, lw, 1, 0, 0.f, 0.f, 0.f, 0.22f * (1.f - y / (float)U(10)));   // inner shadow
    }
    const int ix0 = lcdX0 + U(16), ix1 = lcdX1 - U(16);
    const float ghost = 0.05f;
    // status row: antenna + signal, band, station button / time
    {
        float p = 2.6f * u, y = lcdY0 + U(13);
        int x = ix0;
        if (!info.media && info.volume < 0) {
            float s = p * 8;   // antenna
            cv.Line(x + s * 0.35f, y + s * 0.15f, x + s * 0.35f, y + s, std::max(1.5f, p * 0.8f), ink[0], ink[1], ink[2]);
            cv.Line(x, y, x + s * 0.35f, y + s * 0.4f, std::max(1.5f, p * 0.8f), ink[0], ink[1], ink[2]);
            cv.Line(x + s * 0.7f, y, x + s * 0.35f, y + s * 0.4f, std::max(1.5f, p * 0.8f), ink[0], ink[1], ink[2]);
            x += (int)(s * 0.9f);
            for (int i = 0; i < 5; ++i) {
                float bh = s * (0.2f + 0.2f * i);
                BoxAA(cv, x + i * p * 1.8f, y + s - bh, p * 1.2f, bh, ink[0], ink[1], ink[2], i < std::max(0, info.bars) ? 0.92f : ghost);
            }
            x += (int)(5 * p * 1.8f + p * 3);
        }
        std::wstring band = info.volume >= 0 ? L"" : Upper(BandOf(info.source));
        if (!band.empty() && DotFontCovers(band)) { DotText(cv, band, (float)x, y, p, ink, ghost); x += (int)(DotWidth(band.size(), p) + p * 4); }
        std::wstring right;
        if (info.volume < 0 && info.media && info.posSec >= 0) right = Clock(info.posSec) + (info.lenSec > 0 ? L" / " + Clock(info.lenSec) : L"");
        else if (info.volume < 0 && info.button > 0) right = L"PRESET " + std::to_wstring(info.button);
        if (info.volume < 0 && info.favourite) right = (right.empty() ? L"" : right + L" ") + L"♥";
        if (!right.empty()) DotText(cv, right, ix1 - DotWidth(right.size(), p), y, p, ink, ghost);
    }
    if (info.volume >= 0) {   // volume: label, big number, 20 segments
        const bool muted = info.muted;
        int v = std::clamp(info.volume, 0, 100);
        std::wstring label = Upper(info.volumeLabel.empty() ? L"Volume" : info.volumeLabel);
        LcdLine(cv, mask, px, label, ix0, ix1, (float)lcdY0 + U(42), 3.4f * u, 2.4f * u, ink, ghost, true);
        std::wstring num = muted ? L"--" : std::to_wstring(v);
        LcdLine(cv, mask, px, num, ix0, ix1, (float)lcdY0 + U(76), 8.5f * u, 4.f * u, ink, ghost, true);
        const int segs = 20; float gap = 4.f * u, sw = (ix1 - ix0 - (segs - 1) * gap) / segs, sy = lcdY0 + U(162), sh = U(26);
        int lit = muted ? 0 : (v + 2) / 5;
        for (int i = 0; i < segs; ++i) {
            float h = sh * (0.35f + 0.65f * (i + 1) / segs);
            BoxAA(cv, ix0 + i * (sw + gap), sy + sh - h, sw, h, ink[0], ink[1], ink[2], i < lit ? 0.92f : ghost);
        }
    } else {
        // station name (large), country, song / detail (scrolls)
        std::wstring title = Upper(info.title), sub = Upper(info.subtitle);
        std::wstring line4 = (!info.media && !info.song.empty()) ? Upper(info.song) : Upper(info.detail);
        const float pTitle = std::clamp((ix1 - ix0) / std::max(1.f, DotWidth(title.size(), 1.f)), 3.2f * u, 6.4f * u);
        const float pSub = 3.0f * u, pSong = 3.3f * u;
        float hT = pTitle * 8, hS = sub.empty() ? 0 : pSub * 8, hL = line4.empty() ? 0 : pSong * 8;
        float top = lcdY0 + U(40), bottom = lcdY1 - U(14);
        float gap = std::max(4.f * u, (bottom - top - hT - hS - hL) / 3.f);
        float y = top + std::max(0.f, (bottom - top - hT - hS - hL - gap * ((hS > 0) + (hL > 0))) / 2);
        LcdLine(cv, mask, px, title, ix0, ix1, y, pTitle, 3.2f * u, ink, ghost, true); y += hT + gap;
        if (hS > 0) { LcdLine(cv, mask, px, sub, ix0, ix1, y, pSub, 2.4f * u, ink, ghost, true); y += hS + gap; }
        if (hL > 0) {
            float lw = DotWidth(line4.size(), pSong);
            if (songOut && (lw > ix1 - ix0 || !DotFontCovers(line4))) {   // too long: a strip that scrolls
                songOut->strip = LcdStrip(line4, pSong, ink, H);
                songOut->x = ix0; songOut->y = (int)y; songOut->w = ix1 - ix0; songOut->h = songOut->strip->height;
                if (songOut->strip->width <= songOut->w) { songOut->x = ix0 + (songOut->w - songOut->strip->width) / 2; songOut->w = songOut->strip->width; }
            } else LcdLine(cv, mask, px, line4, ix0, ix1, y, pSong, pSong, ink, ghost, true);
        }
    }

    // station buttons under the LCD (6 like a car radio; 4..9 when one of 7..9 is active)
    {
        int first = info.button > 6 ? 4 : 1;
        int by = U(276), bh = U(40), n = 6;
        float gap = 8.f * u, bw = (lcdX1 - lcdX0 - (n - 1) * gap) / n;
        HFONT fNum = MakeFont(U(19), FW_SEMIBOLD);
        for (int k = 0; k < n; ++k) {
            int x0 = lcdX0 + (int)std::lround(k * (bw + gap)), x1 = lcdX0 + (int)std::lround(k * (bw + gap) + bw);
            bool act = info.volume < 0 && info.button == first + k;
            cv.Round(x0, by + U(3), x1 - x0, bh, (float)U(6), 0.f, 0.f, 0.f, 0.5f);
            cv.Round(x0, by, x1 - x0, bh, (float)U(6), th.tile[0], th.tile[1], th.tile[2]);
            cv.Round(x0 + U(2), by + U(2), x1 - x0 - U(4), bh / 2 - U(2), (float)U(5), 1.f, 1.f, 1.f, 0.05f);
            if (act) cv.Round(x0 + (x1 - x0) / 2 - U(9), by + U(5), U(18), U(3), 1.5f * u, th.lcdGlow[0], th.lcdGlow[1], th.lcdGlow[2]);
            const float* c = act ? th.text : th.sub;
            DrawText(mask, px, fNum, std::to_wstring(first + k), RECT{x0, by + U(4), x1, by + bh}, c[0], c[1], c[2], DT_CENTER);
        }
        DeleteObject(fNum);
        if (knobs && info.volume < 0 && !info.position.empty()) {   // "3 / 41" under the tuning knob
            HFONT fPos = MakeFont(U(19), FW_SEMIBOLD), fLab = MakeFont(U(13), FW_SEMIBOLD);
            int cx = (int)(W - side - R * u), hw = U(R + 14);
            if (info.button > 0) DrawText(mask, px, fLab, L"PRESET " + std::to_wstring(info.button), RECT{cx - hw, U(258), cx + hw, U(276)}, th.muted[0], th.muted[1], th.muted[2], DT_CENTER);
            DrawText(mask, px, fPos, info.position, RECT{cx - hw, U(276), cx + hw, U(304)}, th.sub[0], th.sub[1], th.sub[2], DT_CENTER);
            DeleteObject(fPos); DeleteObject(fLab);
        }
    }
    return img;
}

// ---- smart dashboard: source chip and signal at the top, icon bar on the left, glass panel with logo and text ----
static std::shared_ptr<OverlayImage> SmartCard(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const CardTheme& th, float u, float logoSize,
                                               SongLine* songOut) {
    auto img = NewCard(W, H);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    CardBackground(px, W, H, th);
    if (th.glow > 0) cv.Glow((float)U(260), H * 0.55f, (float)U(320), th.accent[0], th.accent[1], th.accent[2], th.glow);
    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const int padX = U(20);
    // header
    HFONT fChip = MakeFont(U(16), FW_BOLD), fHead = MakeFont(U(19), FW_NORMAL);
    std::wstring chip = Upper(info.source.empty() ? L"Radio" : info.source);
    int chipW = Measure(mdc, fChip, chip).cx + U(28), chipH = U(30), chipY = U(14);
    cv.Round(padX, chipY, chipW, chipH, chipH / 2.f, th.accent[0], th.accent[1], th.accent[2]);
    DrawText(mask, px, fChip, chip, RECT{padX, chipY, padX + chipW, chipY + chipH}, th.chipText[0], th.chipText[1], th.chipText[2], DT_CENTER);
    int rightEdge = W - padX;
    if (!info.media && info.bars >= 0) {
        int sw = SignalWidth(u);
        Signal(cv, rightEdge - sw, chipY + chipH - U(3), info.bars, u, th.accent, th.inactive);
        rightEdge -= sw + U(14);
    }
    if (info.media && info.mediaType != "none") {
        DrawIcon(cv, info.paused ? Icon::PlayPause : Icon::PlayPause, (float)rightEdge - U(12), chipY + chipH / 2.f, (float)U(20), info.paused ? th.muted : th.accent);
        rightEdge -= U(36);
    }
    if (info.favourite) {
        HFONT fHeart = MakeFont(U(22), FW_NORMAL);
        DrawText(mask, px, fHeart, L"♥", RECT{rightEdge - U(24), chipY, rightEdge, chipY + chipH}, th.accent[0], th.accent[1], th.accent[2], DT_RIGHT);
        DeleteObject(fHeart);
        rightEdge -= U(34);
    }
    DrawText(mask, px, fHead, info.headline, RECT{padX + chipW + U(14), chipY, std::max(padX + chipW + U(14), rightEdge), chipY + chipH}, th.muted[0], th.muted[1], th.muted[2]);
    DeleteObject(fChip); DeleteObject(fHead);

    // icon bar
    const int barW = U(46), panelX = padX + barW + U(12), panelY = U(60), panelW = W - panelX - padX, panelH = U(346) - panelY;
    {
        const Icon icons[] = {Icon::PlayPause, Icon::Skip, Icon::Back, Icon::Home, Icon::Gear};
        for (int k = 0; k < 5; ++k) {
            float cy = panelY + U(26) + k * (panelH - U(52)) / 4.f;
            DrawIcon(cv, icons[k], padX + barW / 2.f, cy, (float)U(22), th.art);
        }
    }
    // glass panel
    cv.Round(panelX, panelY, panelW, panelH, (float)U(18), th.tile[0], th.tile[1], th.tile[2], 0.62f);
    cv.Round(panelX, panelY, panelW, std::max(1, U(1.5f)), 0, 1.f, 1.f, 1.f, 0.06f);
    const bool progress = info.media && info.posSec >= 0;
    const int inTop = panelY + U(18), inBottom = panelY + panelH - (progress ? U(56) : U(18));
    const int T = std::clamp(U(180 * logoSize), U(60), inBottom - inTop);
    const int tileX = panelX + U(22), tileY = inTop + (inBottom - inTop - T) / 2;
    DrawArtTile(cv, mask, px, W, th, info, logo, tileX, tileY, T, u);
    DrawTextColumn(mask, px, th, info, tileX + T + U(26), panelX + panelW - U(22), tileY, T, u, H, songOut, 1.06f);
    if (progress) {
        HFONT fTime = MakeFont(U(17), FW_SEMIBOLD);
        int cy = panelY + panelH - U(28), tWid = U(58), x0 = panelX + U(22), x1 = panelX + panelW - U(22);
        DrawText(mask, px, fTime, Clock(info.posSec), RECT{x0, cy - U(14), x0 + tWid, cy + U(14)}, th.muted[0], th.muted[1], th.muted[2]);
        DrawText(mask, px, fTime, Clock(info.lenSec), RECT{x1 - tWid, cy - U(14), x1, cy + U(14)}, th.muted[0], th.muted[1], th.muted[2], DT_RIGHT);
        int bx = x0 + tWid + U(8), bw = x1 - x0 - 2 * (tWid + U(8)), bh = U(6);
        cv.Round(bx, cy - bh / 2, bw, bh, bh / 2.f, th.inactive[0], th.inactive[1], th.inactive[2]);
        if (info.lenSec > 0) {
            int fw = std::max(bh, (int)(bw * std::clamp((float)info.posSec / info.lenSec, 0.f, 1.f)));
            cv.Round(bx, cy - bh / 2, fw, bh, bh / 2.f, th.accent[0], th.accent[1], th.accent[2]);
        }
        DeleteObject(fTime);
    }
    return img;
}

// ---- station list (infotainment): station name between arrows, live song line, logo tiles of the station buttons ----
static std::shared_ptr<OverlayImage> PresetsCard(const OverlayInfo& info, int W, int H, const CardTheme& th, float u, SongLine* songOut) {
    auto img = NewCard(W, H);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const float Wu = W / u;
    CardBackground(px, W, H, th);
    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const int padX = U(20);
    // header: < STATION >
    HFONT fTitle = MakeFont(U(34), FW_SEMIBOLD);
    DrawIcon(cv, Icon::Prev, (float)padX + U(10), (float)U(34), (float)U(26), th.text);
    DrawIcon(cv, Icon::Next, (float)W - padX - U(10), (float)U(34), (float)U(26), th.text);
    DrawText(mask, px, fTitle, Upper(info.title), RECT{padX + U(40), U(10), W - padX - U(40), U(58)}, th.text[0], th.text[1], th.text[2], DT_CENTER);
    DeleteObject(fTitle);
    // second line: live song title (scrolls when long), else country and status
    {
        HFONT f = MakeFont(U(19), FW_SEMIBOLD);
        const int y0 = U(60), h = U(30), x0 = padX + U(20), x1 = W - padX - U(20);
        std::wstring line = !info.song.empty() ? info.song : info.subtitle;
        const float* c = !info.song.empty() ? th.text : th.sub;
        if (!info.song.empty() && songOut) {
            int sw = Measure(mdc, f, line).cx + U(4);
            auto strip = std::make_shared<OverlayImage>();
            strip->width = std::max(1, sw); strip->height = h; strip->renderedForScreenHeight = H;
            strip->pixels.assign((size_t)strip->width * h, 0u);
            Gdi sm(strip->width, h);
            DrawText(sm, strip->pixels, f, line, RECT{0, 0, strip->width, h}, c[0], c[1], c[2]);
            songOut->strip = strip; songOut->y = y0; songOut->h = h;
            if (sw <= x1 - x0) { songOut->x = x0 + (x1 - x0 - sw) / 2; songOut->w = sw; } else { songOut->x = x0; songOut->w = x1 - x0; }
        } else DrawText(mask, px, f, line, RECT{x0, y0, x1, y0 + h}, c[0], c[1], c[2], DT_CENTER);
        DeleteObject(f);
    }
    // tiles: as many as fit (about 100 units each), the page with the active button
    const int count = (int)info.tiles.size();
    int n = std::clamp((int)((Wu - 40 + 10) / (100 + 10)), 3, std::max(3, count));
    n = std::min(n, count);
    int active = 0;
    for (int k = 0; k < count; ++k) if (info.tiles[k].active) active = k;
    const int pages = (count + n - 1) / n;
    int page = active / n, first = std::min(page * n, count - n);
    const float gap = 10.f * u, tw = std::min(118.f * u, (W - 2 * padX - (n - 1) * gap) / n);
    const float rowW = n * tw + (n - 1) * gap, rx = (W - rowW) / 2;
    const int ty = U(102), tH = U(158);
    HFONT fName = MakeFont(U(14), FW_SEMIBOLD), fNum = MakeFont(U(40), FW_SEMIBOLD);
    for (int k = 0; k < n; ++k) {
        const OverlayInfo::Tile& t = info.tiles[first + k];
        const OverlayImage* lg = (size_t)(first + k) < info.tileLogos.size() ? info.tileLogos[first + k].get() : nullptr;
        int x0 = (int)std::lround(rx + k * (tw + gap)), x1 = (int)std::lround(rx + k * (tw + gap) + tw), w = x1 - x0;
        if (t.active) {
            cv.Round(x0 - U(4), ty - U(4), w + U(8), tH + U(8), (float)U(9), th.text[0], th.text[1], th.text[2], 0.85f);
            cv.Round(x0 - U(2), ty - U(2), w + U(4), tH + U(4), (float)U(8), th.tile[0] * 1.6f, th.tile[1] * 1.6f, th.tile[2] * 1.6f);
        } else cv.Round(x0, ty, w, tH, (float)U(6), th.tile[0], th.tile[1], th.tile[2]);
        const int nameH = U(30), bx = x0 + U(7), by = ty + U(7), bw = w - U(14), bh = tH - U(14) - nameH;
        if (t.name.empty()) {   // empty button: its number
            DrawText(mask, px, fNum, std::to_wstring(t.button), RECT{x0, ty, x1, ty + tH - nameH / 2}, th.inactive[0] * 1.6f, th.inactive[1] * 1.6f, th.inactive[2] * 1.6f, DT_CENTER);
            continue;
        }
        cv.Round(bx, by, bw, bh, (float)U(5), th.logoTile[0], th.logoTile[1], th.logoTile[2]);
        if (lg) BlitLogo(px, W, lg, bx + U(8), by + U(8), bw - U(16), bh - U(16));
        else {
            HFONT fIni = MakeFont((int)(std::min(bw, bh) * 0.36f), FW_BOLD);
            DrawText(mask, px, fIni, Initials(t.name), RECT{bx, by, bx + bw, by + bh}, th.accent[0], th.accent[1], th.accent[2], DT_CENTER);
            DeleteObject(fIni);
        }
        DrawText(mask, px, fName, Upper(t.name), RECT{x0 + U(4), by + bh + U(2), x1 - U(4), ty + tH - U(2)}, th.text[0], th.text[1], th.text[2], DT_CENTER);
    }
    DeleteObject(fName); DeleteObject(fNum);
    // page marks
    if (pages > 1) {
        const float mw = 56.f * u, mg = 10.f * u, total = pages * mw + (pages - 1) * mg;
        for (int k = 0; k < pages; ++k) {
            const float* c = k == page ? th.accent : th.inactive;
            BoxAA(cv, (W - total) / 2 + k * (mw + mg), (float)U(276), mw, std::max(2.f, 3.f * u), c[0], c[1], c[2]);
        }
    }
    // bottom bar: list and radio icons, source, signal
    cv.Round(padX, U(298), W - 2 * padX, std::max(1, U(1)), 0, th.text[0], th.text[1], th.text[2], 0.10f);
    DrawIcon(cv, Icon::List, (float)padX + U(16), (float)U(324), (float)U(22), th.art);
    DrawIcon(cv, Icon::Radio, (float)padX + U(62), (float)U(324), (float)U(22), th.art);
    HFONT fSrc = MakeFont(U(16), FW_SEMIBOLD);
    std::wstring src = Upper(info.source) + (info.position.empty() ? L"" : L"  •  " + info.position);
    DrawText(mask, px, fSrc, src, RECT{padX + U(90), U(308), W - padX - U(90), U(340)}, th.muted[0], th.muted[1], th.muted[2], DT_CENTER);
    DeleteObject(fSrc);
    if (info.bars >= 0) Signal(cv, W - padX - SignalWidth(u), U(334), info.bars, u, th.accent, th.inactive);
    else DrawIcon(cv, Icon::Gear, (float)W - padX - U(14), (float)U(324), (float)U(22), th.art);
    return img;
}

static std::shared_ptr<OverlayImage> RenderScreenCardImpl(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const std::string& design,
                                                          float uLayout, float logoSize, SongLine* songOut) {
    auto thp = ThemeFor(design);
    const CardTheme& th = *thp;
    FontScope fontScope(th.font.c_str());
    if (info.volume < 0 || th.layout == "classic") {   // the other layouts show the volume like the standard card
        if (th.layout == "classic") return ClassicCard(info, W, H, th, uLayout, songOut);
        if (th.layout == "smart") return SmartCard(info, logo, W, H, th, uLayout, logoSize, songOut);
        if (th.layout == "presets" && !info.media && !info.tiles.empty()) return PresetsCard(info, W, H, th, uLayout, songOut);
    }
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    const float u = uLayout;   // layout unit: 1.0 at 640x360 (the card is 360 units high)
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const float AR = th.accent[0], AG = th.accent[1], AB = th.accent[2];   // accent (chip, bars, progress)
    const float TR = th.text[0], TG = th.text[1], TB = th.text[2];         // main text
    const float MR = th.muted[0], MG = th.muted[1], MB = th.muted[2];      // muted text
    const float IR = th.inactive[0], IG = th.inactive[1], IB = th.inactive[2];   // unlit bars, tracks

    // background: dark blue-grey gradient + a soft amber glow behind the artwork
    for (int y = 0; y < H; ++y) {
        float t = (float)y / std::max(1, H - 1);
        uint32_t v = Pack(1.f, th.bgTop[0] + (th.bgBottom[0] - th.bgTop[0]) * t, th.bgTop[1] + (th.bgBottom[1] - th.bgTop[1]) * t,
                          th.bgTop[2] + (th.bgBottom[2] - th.bgTop[2]) * t);
        std::fill(px.begin() + (size_t)y * W, px.begin() + (size_t)(y + 1) * W, v);
    }
    if (th.background) CardBackground(px, W, H, th);   // own style with a picture
    if (th.glow > 0) cv.Glow(U(120), H * 0.62f, (float)U(300), AR, AG, AB, th.glow);

    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const int padX = U(22);

    // header: source chip + headline, status on the right
    HFONT fChip = MakeFont(U(16), FW_BOLD), fHead = MakeFont(U(19), FW_NORMAL);
    std::wstring chip = Upper(info.source.empty() ? L"Radio" : info.source);
    int chipW = Measure(mdc, fChip, chip).cx + U(26), chipH = U(28), chipY = U(17);
    cv.Round(padX, chipY, chipW, chipH, chipH / 2.f, AR, AG, AB);
    DrawText(mask, px, fChip, chip, RECT{padX, chipY, padX + chipW, chipY + chipH}, th.chipText[0], th.chipText[1], th.chipText[2], DT_CENTER);
    int rightEdge = W - padX, headY0 = chipY, headY1 = chipY + chipH;
    if (info.volume < 0 && !info.media && info.bars >= 0) {   // signal bars
        int bw = U(6), gap = U(4), base = headY1 - U(3);
        int x0 = rightEdge - (5 * bw + 4 * gap);
        for (int i = 0; i < 5; ++i) {
            int bh = U(7 + i * 4), xs = x0 + i * (bw + gap);
            if (i < info.bars) cv.Round(xs, base - bh, bw, bh, U(1.5f), AR, AG, AB);
            else cv.Round(xs, base - bh, bw, bh, U(1.5f), IR, IG, IB);
        }
        rightEdge = x0 - U(14);
    }
    if (info.volume < 0 && info.media && info.mediaType != "none") {   // play / pause symbol
        int s = U(20), x0 = rightEdge - s, y0 = headY0 + (chipH - s) / 2;
        if (info.paused) { cv.Round(x0 + U(2), y0, U(6), s, U(1.5f), TR, TG, TB); cv.Round(x0 + s - U(8), y0, U(6), s, U(1.5f), TR, TG, TB); }
        else cv.Poly({{(float)x0 + U(2), (float)y0}, {(float)x0 + s, y0 + s / 2.f}, {(float)x0 + U(2), (float)y0 + s}}, AR, AG, AB);
        rightEdge = x0 - U(14);
    }
    if (info.volume < 0 && info.favourite) {
        HFONT fHeart = MakeFont(U(22), FW_NORMAL);
        DrawText(mask, px, fHeart, L"♥", RECT{rightEdge - U(24), headY0, rightEdge, headY1}, AR, AG, AB, DT_RIGHT);
        DeleteObject(fHeart);
        rightEdge -= U(36);
    }
    std::wstring headline = info.volume >= 0 ? (info.volumeLabel.empty() ? L"Volume" : info.volumeLabel) : info.headline;
    DrawText(mask, px, fHead, headline, RECT{padX + chipW + U(14), headY0, std::max(padX + chipW + U(14), rightEdge), headY1}, MR, MG, MB);
    cv.Round(padX, U(60), W - 2 * padX, std::max(1, U(1.5f)), 1.f, TR, TG, TB, 0.12f);   // separator
    DeleteObject(fChip); DeleteObject(fHead);

    if (info.volume >= 0) {   // ---- volume ----
        const bool muted = info.muted;
        int v = std::clamp(info.volume, 0, 100);
        // speaker symbol
        const int mid = U(160);
        float s = (float)U(78), sx = (float)padX + U(4), sy = mid - s / 2;
        float ir = muted ? MR : TR, ig = muted ? MG : TG, ib = muted ? MB : TB;
        cv.Round((int)sx, (int)(sy + s * 0.32f), (int)(s * 0.26f), (int)(s * 0.36f), U(3), ir, ig, ib);
        cv.Poly({{sx + s * 0.22f, sy + s * 0.32f}, {sx + s * 0.55f, sy + s * 0.06f}, {sx + s * 0.55f, sy + s * 0.94f}, {sx + s * 0.22f, sy + s * 0.68f}}, ir, ig, ib);
        float cx = sx + s * 0.56f, cyy = sy + s * 0.5f;
        if (muted) {
            cv.Line(cx + s * 0.18f, cyy - s * 0.17f, cx + s * 0.52f, cyy + s * 0.17f, (float)U(6), 0.95f, 0.33f, 0.28f);
            cv.Line(cx + s * 0.18f, cyy + s * 0.17f, cx + s * 0.52f, cyy - s * 0.17f, (float)U(6), 0.95f, 0.33f, 0.28f);
        } else {
            int waves = v == 0 ? 0 : v <= 33 ? 1 : v <= 66 ? 2 : 3;
            for (int i = 0; i < 3; ++i) {
                bool on = i < waves;
                cv.Arc(cx, cyy, s * (0.2f + 0.16f * i), (float)U(5), -0.85f, 0.85f, on ? AR : IR, on ? AG : IG, on ? AB : IB);
            }
        }
        // big percentage (grey while muted: the volume that comes back when unmuting)
        HFONT fBig = MakeFont(U(96), FW_BOLD), fPct = MakeFont(U(40), FW_SEMIBOLD);
        std::wstring num = std::to_wstring(v);
        int nx = (int)(sx + s * 1.25f) + U(20);
        int nw = Measure(mdc, fBig, num).cx;
        DrawText(mask, px, fBig, num, RECT{nx, mid - U(64), nx + nw + U(4), mid + U(60)}, muted ? MR : TR, muted ? MG : TG, muted ? MB : TB);
        DrawText(mask, px, fPct, L"%", RECT{nx + nw + U(6), mid - U(8), nx + nw + U(70), mid + U(42)}, MR, MG, MB);
        DeleteObject(fBig); DeleteObject(fPct);
        // 20 segments = 5 % steps (the step size of the volume keys)
        const int segs = 20, gap = U(5), by = U(262), bh = U(40);
        float segW = (W - 2 * padX - (segs - 1) * gap) / (float)segs;
        int lit = (v + 2) / 5;
        for (int i = 0; i < segs; ++i) {
            int x0 = padX + (int)std::lround(i * (segW + gap)), x1 = padX + (int)std::lround(i * (segW + gap) + segW);
            int h = bh * (35 + 65 * (i + 1) / segs) / 100;   // rising wedge
            if (i < lit && muted) cv.Round(x0, by + bh - h, x1 - x0, h, U(2), MR, MG, MB);
            else if (i < lit) {
                float k = 0.72f + 0.28f * i / (segs - 1);   // brighter towards the right
                cv.Round(x0, by + bh - h, x1 - x0, h, U(2), AR * k, AG * k, AB * k);
            } else cv.Round(x0, by + bh - h, x1 - x0, h, U(2), IR, IG, IB);
        }
        return img;
    }

    // ---- station / track ----
    const bool progress = info.media && info.posSec >= 0;
    const int top = U(78), bottom = progress ? U(286) : U(338);
    const int T = std::clamp(U(200 * logoSize), U(60), bottom - top);   // logo / cover tile (size setting)
    const int tileX = padX, tileY = top + (bottom - top - T) / 2;
    DrawArtTile(cv, mask, px, W, th, info, logo, tileX, tileY, T, u);
    DrawTextColumn(mask, px, th, info, tileX + T + U(26), W - padX, tileY, T, u, H, songOut);

    if (progress) {   // elapsed - bar - length
        HFONT fTime = MakeFont(U(18), FW_SEMIBOLD);
        int cy = U(318), tWid = U(62);
        DrawText(mask, px, fTime, Clock(info.posSec), RECT{padX, cy - U(14), padX + tWid, cy + U(14)}, MR, MG, MB);
        DrawText(mask, px, fTime, Clock(info.lenSec), RECT{W - padX - tWid, cy - U(14), W - padX, cy + U(14)}, MR, MG, MB, DT_RIGHT);
        int bx = padX + tWid + U(10), bw = W - 2 * (padX + tWid + U(10)), bh = U(8);
        cv.Round(bx, cy - bh / 2, bw, bh, bh / 2.f, IR, IG, IB);
        if (info.lenSec > 0 && info.posSec >= 0) {
            float f = std::clamp((float)info.posSec / info.lenSec, 0.f, 1.f);
            int fw = std::max(bh, (int)(bw * f));
            cv.Round(bx, cy - bh / 2, fw, bh, bh / 2.f, AR, AG, AB);
            cv.Circle(bx + fw - bh / 2.f, (float)cy, (float)U(8), TR, TG, TB);
        }
        DeleteObject(fTime);
    }
    return img;
}

// Size setting: the layout is 360 units high; "scale" makes the units larger or smaller, but never taller than the area.
// If the content is lower than the area it is centred vertically and the background is extended.
std::shared_ptr<OverlayImage> RenderScreenCard(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const std::string& design, float scale,
                                               float logoSize, SongLine* song) {
    float base = std::min(W / 16.f, H / 9.f) / 40.f;
    float u = std::min(base * std::clamp(scale, 0.4f, 2.f), H / 360.f);
    logoSize = std::clamp(logoSize, 0.4f, 1.5f);
    int h2 = (int)std::ceil(360.f * u);
    if (song) *song = SongLine{};
    if (h2 >= H - 2) return RenderScreenCardImpl(info, logo, W, H, design, u, logoSize, song);
    auto inner = RenderScreenCardImpl(info, logo, W, h2, design, u, logoSize, song);
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.resize((size_t)W * H);
    int off = (H - h2) / 2;
    for (int y = 0; y < H; ++y) {
        int sy = std::clamp(y - off, 0, h2 - 1);   // above / below: the first / last row of the background
        std::copy(inner->pixels.begin() + (size_t)sy * W, inner->pixels.begin() + (size_t)(sy + 1) * W, img->pixels.begin() + (size_t)y * W);
    }
    if (song && song->strip) song->y += off;
    return img;
}

std::shared_ptr<OverlayImage> RenderScreenVolumeBar(const OverlayInfo& info, int W, int H, const std::string& design, float scale,
                                                    const OverlayImage* under) {
    auto thp = ThemeFor(design);
    const CardTheme& th = *thp;
    FontScope fontScope(th.font.c_str());
    std::shared_ptr<OverlayImage> img;
    if (under && under->width == W && under->height == H) img = std::make_shared<OverlayImage>(*under);
    else {
        img = std::make_shared<OverlayImage>();
        img->width = W; img->height = H; img->renderedForScreenHeight = H;
        img->pixels.assign((size_t)W * H, 0u);
    }
    const float u = std::min(W / 16.f, H / 9.f) / 40.f * std::clamp(scale, 0.4f, 2.f);
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const int sh = std::min(H, U(86));
    Canvas cv{img->pixels, W, H};
    // the bar: background of the design, slightly transparent, rounded lower corners (the top edge is the screen's)
    for (int y = 0; y < sh; ++y) {
        float t = (float)y / std::max(1, sh - 1);
        float r = th.bgTop[0] + (th.bgBottom[0] - th.bgTop[0]) * t * 0.5f, g = th.bgTop[1] + (th.bgBottom[1] - th.bgTop[1]) * t * 0.5f,
              b = th.bgTop[2] + (th.bgBottom[2] - th.bgTop[2]) * t * 0.5f;
        const float rad = (float)U(14);
        for (int x = 0; x < W; ++x) {
            float cov = 1.f;
            float dy = y + 0.5f - (sh - rad);
            if (dy > 0) {   // lower corners
                float dx = x + 0.5f < rad ? rad - (x + 0.5f) : x + 0.5f > W - rad ? x + 0.5f - (W - rad) : 0.f;
                if (dx > 0) cov = std::clamp(rad - std::hypot(dx, dy) + 0.5f, 0.f, 1.f);
            }
            cv.Blend(x, y, cov, r, g, b, 0.94f);
        }
    }
    const bool muted = info.muted;
    const int v = std::clamp(info.volume, 0, 100);
    const float TR = th.text[0], TG = th.text[1], TB = th.text[2], MR = th.muted[0], MG = th.muted[1], MB = th.muted[2];
    Gdi mask(W, sh);
    HFONT fNum = MakeFont(U(40), FW_SEMIBOLD);
    std::wstring text = muted ? (info.volumeLabel.empty() ? L"Mute" : info.volumeLabel) : std::to_wstring(v) + L"%";
    DrawText(mask, img->pixels, fNum, text, RECT{0, U(6), W, sh - U(24)}, muted ? MR : TR, muted ? MG : TG, muted ? MB : TB, DT_CENTER);
    DeleteObject(fNum);
    // level line: thin track over the whole width, the filled part in the text colour (grey while muted)
    const int lx0 = U(26), lx1 = W - U(26), lh = std::max(2, U(4)), ly = sh - U(15) - lh / 2;
    cv.Round(lx0, ly, lx1 - lx0, lh, lh / 2.f, th.inactive[0], th.inactive[1], th.inactive[2]);
    int fw = (int)std::lround((lx1 - lx0) * v / 100.f);
    if (fw >= lh) cv.Round(lx0, ly, fw, lh, lh / 2.f, muted ? MR : TR, muted ? MG : TG, muted ? MB : TB);
    return img;
}

namespace { void Handset(Canvas& cv, float cx, float cy, float s, float angle, float r, float g, float b); }
std::shared_ptr<OverlayImage> RenderScreenNotice(const OverlayInfo& info, int W, int H, const std::string& design, float scale,
                                                 const OverlayImage* under) {
    auto thp = ThemeFor(design);
    const CardTheme& th = *thp;
    FontScope fontScope(th.font.c_str());
    std::shared_ptr<OverlayImage> img;
    if (under && under->width == W && under->height == H) img = std::make_shared<OverlayImage>(*under);
    else img = NewCard(W, H);
    const float u = std::min(W / 16.f, H / 9.f) / 40.f * std::clamp(scale, 0.4f, 2.f);
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    Canvas cv{img->pixels, W, H};
    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const int m = U(12), x0 = m, bw = W - 2 * m, icon = U(58);
    const int tx0 = x0 + U(18) + icon + U(16), tx1 = x0 + bw - U(20);
    HFONT fApp = MakeFont(U(16), FW_SEMIBOLD), fTitle = MakeFont(U(23), FW_SEMIBOLD), fText = MakeFont(U(20), FW_NORMAL);
    const bool twoLines = !info.noticeText.empty() && Measure(mdc, fText, info.noticeText).cx > tx1 - tx0;
    const int bh = std::min(H - 2 * m, U(twoLines ? 134 : 112)), y0 = m;
    // card: shadow, glass panel in the design's colours, thin light edge
    cv.Round(x0, y0 + U(4), bw, bh, (float)U(20), 0.f, 0.f, 0.f, 0.35f);
    float bg[3]; for (int k = 0; k < 3; ++k) bg[k] = th.bgTop[k] * 0.45f + th.tile[k] * 0.55f;
    cv.Round(x0, y0, bw, bh, (float)U(20), bg[0], bg[1], bg[2], 0.96f);
    cv.Round(x0 + U(2), y0 + U(1), bw - U(4), std::max(1, U(1.5f)), 0.f, 1.f, 1.f, 1.f, 0.10f);
    // app icon: green phone (connected) or a speech bubble in the accent colour (message)
    const bool phone = info.noticeIcon == "phone" || info.noticeIcon == "call";
    const int ix = x0 + U(18), iy = y0 + (bh - icon) / 2;
    const float ic[3] = {phone ? 0.20f : th.accent[0], phone ? 0.78f : th.accent[1], phone ? 0.35f : th.accent[2]};
    cv.Round(ix, iy, icon, icon, (float)U(15), ic[0], ic[1], ic[2]);
    const float gr = phone ? 1.f : th.chipText[0], gg = phone ? 1.f : th.chipText[1], gb = phone ? 1.f : th.chipText[2];
    if (info.noticeIcon == "call") Handset(cv, ix + icon / 2.f, iy + icon / 2.f, icon * 0.62f, -0.75f, 1.f, 1.f, 1.f);   // incoming call
    else if (phone) {   // smartphone outline with screen and home dot
        int pw = U(24), ph = U(38), px0 = ix + (icon - pw) / 2, py0 = iy + (icon - ph) / 2;
        cv.Round(px0, py0, pw, ph, (float)U(5), gr, gg, gb);
        cv.Round(px0 + U(3), py0 + U(5), pw - U(6), ph - U(12), (float)U(1.5f), ic[0], ic[1], ic[2]);
        cv.Circle(px0 + pw / 2.f, py0 + ph - U(3.5f), (float)U(1.8f), ic[0], ic[1], ic[2]);
    } else {       // speech bubble with three dots
        int bw2 = U(34), bh2 = U(25), bx = ix + (icon - bw2) / 2, by = iy + (icon - bh2) / 2 - U(3);
        cv.Round(bx, by, bw2, bh2, (float)U(9), gr, gg, gb);
        cv.Poly({{(float)bx + U(7), (float)by + bh2 - U(2)}, {(float)bx + U(5), (float)by + bh2 + U(8)}, {(float)bx + U(16), (float)by + bh2 - U(2)}}, gr, gg, gb);
        for (int k = -1; k <= 1; ++k) cv.Circle(bx + bw2 / 2.f + k * U(8), by + bh2 / 2.f, (float)U(2.6f), ic[0], ic[1], ic[2]);
    }
    // text: app and time, sender, message
    int ty = y0 + U(14);
    std::wstring app = Upper(info.noticeApp);
    DrawText(mask, img->pixels, fApp, app, RECT{tx0, ty, tx1 - U(70), ty + U(22)}, th.muted[0], th.muted[1], th.muted[2]);
    DrawText(mask, img->pixels, fApp, info.noticeTime, RECT{tx1 - U(110), ty, tx1, ty + U(22)}, th.muted[0], th.muted[1], th.muted[2], DT_RIGHT);
    ty += U(25);
    DrawText(mask, img->pixels, fTitle, info.noticeTitle, RECT{tx0, ty, tx1, ty + U(32)}, th.text[0], th.text[1], th.text[2]);
    ty += U(32);
    if (twoLines) DrawText2(mask, img->pixels, fText, info.noticeText, RECT{tx0, ty, tx1, y0 + bh - U(10)}, th.sub[0], th.sub[1], th.sub[2]);
    else DrawText(mask, img->pixels, fText, info.noticeText, RECT{tx0, ty, tx1, ty + U(28)}, th.sub[0], th.sub[1], th.sub[2]);
    DeleteObject(fApp); DeleteObject(fTitle); DeleteObject(fText);
    return img;
}

// ------------------------------------------------------------------------------------------------
// Virtual phone (1.6.0): a smartphone drawn in the corner of the screen
namespace {
int WrapHeight(HDC dc, HFONT f, const std::wstring& s, int width) {
    if (s.empty()) return 0;
    RECT r{0, 0, std::max(1, width), 10000}; HGDIOBJ o = SelectObject(dc, f);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, o);
    return r.bottom;
}
void DrawWrap(Gdi& mask, std::vector<uint32_t>& out, HFONT f, const std::wstring& s, RECT r, float R, float G, float B) {
    if (s.empty()) return;
    RECT c = ClipRect(mask, r);
    if (c.right <= c.left || c.bottom <= c.top) return;
    ClearMask(mask, c);
    HGDIOBJ o = SelectObject(mask.dc, f);
    DrawTextW(mask.dc, s.c_str(), (int)s.size(), &r, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);
    SelectObject(mask.dc, o); GdiFlush();
    BlendMask(mask, out, c, R, G, B);
}
// handset glyph (receiver) centred at cx, cy; angle in radians (0 = normal, ~2.36 = hang up)
void Handset(Canvas& cv, float cx, float cy, float s, float angle, float r, float g, float b) {
    auto P = [&](float x, float y) { float c = std::cos(angle), sn = std::sin(angle); return std::make_pair(cx + x * c - y * sn, cy + x * sn + y * c); };
    std::vector<std::pair<float, float>> pts;
    // receiver: an arc open to the bottom with an ear and a mouth piece (angle 0 = lying, the hang-up symbol)
    for (int i = 0; i <= 16; ++i) { float a = 3.1416f * (1.15f + 0.7f * i / 16.f); pts.push_back(P(std::cos(a) * s * 0.42f, std::sin(a) * s * 0.42f + s * 0.22f)); }
    for (int i = 16; i >= 0; --i) { float a = 3.1416f * (1.15f + 0.7f * i / 16.f); pts.push_back(P(std::cos(a) * s * 0.26f, std::sin(a) * s * 0.26f + s * 0.22f)); }
    cv.Poly(pts, r, g, b);
    auto e1 = P(-s * 0.34f, s * 0.08f), e2 = P(s * 0.34f, s * 0.08f);
    cv.Circle(e1.first, e1.second, s * 0.13f, r, g, b); cv.Circle(e2.first, e2.second, s * 0.13f, r, g, b);
}
} // namespace

std::shared_ptr<OverlayImage> RenderPhone(const PhoneView& v, int screenH, std::vector<PhoneSpot>* spots) {
    auto thp = ThemeFor(v.design);
    const CardTheme& th = *thp;
    FontScope fontScope(th.font.c_str());
    const float u = std::max(0.3f, screenH * 0.62f / 840.f);
    auto U = [&](float x) { return (int)std::lround(x * u); };
    const int W = U(400), H = U(840);
    auto img = NewCard(W, H);
    img->renderedForScreenHeight = screenH;   // drawn 1:1 on this screen height (not stretched to it)
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const float AR = th.accent[0], AG = th.accent[1], AB = th.accent[2];
    auto spot = [&](int x0, int y0, int x1, int y1, int act, int idx = -1) { if (spots) spots->push_back(PhoneSpot{RECT{x0, y0, x1, y1}, act, idx}); };
    const bool e = v.en;

    // body: dark metal frame, black glass, screen
    cv.Round(0, 0, W, H, (float)U(58), 0.22f, 0.23f, 0.25f);
    cv.Round(U(3), U(3), W - U(6), H - U(6), (float)U(55), 0.02f, 0.02f, 0.025f);
    const int sx0 = U(14), sy0 = U(14), sx1 = W - U(14), sy1 = H - U(14), sw = sx1 - sx0, sh = sy1 - sy0;
    const bool home = v.screen == PhoneScreen::Home, call = v.screen == PhoneScreen::Incoming || v.screen == PhoneScreen::InCall;
    {   // screen background: wallpaper on the home screen and during calls, dark elsewhere
        float top[3], bot[3];
        if (home) { for (int k = 0; k < 3; ++k) { top[k] = th.accent[k] * 0.55f + 0.05f; bot[k] = th.bgBottom[k] * 0.6f; } }
        else if (call) { top[0] = 0.10f; top[1] = 0.16f; top[2] = 0.14f; bot[0] = 0.02f; bot[1] = 0.03f; bot[2] = 0.03f; }
        else { top[0] = top[1] = 0.055f; top[2] = 0.065f; bot[0] = bot[1] = 0.04f; bot[2] = 0.045f; }
        for (int y = 0; y < sh; ++y) {
            float t = (float)y / sh;
            float r = top[0] + (bot[0] - top[0]) * t, g = top[1] + (bot[1] - top[1]) * t, b = top[2] + (bot[2] - top[2]) * t;
            for (int x = 0; x < sw; ++x) {
                float cov = RoundedCoverage(x, y, sw, sh, (float)U(44));
                if (cov > 0) cv.Blend(sx0 + x, sy0 + y, cov, r, g, b);
            }
        }
        if (home) cv.Glow(sx0 + sw * 0.3f, sy0 + sh * 0.25f, sw * 0.9f, AR, AG, AB, 0.25f);
    }
    // status bar: clock, island, Bluetooth + signal + battery
    HFONT fStat = MakeFont(U(17), FW_SEMIBOLD);
    DrawText(mask, px, fStat, v.clock, RECT{sx0 + U(34), sy0 + U(10), sx0 + U(130), sy0 + U(40)}, 0.97f, 0.97f, 0.98f);
    cv.Round(W / 2 - U(56), sy0 + U(10), U(112), U(32), (float)U(16), 0.f, 0.f, 0.f);   // island
    {
        int bx = sx1 - U(40) - U(28), by = sy0 + U(18);
        cv.Round(bx, by, U(26), U(13), (float)U(4), 0.97f, 0.97f, 0.98f, 0.9f);   // battery
        cv.Round(bx + U(2), by + U(2), U(18), U(9), (float)U(2), 0.30f, 0.85f, 0.40f);
        cv.Round(bx + U(26), by + U(4), U(2), U(5), 1.f, 0.97f, 0.97f, 0.98f, 0.9f);
        for (int i = 0; i < 4; ++i) cv.Round(bx - U(38) + i * U(6), by + U(10) - U(3 + i * 3), U(4), U(3 + i * 3), 1.f, 0.97f, 0.97f, 0.98f);   // signal
    }
    DeleteObject(fStat);
    const int contentTop = sy0 + U(58), contentBot = sy1 - U(36);
    HFONT fHead = MakeFont(U(22), FW_SEMIBOLD), fTitle = MakeFont(U(18), FW_SEMIBOLD), fText = MakeFont(U(15), FW_NORMAL), fSmall = MakeFont(U(13), FW_NORMAL);
    const float TR = 0.96f, TG = 0.96f, TB = 0.97f, MR = 0.58f, MG = 0.60f, MB = 0.64f;
    auto header = [&](const std::wstring& title) {
        int y = contentTop;
        cv.Line((float)sx0 + U(30), y + U(22), (float)sx0 + U(22), y + U(30), (float)U(3), AR, AG, AB);   // back chevron
        cv.Line((float)sx0 + U(22), y + U(30), (float)sx0 + U(30), y + U(38), (float)U(3), AR, AG, AB);
        spot(sx0, y, sx0 + U(90), y + U(56), PA_Back);
        DrawText(mask, px, fHead, title, RECT{sx0 + U(60), y + U(10), sx1 - U(60), y + U(50)}, TR, TG, TB, DT_CENTER);
        cv.Round(sx0 + U(16), y + U(58), sw - U(32), std::max(1, U(1)), 0, 1.f, 1.f, 1.f, 0.08f);
        return y + U(66);
    };
    auto hint = [&](const std::wstring& t) { DrawText(mask, px, fSmall, t, RECT{sx0 + U(20), contentBot - U(24), sx1 - U(20), contentBot}, MR, MG, MB, DT_CENTER); };
    auto avatar = [&](float cx, float cy, float r, const std::wstring& name) {
        cv.Circle(cx, cy, r, 0.36f, 0.38f, 0.43f);
        HFONT f = MakeFont((int)(r * 0.9f), FW_SEMIBOLD);
        DrawText(mask, px, f, Initials(name), RECT{(int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r)}, 0.97f, 0.97f, 0.98f, DT_CENTER);
        DeleteObject(f);
    };
    auto appIcon = [&](int k, int x, int y, int s) {   // 0 messages, 1 phone, 2 job, 3 music
        const float col[4][3] = {{0.20f, 0.78f, 0.35f}, {0.20f, 0.60f, 0.98f}, {1.f, 0.62f, 0.04f}, {0.98f, 0.25f, 0.38f}};
        cv.Round(x, y, s, s, s * 0.24f, col[k][0], col[k][1], col[k][2]);
        cv.Round(x, y, s, s / 2, s * 0.24f, 1.f, 1.f, 1.f, 0.10f);
        float cx = x + s / 2.f, cy = y + s / 2.f;
        if (k == 0) {
            cv.Round((int)(cx - s * 0.30f), (int)(cy - s * 0.24f), (int)(s * 0.60f), (int)(s * 0.42f), s * 0.18f, 1.f, 1.f, 1.f);
            cv.Poly({{cx - s * 0.18f, cy + s * 0.12f}, {cx - s * 0.24f, cy + s * 0.28f}, {cx - s * 0.04f, cy + s * 0.14f}}, 1.f, 1.f, 1.f);
        } else if (k == 1) Handset(cv, cx, cy, s * 0.62f, -0.75f, 1.f, 1.f, 1.f);
        else if (k == 2) {   // package
            cv.Round((int)(cx - s * 0.28f), (int)(cy - s * 0.22f), (int)(s * 0.56f), (int)(s * 0.46f), s * 0.05f, 1.f, 1.f, 1.f);
            cv.Round((int)(cx - s * 0.28f), (int)(cy - s * 0.22f), (int)(s * 0.56f), (int)(s * 0.08f), 0.f, col[2][0] * 0.8f, col[2][1] * 0.8f, col[2][2] * 0.8f);
            cv.Round((int)(cx - s * 0.04f), (int)(cy - s * 0.22f), (int)(s * 0.08f), (int)(s * 0.20f), 0.f, col[2][0] * 0.8f, col[2][1] * 0.8f, col[2][2] * 0.8f);
        } else {             // note
            cv.Circle(cx - s * 0.10f, cy + s * 0.16f, s * 0.12f, 1.f, 1.f, 1.f);
            cv.Round((int)(cx - s * 0.01f), (int)(cy - s * 0.26f), (int)(s * 0.07f), (int)(s * 0.44f), 1.f, 1.f, 1.f, 1.f);
            cv.Poly({{cx + s * 0.05f, cy - s * 0.26f}, {cx + s * 0.24f, cy - s * 0.16f}, {cx + s * 0.24f, cy - s * 0.06f}, {cx + s * 0.05f, cy - s * 0.14f}}, 1.f, 1.f, 1.f);
        }
    };
    auto rowSel = [&](int i, int y0, int y1) { if (i == v.sel) cv.Round(sx0 + U(8), y0, sw - U(16), y1 - y0, (float)U(12), 1.f, 1.f, 1.f, 0.08f); };

    switch (v.screen) {
    case PhoneScreen::Home: {
        HFONT fClock = MakeFont(U(76), FW_LIGHT), fDay = MakeFont(U(20), FW_NORMAL), fLabel = MakeFont(U(15), FW_NORMAL), fBadge = MakeFont(U(14), FW_BOLD);
        DrawText(mask, px, fDay, v.day, RECT{sx0, contentTop + U(20), sx1, contentTop + U(50)}, 0.92f, 0.92f, 0.94f, DT_CENTER);
        DrawText(mask, px, fClock, v.clock, RECT{sx0, contentTop + U(48), sx1, contentTop + U(140)}, 0.98f, 0.98f, 0.99f, DT_CENTER);
        if (!v.device.empty()) DrawText(mask, px, fSmall, v.device, RECT{sx0, contentTop + U(140), sx1, contentTop + U(164)}, 0.80f, 0.82f, 0.86f, DT_CENTER);
        const wchar_t* lde[] = {L"Nachrichten", L"Telefon", L"Auftrag", L"Musik"}, *len[] = {L"Messages", L"Phone", L"Job", L"Music"};
        const int s = U(86);
        for (int k = 0; k < 4; ++k) {
            int cx = sx0 + sw / 2 + (k % 2 ? U(70) : -U(70)), y = contentTop + U(230) + (k / 2) * U(150), x = cx - s / 2;
            if (k == v.sel) cv.Round(x - U(8), y - U(8), s + U(16), s + U(44), (float)U(26), 1.f, 1.f, 1.f, 0.14f);
            appIcon(k, x, y, s);
            DrawText(mask, px, fLabel, e ? len[k] : lde[k], RECT{x - U(30), y + s + U(6), x + s + U(30), y + s + U(28)}, 0.96f, 0.96f, 0.97f, DT_CENTER);
            int badge = k == 0 ? v.unreadMessages : k == 1 ? v.missedCalls : 0;
            if (badge > 0) {
                float bx = x + s - U(6), by = y + U(6);
                cv.Circle(bx, by, (float)U(13), 1.f, 0.23f, 0.19f);
                DrawText(mask, px, fBadge, std::to_wstring(std::min(badge, 99)), RECT{(int)bx - U(13), (int)by - U(13), (int)bx + U(13), (int)by + U(13)}, 1.f, 1.f, 1.f, DT_CENTER);
            }
            spot(x - U(16), y - U(10), x + s + U(16), y + s + U(30), PA_App, k);
        }
        DeleteObject(fClock); DeleteObject(fDay); DeleteObject(fLabel); DeleteObject(fBadge);
        break; }
    case PhoneScreen::Messages: case PhoneScreen::Calls: {
        int y = header(v.title);
        const int rowH = U(72), vis = std::max(1, (contentBot - U(30) - y) / rowH);
        int first = std::clamp(std::max(v.scroll, v.sel - vis + 1), 0, std::max(0, (int)v.items.size() - vis));
        for (int i = first; i < (int)v.items.size() && i < first + vis; ++i, y += rowH) {
            const PhoneListItem& it = v.items[i];
            rowSel(i, y + U(2), y + rowH - U(2));
            int x0 = sx0 + U(28);
            if (v.screen == PhoneScreen::Calls) {   // arrow: incoming (green) / missed, declined (red)
                bool bad = it.kind != 1;
                float r = bad ? 1.f : 0.30f, g = bad ? 0.30f : 0.85f, b = bad ? 0.28f : 0.40f, cx = x0 + U(6), cy = y + rowH / 2.f;
                cv.Line(cx + U(6), cy - U(6), cx - U(5), cy + U(5), (float)U(2.5f), r, g, b);
                cv.Line(cx - U(5), cy + U(5), cx - U(5), cy - U(1), (float)U(2.5f), r, g, b); cv.Line(cx - U(5), cy + U(5), cx + U(1), cy + U(5), (float)U(2.5f), r, g, b);
                x0 += U(24);
            } else if (it.unread) cv.Circle((float)sx0 + U(17), y + rowH / 2.f, (float)U(5), AR, AG, AB);
            const bool red = v.screen == PhoneScreen::Calls && it.kind == 0;
            DrawText(mask, px, fTitle, it.title, RECT{x0, y + U(10), sx1 - U(90), y + U(36)}, red ? 1.f : TR, red ? 0.36f : TG, red ? 0.33f : TB);
            DrawText(mask, px, fSmall, it.time, RECT{sx1 - U(110), y + U(12), sx1 - U(22), y + U(34)}, MR, MG, MB, DT_RIGHT);
            DrawText(mask, px, fText, it.text, RECT{x0, y + U(38), sx1 - U(22), y + U(62)}, MR, MG, MB);
            cv.Round(x0, y + rowH - 1, sx1 - U(16) - x0, 1, 0, 1.f, 1.f, 1.f, 0.06f);
            spot(sx0, y, sx1, y + rowH, PA_Item, i);
        }
        if (!v.hint.empty()) DrawText(mask, px, fText, v.hint, RECT{sx0, y + U(60), sx1, y + U(90)}, MR, MG, MB, DT_CENTER);
        break; }
    case PhoneScreen::Thread: {
        int top = header(v.title) + U(8);
        const int maxW = sw - U(110);
        std::vector<int> hs;
        for (auto& b : v.bubbles) hs.push_back(WrapHeight(mdc, fText, b, maxW - U(28)) + U(24));
        int y = contentBot - U(10);   // newest at the bottom
        for (int i = (int)v.bubbles.size() - 1; i >= 0 && y - hs[i] - U(20) > top; --i) {
            int bh = hs[i], bw = std::min(maxW, std::max(U(80), (int)Measure(mdc, fText, v.bubbles[i]).cx + U(28)));
            y -= U(18);
            DrawText(mask, px, fSmall, v.bubbleTimes[i], RECT{sx0 + U(26), y, sx1, y + U(16)}, MR, MG, MB);
            y -= bh + U(2);
            cv.Round(sx0 + U(20), y, bw, bh, (float)U(18), 0.17f, 0.18f, 0.20f);
            DrawWrap(mask, px, fText, v.bubbles[i], RECT{sx0 + U(34), y + U(12), sx0 + U(20) + bw - U(14), y + bh - U(8)}, TR, TG, TB);
            y -= U(8);
        }
        break; }
    case PhoneScreen::CallDetail: {
        int y = header(v.title);
        avatar(W / 2.f, y + U(70), (float)U(46), v.caller);
        DrawText(mask, px, fHead, v.caller, RECT{sx0, y + U(124), sx1, y + U(156)}, TR, TG, TB, DT_CENTER);
        DrawText(mask, px, fSmall, v.callState, RECT{sx0, y + U(156), sx1, y + U(176)}, MR, MG, MB, DT_CENTER);
        int ty = y + U(196), th2 = std::min(WrapHeight(mdc, fText, v.callText, sw - U(76)) + U(28), contentBot - U(110) - ty);
        cv.Round(sx0 + U(20), ty, sw - U(40), th2, (float)U(18), 0.12f, 0.13f, 0.15f);
        DrawWrap(mask, px, fText, v.callText, RECT{sx0 + U(38), ty + U(14), sx1 - U(38), ty + th2 - U(10)}, TR, TG, TB);
        int by = contentBot - U(86);
        cv.Round(sx0 + U(60), by, sw - U(120), U(48), (float)U(24), AR, AG, AB);
        DrawText(mask, px, fTitle, e ? L"Play again" : L"Nochmal anhören", RECT{sx0 + U(60), by, sx1 - U(60), by + U(48)}, th.chipText[0], th.chipText[1], th.chipText[2], DT_CENTER);
        spot(sx0 + U(60), by, sx1 - U(60), by + U(48), PA_Replay);
        break; }
    case PhoneScreen::Incoming: case PhoneScreen::InCall: {
        HFONT fName = MakeFont(U(32), FW_NORMAL);
        DrawText(mask, px, fName, v.caller, RECT{sx0 + U(20), contentTop + U(50), sx1 - U(20), contentTop + U(96)}, TR, TG, TB, DT_CENTER);
        DrawText(mask, px, fText, v.callState, RECT{sx0, contentTop + U(98), sx1, contentTop + U(124)}, 0.80f, 0.82f, 0.86f, DT_CENTER);
        avatar(W / 2.f, contentTop + U(220), (float)U(62), v.caller);
        if (v.screen == PhoneScreen::InCall && !v.callText.empty()) {   // what is said, as subtitles
            int ty = contentTop + U(310), th2 = std::min(WrapHeight(mdc, fText, v.callText, sw - U(76)) + U(28), contentBot - U(150) - ty);
            cv.Round(sx0 + U(20), ty, sw - U(40), th2, (float)U(18), 1.f, 1.f, 1.f, 0.07f);
            DrawWrap(mask, px, fText, v.callText, RECT{sx0 + U(38), ty + U(14), sx1 - U(38), ty + th2 - U(10)}, TR, TG, TB);
        }
        const int by = contentBot - U(120), bs = U(78);
        auto button = [&](int cx, bool green, float angle, const std::wstring& label, int act, bool selected) {
            if (selected) cv.Circle((float)cx, by + bs / 2.f, bs / 2.f + U(6), 1.f, 1.f, 1.f, 0.25f);
            cv.Circle((float)cx, by + bs / 2.f, bs / 2.f, green ? 0.20f : 1.f, green ? 0.78f : 0.23f, green ? 0.35f : 0.19f);
            Handset(cv, (float)cx, by + bs / 2.f, bs * 0.5f, angle, 1.f, 1.f, 1.f);
            DrawText(mask, px, fSmall, label, RECT{cx - U(70), by + bs + U(6), cx + U(70), by + bs + U(26)}, 0.92f, 0.92f, 0.94f, DT_CENTER);
            spot(cx - bs / 2 - U(8), by - U(8), cx + bs / 2 + U(8), by + bs + U(28), act);
        };
        if (v.screen == PhoneScreen::Incoming) {
            button(sx0 + sw / 4, false, 0.f, e ? L"Decline" : L"Ablehnen", PA_Decline, v.sel == 1);
            button(sx0 + sw * 3 / 4, true, -0.75f, e ? L"Answer" : L"Annehmen", PA_Accept, v.sel == 0);
        } else button(sx0 + sw / 2, false, 0.f, e ? L"End" : L"Auflegen", PA_Hangup, false);
        DeleteObject(fName);
        break; }
    case PhoneScreen::Job: {
        int y = header(v.title);
        if (!v.jobActive) {
            DrawText(mask, px, fHead, e ? L"No job" : L"Kein Auftrag", RECT{sx0, y + U(120), sx1, y + U(160)}, TR, TG, TB, DT_CENTER);
            DrawText(mask, px, fText, e ? L"Take a job and it shows up here" : L"Nimm einen Auftrag an – er erscheint hier", RECT{sx0 + U(20), y + U(164), sx1 - U(20), y + U(190)}, MR, MG, MB, DT_CENTER);
            break;
        }
        DrawText(mask, px, fHead, v.cargo, RECT{sx0 + U(24), y + U(6), sx1 - U(24), y + U(38)}, TR, TG, TB);
        // route: start - destination with a line
        int ry = y + U(46);
        cv.Circle((float)sx0 + U(32), ry + U(12), (float)U(6), 0.35f, 0.65f, 1.f);
        cv.Round(sx0 + U(31), ry + U(18), U(2), U(30), 0, 1.f, 1.f, 1.f, 0.25f);
        cv.Circle((float)sx0 + U(32), ry + U(56), (float)U(6), 1.f, 0.30f, 0.28f);
        DrawText(mask, px, fTitle, v.from, RECT{sx0 + U(50), ry, sx1 - U(20), ry + U(24)}, TR, TG, TB);
        DrawText(mask, px, fSmall, v.fromCompany, RECT{sx0 + U(50), ry + U(22), sx1 - U(20), ry + U(40)}, MR, MG, MB);
        DrawText(mask, px, fTitle, v.to, RECT{sx0 + U(50), ry + U(44), sx1 - U(20), ry + U(68)}, TR, TG, TB);
        DrawText(mask, px, fSmall, v.toCompany, RECT{sx0 + U(50), ry + U(66), sx1 - U(20), ry + U(84)}, MR, MG, MB);
        // facts: 2 x 2
        const wchar_t* lab[4] = {e ? L"Time left" : L"Restzeit", e ? L"Distance" : L"Entfernung", e ? L"Arrival" : L"Ankunft", e ? L"Pay" : L"Lohn"};
        const std::wstring* val[4] = {&v.remaining, &v.distance, &v.eta, &v.income};
        int fy = ry + U(96);
        for (int k = 0; k < 4; ++k) {
            int cx0 = sx0 + U(18) + (k % 2) * (sw - U(36)) / 2, cy0 = fy + (k / 2) * U(62), cw = (sw - U(36)) / 2 - U(8);
            cv.Round(cx0, cy0, cw, U(54), (float)U(14), 1.f, 1.f, 1.f, 0.06f);
            DrawText(mask, px, fSmall, lab[k], RECT{cx0 + U(12), cy0 + U(6), cx0 + cw - U(8), cy0 + U(24)}, MR, MG, MB);
            bool late = k == 0 && v.late;
            DrawText(mask, px, fTitle, *val[k], RECT{cx0 + U(12), cy0 + U(24), cx0 + cw - U(8), cy0 + U(50)}, late ? 1.f : TR, late ? 0.36f : TG, late ? 0.33f : TB);
        }
        // map
        int mx0 = sx0 + U(18), my0 = fy + U(132), mx1 = sx1 - U(18), my1 = contentBot - U(8);
        if (v.haveMap && my1 - my0 > U(80)) {
            const int mw = mx1 - mx0, mh = my1 - my0;
            for (int yy = 0; yy < mh; ++yy) for (int xx = 0; xx < mw; ++xx) {
                float cov = RoundedCoverage(xx, yy, mw, mh, (float)U(16));
                if (cov > 0) cv.Blend(mx0 + xx, my0 + yy, cov, 0.10f, 0.13f, 0.17f);
            }
            for (int gx = 1; gx < 4; ++gx) cv.Round(mx0 + mw * gx / 4, my0 + U(4), 1, mh - U(8), 0, 1.f, 1.f, 1.f, 0.04f);
            for (int gy = 1; gy < 4; ++gy) cv.Round(mx0 + U(4), my0 + mh * gy / 4, mw - U(8), 1, 0, 1.f, 1.f, 1.f, 0.04f);
            auto MP = [&](float x, float y2) { return std::make_pair(mx0 + U(10) + x * (mw - U(20)), my0 + U(10) + y2 * (mh - U(20))); };
            const PhoneMapPoint* dest = nullptr;
            for (auto& p : v.map) if (p.kind == 1) dest = &p;
            if (dest) {   // dashed line truck -> destination
                auto a = MP(v.truckX, v.truckY), b = MP(dest->x, dest->y);
                float len = std::hypot(b.first - a.first, b.second - a.second); int n = std::max(1, (int)(len / U(10)));
                for (int i = 0; i < n; i += 2) {
                    float t0 = (float)i / n, t1 = std::min(1.f, (float)(i + 1) / n);
                    cv.Line(a.first + (b.first - a.first) * t0, a.second + (b.second - a.second) * t0, a.first + (b.first - a.first) * t1, a.second + (b.second - a.second) * t1, (float)U(2.5f), AR, AG, AB);
                }
            }
            for (auto& p : v.map) {
                auto q = MP(p.x, p.y);
                if (p.kind == 0) {
                    cv.Circle(q.first, q.second, (float)U(2.5f), 0.65f, 0.68f, 0.74f);
                    if (!p.name.empty()) DrawText(mask, px, fSmall, p.name, RECT{(int)q.first + U(5), (int)q.second - U(9), (int)q.first + U(130), (int)q.second + U(9)}, 0.55f, 0.58f, 0.64f);
                } else {
                    bool d = p.kind == 1;
                    cv.Circle(q.first, q.second - U(9), (float)U(8), d ? 1.f : 0.35f, d ? 0.30f : 0.65f, d ? 0.28f : 1.f);
                    cv.Poly({{q.first - U(6), q.second - U(6)}, {q.first + U(6), q.second - U(6)}, {q.first, q.second + U(2)}}, d ? 1.f : 0.35f, d ? 0.30f : 0.65f, d ? 0.28f : 1.f);
                    cv.Circle(q.first, q.second - U(9), (float)U(3), 1.f, 1.f, 1.f);
                    DrawText(mask, px, fSmall, p.name, RECT{(int)q.first + U(10), (int)q.second - U(18), (int)q.first + U(150), (int)q.second}, TR, TG, TB);
                }
            }
            {   // truck: arrow in the driving direction
                auto c = MP(v.truckX, v.truckY);
                float dx = -std::sin(v.truckHeading), dy = -std::cos(v.truckHeading), s = (float)U(11);
                cv.Circle(c.first, c.second, (float)U(14), AR, AG, AB, 0.25f);
                cv.Poly({{c.first + dx * s, c.second + dy * s}, {c.first - dx * s * 0.6f - dy * s * 0.6f, c.second - dy * s * 0.6f + dx * s * 0.6f},
                         {c.first - dx * s * 0.25f, c.second - dy * s * 0.25f}, {c.first - dx * s * 0.6f + dy * s * 0.6f, c.second - dy * s * 0.6f - dx * s * 0.6f}}, AR, AG, AB);
            }
            DrawText(mask, px, fSmall, v.mapScale, RECT{mx0 + U(12), my1 - U(24), mx1, my1 - U(4)}, MR, MG, MB);
        }
        break; }
    case PhoneScreen::Music: {
        int y = header(v.title);
        if (v.musicActive) {
            int art = U(150), ax = W / 2 - art / 2;
            cv.Round(ax, y + U(8), art, art, (float)U(20), 0.20f, 0.45f, 0.95f);
            {   // Bluetooth rune
                float cx = ax + art / 2.f, cy = y + U(8) + art / 2.f, s = art * 0.42f, w = (float)U(5);
                cv.Line(cx, cy - s / 2, cx, cy + s / 2, w, 1.f, 1.f, 1.f);
                cv.Line(cx, cy - s / 2, cx + s * 0.25f, cy - s / 4, w, 1.f, 1.f, 1.f); cv.Line(cx + s * 0.25f, cy - s / 4, cx - s * 0.25f, cy + s / 4, w, 1.f, 1.f, 1.f);
                cv.Line(cx, cy + s / 2, cx + s * 0.25f, cy + s / 4, w, 1.f, 1.f, 1.f); cv.Line(cx + s * 0.25f, cy + s / 4, cx - s * 0.25f, cy - s / 4, w, 1.f, 1.f, 1.f);
            }
            int ty = y + U(170);
            DrawText(mask, px, fHead, v.musicTitle, RECT{sx0 + U(24), ty, sx1 - U(24), ty + U(32)}, TR, TG, TB, DT_CENTER);
            DrawText(mask, px, fText, v.musicArtist, RECT{sx0 + U(24), ty + U(32), sx1 - U(24), ty + U(54)}, MR, MG, MB, DT_CENTER);
            int py = ty + U(70), bx0 = sx0 + U(30), bx1 = sx1 - U(30);
            cv.Round(bx0, py, bx1 - bx0, U(4), (float)U(2), 1.f, 1.f, 1.f, 0.15f);
            if (v.musicLen > 0 && v.musicPos >= 0) cv.Round(bx0, py, std::max(U(4), (int)((bx1 - bx0) * std::min(1.f, (float)v.musicPos / v.musicLen))), U(4), (float)U(2), TR, TG, TB);
            DrawText(mask, px, fSmall, Clock(v.musicPos), RECT{bx0, py + U(8), bx0 + U(80), py + U(26)}, MR, MG, MB);
            DrawText(mask, px, fSmall, Clock(v.musicLen), RECT{bx1 - U(80), py + U(8), bx1, py + U(26)}, MR, MG, MB, DT_RIGHT);
            int cy = py + U(62), cx = W / 2;
            // previous | play/pause | next
            auto tri = [&](float x, float yy, float s, float dir) { cv.Poly({{x - dir * s * 0.5f, yy - s * 0.55f}, {x + dir * s * 0.5f, yy}, {x - dir * s * 0.5f, yy + s * 0.55f}}, TR, TG, TB); };
            tri(cx - U(92), (float)cy, (float)U(18), -1); tri(cx - U(74), (float)cy, (float)U(18), -1);
            tri(cx + U(74), (float)cy, (float)U(18), 1); tri(cx + U(92), (float)cy, (float)U(18), 1);
            if (v.musicPaused) tri((float)cx + U(4), (float)cy, (float)U(30), 1);
            else { cv.Round(cx - U(13), cy - U(17), U(9), U(34), (float)U(2), TR, TG, TB); cv.Round(cx + U(4), cy - U(17), U(9), U(34), (float)U(2), TR, TG, TB); }
            spot(cx - U(120), cy - U(26), cx - U(50), cy + U(26), PA_Prev);
            spot(cx - U(36), cy - U(30), cx + U(36), cy + U(30), PA_PlayPause);
            spot(cx + U(50), cy - U(26), cx + U(120), cy + U(26), PA_Next);
            y = cy + U(44);
        }
        if (!v.musicFolders.empty()) {
            DrawText(mask, px, fSmall, e ? L"FOLDERS" : L"ORDNER", RECT{sx0 + U(24), y, sx1, y + U(20)}, MR, MG, MB);
            y += U(24);
            const int rowH = U(48);
            for (int i = 0; i < (int)v.musicFolders.size() && y + rowH < contentBot - U(10); ++i, y += rowH) {
                rowSel(i, y, y + rowH);
                bool playing = i == v.musicFolderPlaying;
                DrawText(mask, px, fTitle, v.musicFolders[i], RECT{sx0 + U(28), y, sx1 - U(50), y + rowH}, playing ? AR : TR, playing ? AG : TG, playing ? AB : TB);
                if (playing) DrawText(mask, px, fTitle, L"♪", RECT{sx1 - U(50), y, sx1 - U(22), y + rowH}, AR, AG, AB, DT_RIGHT);
                spot(sx0, y, sx1, y + rowH, PA_Folder, i);
            }
        } else DrawWrap(mask, px, fText, v.hint, RECT{sx0 + U(30), y + U(40), sx1 - U(30), y + U(140)}, MR, MG, MB);
        break; }
    }
    if (v.screen == PhoneScreen::Home && !v.hint.empty()) hint(v.hint);
    // home bar
    cv.Round(W / 2 - U(60), sy1 - U(16), U(120), U(5), (float)U(2.5f), 1.f, 1.f, 1.f, 0.55f);
    DeleteObject(fHead); DeleteObject(fTitle); DeleteObject(fText); DeleteObject(fSmall);
    return img;
}

std::shared_ptr<OverlayImage> ComposeSongLine(const OverlayImage& card, const SongLine& s, int offset) {
    auto img = std::make_shared<OverlayImage>(card);
    if (!s.strip || s.w <= 0 || s.h <= 0) return img;
    const OverlayImage& st = *s.strip;
    const bool scroll = st.width > s.w;
    const int period = st.width + s.h * 3;            // the title, then a gap, then the title again
    const int fade = scroll ? std::max(1, s.h / 2) : 0;   // soft edges while it scrolls
    for (int y = 0; y < s.h && y < st.height; ++y) {
        int dy = s.y + y;
        if (dy < 0 || dy >= img->height) continue;
        for (int x = 0; x < s.w; ++x) {
            int dx = s.x + x;
            if (dx < 0 || dx >= img->width) continue;
            int sx = scroll ? ((x + offset) % period + period) % period : x;
            if (sx >= st.width) continue;
            uint32_t p = st.pixels[(size_t)y * st.width + sx];
            float a = ((p >> 24) & 255) / 255.f;
            if (a <= 0.f) continue;
            float k = 1.f;
            if (fade) k = std::min({1.f, (x + 0.5f) / fade, (s.w - x - 0.5f) / fade});
            Over(img->pixels[(size_t)dy * img->width + dx], a * k, ((p >> 16) & 255) / 255.f * k, ((p >> 8) & 255) / 255.f * k, (p & 255) / 255.f * k);
        }
    }
    return img;
}

std::shared_ptr<OverlayImage> RenderScreenProbe(const std::wstring& label, int W, int H) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;
    const int cs = std::max(8, std::min(W, H) / 8);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)   // magenta / dark checkerboard: impossible to miss
        px[(size_t)y * W + x] = ((x / cs + y / cs) & 1) ? Pack(1.f, 1.f, 0.f, 0.85f) : Pack(1.f, 0.12f, 0.f, 0.15f);
    Gdi mask(W, H);
    HFONT f = MakeFont(std::max(10, H / 7), FW_BOLD), fs = MakeFont(std::max(8, H / 12), FW_BOLD);
    FillRect(px, W, H, 0, H / 3, W, 2 * H / 3, 1.f, 0.f, 0.f, 0.f);
    DrawText(mask, px, f, label, RECT{0, H / 3, W, 2 * H / 3}, 1.f, 1.f, 0.2f, DT_CENTER);
    DrawText(mask, px, fs, L"▲ TOP ▲", RECT{0, 0, W, H / 6}, 1.f, 1.f, 1.f, DT_CENTER);
    DeleteObject(f); DeleteObject(fs);
    return img;
}

std::shared_ptr<OverlayImage> RenderScreenGrid(int W, int H, bool flipX, bool flipY) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, Pack(1.f, 0.05f, 0.05f, 0.08f));
    auto& px = img->pixels;
    Gdi mask(W, H);
    HFONT f = MakeFont(std::max(8, std::min(W / 10, H / 10) * 2 / 5), FW_BOLD);
    for (int cy = 0; cy < 10; ++cy) for (int cx = 0; cx < 10; ++cx) {
        int x0 = W * cx / 10, x1 = W * (cx + 1) / 10, y0 = H * cy / 10, y1 = H * (cy + 1) / 10;
        if ((cx + cy) & 1) FillRect(px, W, H, x0, y0, x1, y1, 1.f, 0.10f, 0.10f, 0.16f);
        // label = position in the render target (the image is mirrored when it is drawn, so undo that here)
        int lx = flipX ? 9 - cx : cx, ly = flipY ? 9 - cy : cy;
        wchar_t t[16]; swprintf(t, 16, L"%d,%d", lx, ly);
        bool corner = (lx == 0 && ly == 0);
        DrawText(mask, px, f, t, RECT{x0, y0, x1, y1}, corner ? 1.f : 0.2f, 1.f, corner ? 0.2f : 1.f, DT_CENTER);
    }
    for (int i = 0; i <= 10; ++i) {   // grid lines, thicker every 50 %
        int t = (i % 5 == 0) ? 3 : 1;
        int x = std::min(W - t, W * i / 10), y = std::min(H - t, H * i / 10);
        FillRect(px, W, H, x, 0, x + t, H, 1.f, 1.f, 0.f, 0.85f);
        FillRect(px, W, H, 0, y, W, y + t, 1.f, 1.f, 0.f, 0.85f);
    }
    DeleteObject(f);
    return img;
}


// ------------------------------------------------------------------------------------------------
// Settings menu (Ctrl+0). Layout is computed in one place and used by both the renderer and the hit test.
namespace {
struct MenuLayout {
    float s = 1;
    int W = 0, H = 0, headH = 0, sideW = 0, x0 = 0, x1 = 0, itemsY = 0, rowH = 0, footY = 0;
    RECT close{}, save{}, reset{}, def{};
    bool keys = false;
    int first = 0, visible = 1;
    int S(float v) const { return std::max(1, (int)std::lround(v * s)); }
    RECT Page(int i) const { return RECT{S(14), S(98) + i * S(44), sideW - S(14), S(98) + i * S(44) + S(38)}; }
    RECT Row(int i) const { i -= first; return RECT{x0 - S(12), itemsY + i * rowH, x1 + S(8), itemsY + (i + 1) * rowH}; }
    bool Shown(int i) const { return i >= first && i < first + visible; }
    RECT Button(int i) const { RECT r = Row(i); int cy = (r.top + r.bottom) / 2; return RECT{CtlX1() - S(190), cy - S(18), CtlX1(), cy + S(18)}; }
    int CtlX0() const { return x0 + (x1 - x0) * 52 / 100; }
    int CtlX1() const { return x1 - S(6); }
    RECT Toggle(int i) const { RECT r = Row(i); int cy = (r.top + r.bottom) / 2; return RECT{CtlX1() - S(54), cy - S(15), CtlX1(), cy + S(15)}; }
    RECT Track(int i) const { RECT r = Row(i); int cy = (r.top + r.bottom) / 2; return RECT{CtlX0(), cy - S(12), CtlX1() - S(86), cy + S(12)}; }
    RECT Prev(int i) const { RECT r = Row(i); int cy = (r.top + r.bottom) / 2; return RECT{CtlX0(), cy - S(16), CtlX0() + S(34), cy + S(16)}; }
    RECT Next(int i) const { RECT r = Row(i); int cy = (r.top + r.bottom) / 2; return RECT{CtlX1() - S(34), cy - S(16), CtlX1(), cy + S(16)}; }
    RECT KeyCell(int i) const { RECT r = Row(i); return RECT{x0 + (x1 - x0) * 48 / 100, r.top + S(3), x0 + (x1 - x0) * 77 / 100, r.bottom - S(3)}; }
    RECT BtnCell(int i) const { RECT r = Row(i); return RECT{x0 + (x1 - x0) * 79 / 100, r.top + S(3), x1, r.bottom - S(3)}; }
};
MenuLayout Layout(const MenuView& v, int screenH) {
    MenuLayout L;
    L.s = (float)std::clamp(screenH / 1080.0, 0.5, 4.0);
    L.W = L.S(1120); L.H = L.S(720); L.headH = L.S(80); L.sideW = L.S(232);
    L.x0 = L.sideW + L.S(26); L.x1 = L.W - L.S(30);
    L.keys = !v.items.empty() && v.items[0].kind == MenuItemKind::Key;
    L.itemsY = L.headH + L.S(46) + (L.keys ? L.S(26) : 0);
    L.rowH = L.keys ? L.S(34) : L.S(54);
    L.footY = L.H - L.S(72);
    L.visible = std::max(1, (L.footY - L.S(8) - L.itemsY) / L.rowH);
    L.first = std::clamp(v.firstRow, 0, std::max(0, (int)v.items.size() - L.visible));
    L.close = RECT{L.W - L.S(64), L.S(20), L.W - L.S(26), L.S(58)};
    int by0 = L.footY + L.S(16), by1 = L.footY + L.S(56);
    L.save = RECT{L.W - L.S(30) - L.S(190), by0, L.W - L.S(30), by1};
    L.reset = RECT{L.save.left - L.S(12) - L.S(170), by0, L.save.left - L.S(12), by1};
    L.def = RECT{L.reset.left - L.S(12) - L.S(140), by0, L.reset.left - L.S(12), by1};
    return L;
}
bool In(const RECT& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
} // namespace

MenuHit HitTestMenu(const MenuView& v, int screenH, float x, float y) {
    MenuLayout L = Layout(v, screenH);
    MenuHit h;
    if (x < 0 || y < 0 || x >= L.W || y >= L.H) return h;
    h.kind = MenuHit::Panel;
    if (In(L.close, x, y)) { h.kind = MenuHit::Close; return h; }
    if (y < L.headH) { h.kind = MenuHit::Header; return h; }   // title bar: drag the menu
    if (In(L.save, x, y)) { h.kind = MenuHit::Save; return h; }
    if (In(L.reset, x, y)) { h.kind = MenuHit::Reset; return h; }
    if (In(L.def, x, y)) { h.kind = MenuHit::Default; return h; }
    for (int i = 0; i < (int)v.pages.size(); ++i) if (In(L.Page(i), x, y)) { h.kind = MenuHit::Page; h.index = i; return h; }
    for (int i = 0; i < (int)v.items.size(); ++i) {
        if (!L.Shown(i) || !In(L.Row(i), x, y)) continue;
        h.index = i; h.kind = MenuHit::Row;
        const MenuItem& it = v.items[i];
        switch (it.kind) {
        case MenuItemKind::Toggle: if (x >= L.CtlX0()) h.kind = MenuHit::Toggle; break;
        case MenuItemKind::Slider: {
            RECT t = L.Track(i);
            if (x >= t.left - L.S(10) && x <= t.right + L.S(10)) { h.kind = MenuHit::Slider; h.frac = std::clamp((x - t.left) / std::max(1.f, (float)(t.right - t.left)), 0.f, 1.f); }
            break; }
        case MenuItemKind::Choice:
            if (In(L.Prev(i), x, y) || (x >= L.CtlX0() && x < (L.CtlX0() + L.CtlX1()) / 2)) h.kind = MenuHit::ChoicePrev;
            else if (x >= (L.CtlX0() + L.CtlX1()) / 2) h.kind = MenuHit::ChoiceNext;
            break;
        case MenuItemKind::Button: if (In(L.Button(i), x, y)) h.kind = MenuHit::ItemButton; break;
        case MenuItemKind::Heading: h.kind = MenuHit::Panel; h.index = -1; break;
        case MenuItemKind::Text: if (x >= L.CtlX0()) h.kind = MenuHit::TextField; break;
        case MenuItemKind::Key:
            if (In(L.KeyCell(i), x, y)) h.kind = MenuHit::KeyCell;
            else if (In(L.BtnCell(i), x, y)) h.kind = MenuHit::ButtonCell;
            break;
        }
        return h;
    }
    return h;
}

int MenuVisibleRows(const MenuView& v, int screenH) { return Layout(v, screenH).visible; }

float MenuSliderFrac(const MenuView& v, int screenH, int row, float x) {
    MenuLayout L = Layout(v, screenH);
    RECT t = L.Track(row);
    return std::clamp((x - t.left) / std::max(1.f, (float)(t.right - t.left)), 0.f, 1.f);
}

std::shared_ptr<OverlayImage> RenderMenu(const MenuView& v, int screenH) {
    MenuLayout L = Layout(v, screenH);
    auto S = [&](float x) { return L.S(x); };
    const int W = L.W, H = L.H;
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = screenH;
    img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    const float AR = 1.f, AG = 0.682f, AB = 0.f;
    const float TR = 0.96f, TG = 0.96f, TB = 0.97f, MR = 0.58f, MG = 0.61f, MB = 0.67f;

    // panel + sidebar
    const float rad = (float)S(18);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float cov = RoundedCoverage(x, y, W, H, rad);
        if (cov <= 0) continue;
        float t = (float)y / H;
        bool side = x < L.sideW && y >= L.headH && y < L.footY;
        float r = side ? 0.075f : 0.105f - 0.035f * t, g = side ? 0.08f : 0.115f - 0.04f * t, b = side ? 0.095f : 0.14f - 0.045f * t;
        float a = 0.96f * cov;
        Over(px[(size_t)y * W + x], a, r * a, g * a, b * a);
    }
    cv.Glow((float)S(150), (float)S(30), (float)S(380), AR, AG, AB, 0.07f);
    for (int x = S(18); x < W - S(18); ++x) for (int y = 0; y < S(4); ++y) cv.Blend(x, y, 1.f, AR, AG, AB);
    cv.Round(0, L.headH, W, std::max(1, S(1.5f)), 1.f, 1.f, 1.f, 1.f, 0.08f);
    cv.Round(0, L.footY, W, std::max(1, S(1.5f)), 1.f, 1.f, 1.f, 1.f, 0.08f);
    cv.Round(L.sideW, L.headH, std::max(1, S(1.5f)), L.footY - L.headH, 1.f, 1.f, 1.f, 1.f, 0.06f);

    Gdi mask(W, H);
    HFONT fTitle = MakeFont(S(30), FW_BOLD), fSub = MakeFont(S(15), FW_NORMAL), fPage = MakeFont(S(17), FW_SEMIBOLD),
          fSection = MakeFont(S(14), FW_BOLD), fLabel = MakeFont(S(19), FW_NORMAL), fHint = MakeFont(S(14), FW_NORMAL),
          fValue = MakeFont(S(18), FW_SEMIBOLD), fBtn = MakeFont(S(17), FW_SEMIBOLD), fBadge = MakeFont(S(12), FW_BOLD),
          fStatus = MakeFont(S(15), FW_NORMAL), fIcon = MakeFont(S(24), FW_NORMAL);

    // header: radio icon (speaker in a rounded square), title, subtitle, close button
    int ix = S(26), iy = S(18), is = S(44);
    cv.Round(ix, iy, is, is, (float)S(12), AR, AG, AB);
    {
        float sx = ix + is * 0.2f, sy = iy + is * 0.5f, k = is / 44.f;
        cv.Round((int)sx, (int)(sy - 6 * k), (int)(7 * k), (int)(12 * k), 2 * k, 0.08f, 0.08f, 0.1f);
        cv.Poly({{sx + 6 * k, sy - 6 * k}, {sx + 15 * k, sy - 13 * k}, {sx + 15 * k, sy + 13 * k}, {sx + 6 * k, sy + 6 * k}}, 0.08f, 0.08f, 0.1f);
        for (int i = 0; i < 2; ++i) cv.Arc(sx + 16 * k, sy, (6.f + 6 * i) * k, 2.6f * k, -0.9f, 0.9f, 0.08f, 0.08f, 0.1f);
    }
    DrawText(mask, px, fTitle, v.title, RECT{ix + is + S(16), S(12), W - S(420), S(52)}, TR, TG, TB);
    DrawText(mask, px, fSub, v.subtitle, RECT{ix + is + S(18), S(48), W - S(200), S(70)}, MR, MG, MB);
    {
        bool hov = v.hoverControl == MenuHit::Close;
        if (hov) cv.Round(L.close.left, L.close.top, L.close.right - L.close.left, L.close.bottom - L.close.top, (float)S(10), 1.f, 1.f, 1.f, 0.10f);
        float cx = (L.close.left + L.close.right) / 2.f, cy = (L.close.top + L.close.bottom) / 2.f, d = (float)S(8);
        cv.Line(cx - d, cy - d, cx + d, cy + d, (float)S(2.4f), hov ? TR : MR, hov ? TG : MG, hov ? TB : MB);
        cv.Line(cx - d, cy + d, cx + d, cy - d, (float)S(2.4f), hov ? TR : MR, hov ? TG : MG, hov ? TB : MB);
    }

    // sidebar pages
    for (int i = 0; i < (int)v.pages.size(); ++i) {
        RECT r = L.Page(i);
        bool act = i == v.page, hov = v.hoverControl == 100 + i;
        if (act) { cv.Round(r.left, r.top, r.right - r.left, r.bottom - r.top, (float)S(10), AR, AG, AB, 0.16f); cv.Round(r.left, r.top + S(8), S(4), r.bottom - r.top - S(16), (float)S(2), AR, AG, AB); }
        else if (hov) cv.Round(r.left, r.top, r.right - r.left, r.bottom - r.top, (float)S(10), 1.f, 1.f, 1.f, 0.06f);
        DrawText(mask, px, fPage, v.pages[i], RECT{r.left + S(18), r.top, r.right - S(8), r.bottom}, act ? AR : (hov ? TR : 0.80f), act ? AG : (hov ? TG : 0.81f), act ? AB : (hov ? TB : 0.85f));
    }

    // section title + column headers
    DrawText(mask, px, fSection, Upper(v.section), RECT{L.x0, L.headH + S(14), L.x1, L.headH + S(40)}, AR, AG, AB);
    if (L.keys) {
        RECT k = L.KeyCell(0), b = L.BtnCell(0);
        DrawText(mask, px, fSection, Upper(v.keyHeader), RECT{k.left + S(10), L.itemsY - S(26), k.right, L.itemsY}, MR, MG, MB);
        DrawText(mask, px, fSection, Upper(v.buttonHeader), RECT{b.left + S(10), L.itemsY - S(26), b.right, L.itemsY}, MR, MG, MB);
    }

    // rows (scrollbar when they do not fit)
    if ((int)v.items.size() > L.visible) {
        int top = L.itemsY, h = L.visible * L.rowH, n = (int)v.items.size();
        int th = std::max(S(30), h * L.visible / n), ty = top + (h - th) * L.first / std::max(1, n - L.visible);
        cv.Round(L.x1 + S(14), top, S(5), h, (float)S(2.5f), 1.f, 1.f, 1.f, 0.06f);
        cv.Round(L.x1 + S(14), ty, S(5), th, (float)S(2.5f), AR, AG, AB, 0.8f);
    }
    for (int i = 0; i < (int)v.items.size(); ++i) {
        if (!L.Shown(i)) continue;
        const MenuItem& it = v.items[i];
        RECT r = L.Row(i);
        if (it.kind == MenuItemKind::Heading) {   // small section title inside a page
            DrawText(mask, px, fSection, Upper(it.label), RECT{L.x0, r.top + S(14), L.x1, r.bottom}, AR, AG, AB);
            if (!it.hint.empty()) DrawText(mask, px, fHint, it.hint, RECT{L.x0 + S(260), r.top + S(14), L.x1, r.bottom}, MR, MG, MB, DT_RIGHT);
            cv.Round(L.x0, r.top + S(10), L.x1 - L.x0, std::max(1, S(1.5f)), 1.f, 1.f, 1.f, 1.f, 0.07f);
            continue;
        }
        bool sel = i == v.selected, hov = i == v.hover;
        if (sel) {
            cv.Round(r.left, r.top + S(2), r.right - r.left, r.bottom - r.top - S(4), (float)S(10), AR, AG, AB, 0.12f);
            cv.Round(r.left, r.top + S(8), S(4), r.bottom - r.top - S(16), (float)S(2), AR, AG, AB);
        } else if (hov) cv.Round(r.left, r.top + S(2), r.right - r.left, r.bottom - r.top - S(4), (float)S(10), 1.f, 1.f, 1.f, 0.05f);
        int lx = L.x0 + S(6), lx1 = (it.kind == MenuItemKind::Key ? L.KeyCell(i).left : L.CtlX0()) - S(14);
        bool twoLines = !it.hint.empty() && it.kind != MenuItemKind::Key;
        int cy = (r.top + r.bottom) / 2;
        int labelW = lx1 - lx;
        if (it.restart) {   // small "restart" pill after the label
            int lw = std::min(labelW - S(90), (int)Measure(mask.dc, fLabel, it.label).cx);
            int px0 = lx + lw + S(10), pw = Measure(mask.dc, fBadge, v.restartText).cx + S(14), ptop = twoLines ? r.top + S(9) : cy - S(10);
            cv.Round(px0, ptop, pw, S(20), (float)S(10), 0.30f, 0.32f, 0.38f);
            DrawText(mask, px, fBadge, v.restartText, RECT{px0, ptop, px0 + pw, ptop + S(20)}, 0.88f, 0.89f, 0.92f, DT_CENTER);
            labelW = lw + S(2);
        }
        if (twoLines) {
            DrawText(mask, px, fLabel, it.label, RECT{lx, r.top + S(5), lx + labelW, cy + S(4)}, TR, TG, TB);
            DrawText(mask, px, fHint, it.hint, RECT{lx, cy + S(2), lx1, r.bottom - S(5)}, MR, MG, MB);
        } else DrawText(mask, px, fLabel, it.label, RECT{lx, r.top, lx + labelW, r.bottom}, TR, TG, TB);

        switch (it.kind) {
        case MenuItemKind::Toggle: {
            RECT t = L.Toggle(i);
            int tw = t.right - t.left, th = t.bottom - t.top;
            if (it.on) cv.Round(t.left, t.top, tw, th, th / 2.f, AR, AG, AB); else cv.Round(t.left, t.top, tw, th, th / 2.f, 0.26f, 0.27f, 0.31f);
            float kx = it.on ? t.right - th / 2.f : t.left + th / 2.f;
            cv.Circle(kx, (t.top + t.bottom) / 2.f, th / 2.f - S(3), it.on ? 0.08f : 0.80f, it.on ? 0.08f : 0.81f, it.on ? 0.1f : 0.85f);
            break; }
        case MenuItemKind::Slider: {
            RECT t = L.Track(i);
            int ty = (t.top + t.bottom) / 2, th = S(6), tw = t.right - t.left;
            cv.Round(t.left, ty - th / 2, tw, th, th / 2.f, 0.26f, 0.27f, 0.31f);
            int fw = std::max(th, (int)(tw * std::clamp(it.frac, 0.f, 1.f)));
            cv.Round(t.left, ty - th / 2, fw, th, th / 2.f, AR, AG, AB);
            cv.Circle((float)t.left + fw, (float)ty, (float)S(9), TR, TG, TB);
            DrawText(mask, px, fValue, it.value, RECT{t.right + S(14), r.top, L.CtlX1(), r.bottom}, AR, AG, AB, DT_RIGHT);
            break; }
        case MenuItemKind::Choice: {
            RECT p = L.Prev(i), n = L.Next(i);
            for (RECT b : {p, n}) cv.Round(b.left, b.top, b.right - b.left, b.bottom - b.top, (float)S(8), 1.f, 1.f, 1.f, 0.07f);
            float ay = (p.top + p.bottom) / 2.f, d = (float)S(6);
            float pxc = (p.left + p.right) / 2.f, nxc = (n.left + n.right) / 2.f;
            cv.Line(pxc + d / 2, ay - d, pxc - d / 2, ay, (float)S(2.2f), TR, TG, TB); cv.Line(pxc - d / 2, ay, pxc + d / 2, ay + d, (float)S(2.2f), TR, TG, TB);
            cv.Line(nxc - d / 2, ay - d, nxc + d / 2, ay, (float)S(2.2f), TR, TG, TB); cv.Line(nxc + d / 2, ay, nxc - d / 2, ay + d, (float)S(2.2f), TR, TG, TB);
            DrawText(mask, px, fValue, it.value, RECT{p.right + S(8), r.top, n.left - S(8), r.bottom}, AR, AG, AB, DT_CENTER);
            break; }
        case MenuItemKind::Button: {
            RECT b = L.Button(i);
            bool hovB = hov && v.hoverControl == MenuHit::ItemButton;
            cv.Round(b.left, b.top, b.right - b.left, b.bottom - b.top, (float)S(10), AR, AG, AB, hovB ? 0.95f : 0.75f);
            DrawText(mask, px, fBtn, it.button, b, 0.07f, 0.07f, 0.09f, DT_CENTER);
            DrawText(mask, px, fValue, it.value, RECT{L.CtlX0() - S(40), r.top, b.left - S(14), r.bottom}, 0.86f, 0.87f, 0.90f, DT_RIGHT);
            break; }
        case MenuItemKind::Heading: break;
        case MenuItemKind::Text: {   // text field; while typing: accent frame, the text being typed and a caret
            RECT f{L.CtlX0(), cy - S(18), L.CtlX1(), cy + S(18)};
            bool editing = sel && v.capture == 3;
            cv.Round(f.left, f.top, f.right - f.left, f.bottom - f.top, (float)S(8), editing ? AR : 1.f, editing ? AG : 1.f, editing ? AB : 1.f, editing ? 0.22f : 0.06f);
            std::wstring t = editing ? v.captureText + L"\u2502" : it.value;
            RECT tr{f.left + S(12), f.top, f.right - S(12), f.bottom};
            if (editing) while (t.size() > 2 && Measure(mask.dc, fValue, t).cx > tr.right - tr.left) t = L"\u2026" + t.substr(2);   // typing: keep the end visible
            DrawText(mask, px, fValue, t, tr, editing ? TR : AR, editing ? TG : AG, editing ? TB : AB);
            break; }
        case MenuItemKind::Key: {
            RECT k = L.KeyCell(i), b = L.BtnCell(i);
            bool capK = sel && v.capture == 1, capB = sel && v.capture == 2;
            for (auto [cell, cap] : {std::pair<RECT, bool>{k, capK}, std::pair<RECT, bool>{b, capB}})
                cv.Round(cell.left, cell.top, cell.right - cell.left, cell.bottom - cell.top, (float)S(8), cap ? AR : 1.f, cap ? AG : 1.f, cap ? AB : 1.f, cap ? 0.22f : 0.05f);
            bool noneK = it.key == L"–", noneB = it.button == L"–";
            DrawText(mask, px, fValue, capK ? v.captureText : it.key, RECT{k.left + S(10), k.top, k.right - S(6), k.bottom},
                     capK ? AR : noneK ? MR : TR, capK ? AG : noneK ? MG : TG, capK ? AB : noneK ? MB : TB);
            DrawText(mask, px, fLabel, capB ? v.captureText : it.button, RECT{b.left + S(10), b.top, b.right - S(6), b.bottom},
                     capB ? AR : noneB ? MR : 0.86f, capB ? AG : noneB ? MG : 0.87f, capB ? AB : noneB ? MB : 0.90f);
            break; }
        }
    }

    // footer: status + buttons
    int fy0 = L.footY + S(4), fy1 = L.H - S(6);
    if (v.unsaved) cv.Circle((float)S(34), (fy0 + fy1) / 2.f - S(9), (float)S(4), AR, AG, AB);
    DrawText(mask, px, fStatus, v.status, RECT{S(46), fy0 + S(6), L.def.left - S(16), (fy0 + fy1) / 2 + S(2)}, v.unsaved ? AR : MR, v.unsaved ? AG : MG, v.unsaved ? AB : MB);
    DrawText(mask, px, fHint, v.footer, RECT{S(46), (fy0 + fy1) / 2, L.def.left - S(16), fy1 - S(4)}, MR, MG, MB);
    struct Btn { RECT r; const std::wstring& t; bool primary; int id; };
    for (const Btn& b : {Btn{L.def, v.btnDefault, false, MenuHit::Default}, Btn{L.reset, v.btnReset, false, MenuHit::Reset}, Btn{L.save, v.btnSave, true, MenuHit::Save}}) {
        bool hov = v.hoverControl == b.id;
        int bw = b.r.right - b.r.left, bh = b.r.bottom - b.r.top;
        if (b.primary) cv.Round(b.r.left, b.r.top, bw, bh, (float)S(10), hov ? 1.f : AR, hov ? 0.78f : AG, hov ? 0.25f : AB);
        else cv.Round(b.r.left, b.r.top, bw, bh, (float)S(10), 1.f, 1.f, 1.f, hov ? 0.16f : 0.08f);
        DrawText(mask, px, fBtn, b.t, b.r, b.primary ? 0.07f : TR, b.primary ? 0.07f : TG, b.primary ? 0.09f : TB, DT_CENTER);
    }
    for (HFONT f : {fTitle, fSub, fPage, fSection, fLabel, fHint, fValue, fBtn, fBadge, fStatus, fIcon}) DeleteObject(f);
    return img;
}

std::shared_ptr<OverlayImage> RenderMenuCursor(int screenH) {
    const float s = (float)std::clamp(screenH / 1080.0, 0.5, 4.0);
    int W = std::max(8, (int)std::lround(26 * s)), H = std::max(10, (int)std::lround(36 * s));
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = screenH;
    img->pixels.assign((size_t)W * H, 0u);
    Canvas cv{img->pixels, W, H};
    auto P = [&](float x, float y) { return std::pair<float, float>{x * s, y * s}; };
    std::vector<std::pair<float, float>> outer = {P(1, 1), P(1, 27), P(7.5f, 21), P(12, 31), P(16.5f, 29), P(12, 19.5f), P(20.5f, 19.5f)};
    std::vector<std::pair<float, float>> inner = {P(3, 5.5f), P(3, 22.5f), P(8, 17.8f), P(12.8f, 28.2f), P(14.2f, 27.6f), P(9.6f, 17.5f), P(16.4f, 17.5f)};
    cv.Poly(outer, 0.05f, 0.05f, 0.06f);
    cv.Poly(inner, 1.f, 1.f, 1.f);
    return img;
}

// ------------------------------------------------------------------------------------------------
// Test mode for the in-cab screen: coordinate grid over the whole render target plus the area the card is drawn
// into (orange frame, corner values), so areaX/Y/W/H can be set until nothing is cut off.
std::shared_ptr<OverlayImage> RenderScreenTest(int W, int H, bool flipX, bool flipY, float ax, float ay, float aw, float ah) {
    auto img = RenderScreenGrid(W, H, flipX, flipY);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    // area in target coordinates -> picture coordinates (the picture is mirrored when it is drawn)
    float x0 = flipX ? 1.f - (ax + aw) : ax, x1 = x0 + aw;
    float y0 = flipY ? 1.f - (ay + ah) : ay, y1 = y0 + ah;
    int X0 = (int)(x0 * W), X1 = (int)(x1 * W), Y0 = (int)(y0 * H), Y1 = (int)(y1 * H);
    FillRect(px, W, H, X0, Y0, X1, Y1, 0.30f, 1.f, 0.55f, 0.f);   // translucent fill
    int t = std::max(3, std::min(W, H) / 150);
    FillRect(px, W, H, X0, Y0, X1, Y0 + t, 1.f, 1.f, 0.55f, 0.f); FillRect(px, W, H, X0, Y1 - t, X1, Y1, 1.f, 1.f, 0.55f, 0.f);
    FillRect(px, W, H, X0, Y0, X0 + t, Y1, 1.f, 1.f, 0.55f, 0.f); FillRect(px, W, H, X1 - t, Y0, X1, Y1, 1.f, 1.f, 0.55f, 0.f);
    Gdi mask(W, H);
    HFONT f = MakeFont(std::max(10, std::min(W, H) / 26), FW_BOLD), fb = MakeFont(std::max(12, std::min(W, H) / 16), FW_BOLD);
    auto num = [](float v) { wchar_t b[16]; swprintf(b, 16, L"%.2f", v); std::wstring s = b; for (auto& c : s) if (c == L'.') c = L','; return s; };
    std::wstring tl = L"X " + num(ax) + L"  Y " + num(ay), br = L"B " + num(aw) + L"  H " + num(ah);
    int lh = std::min(W, H) / 14, lw = W / 2;
    // label boxes inside the frame corners (readable on every background)
    cv.Round(X0 + t * 2, Y0 + t * 2, std::min(lw, X1 - X0 - t * 4), lh, (float)lh / 4, 0.f, 0.f, 0.f, 0.75f);
    DrawText(mask, px, f, tl, RECT{X0 + t * 3, Y0 + t * 2, X0 + t * 2 + std::min(lw, X1 - X0 - t * 4), Y0 + t * 2 + lh}, 1.f, 0.75f, 0.2f);
    cv.Round(std::max(X0 + t * 2, X1 - t * 2 - lw), Y1 - t * 2 - lh, std::min(lw, X1 - X0 - t * 4), lh, (float)lh / 4, 0.f, 0.f, 0.f, 0.75f);
    DrawText(mask, px, f, br, RECT{std::max(X0 + t * 2, X1 - t * 2 - lw), Y1 - t * 2 - lh, X1 - t * 3, Y1 - t * 2}, 1.f, 0.75f, 0.2f, DT_RIGHT);
    DrawText(mask, px, fb, L"TEST", RECT{X0, (Y0 + Y1) / 2 - lh, X1, (Y0 + Y1) / 2 + lh}, 1.f, 1.f, 1.f, DT_CENTER);
    DeleteObject(f); DeleteObject(fb);
    return img;
}

// Start screen when the ignition is switched on (like modern infotainment systems): black background with the
// manufacturer's logo from realistic-radio\brands\ if the player put one there, otherwise the brand name as text.
std::shared_ptr<OverlayImage> RenderScreenSplash(const OverlayImage* logo, const std::wstring& brand, const std::wstring& model, int W, int H,
                                                 float logoScale, float scale) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, Pack(1.f, 0.f, 0.f, 0.f));
    auto& px = img->pixels;
    const float u = std::min(W / 16.f, H / 9.f) / 40.f * std::clamp(scale, 0.4f, 2.f);
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    if (logo && logo->width > 0) {   // logoScale: height of the logo box as part of the area
        float k = std::clamp(logoScale, 0.1f, 1.f);
        int bh = (int)(H * k), bw = std::min(W, (int)(W * k * 1.1f));
        BlitLogo(px, W, logo, (W - bw) / 2, (H - bh) / 2 - U(model.empty() ? 0 : 14), bw, bh);
        if (!model.empty()) {
            Gdi mask(W, H);
            HFONT f = MakeFont(U(20), FW_NORMAL);
            DrawText(mask, px, f, model, RECT{0, H - U(64), W, H - U(30)}, 0.55f, 0.56f, 0.6f, DT_CENTER);
            DeleteObject(f);
        }
        return img;
    }
    if (brand.empty()) return img;
    // brand name, wide letter spacing, thin line, model below
    std::wstring spaced;
    for (wchar_t c : Upper(brand)) { if (!spaced.empty()) spaced += L"  "; spaced += c; }
    Gdi mask(W, H);
    HFONT fBrand = MakeFont(U(64), FW_LIGHT), fModel = MakeFont(U(20), FW_NORMAL);
    int cy = H / 2;
    DrawText(mask, px, fBrand, spaced, RECT{0, cy - U(62), W, cy + U(14)}, 0.94f, 0.94f, 0.96f, DT_CENTER);
    Canvas cv{px, W, H};
    int lw = std::min(W / 3, U(220));
    cv.Round((W - lw) / 2, cy + U(26), lw, std::max(1, U(1.5f)), 1.f, 0.94f, 0.94f, 0.96f, 0.35f);
    if (!model.empty()) DrawText(mask, px, fModel, model, RECT{0, cy + U(42), W, cy + U(74)}, 0.55f, 0.56f, 0.6f, DT_CENTER);
    DeleteObject(fBrand); DeleteObject(fModel);
    return img;
}

// Truck logo pictures are often phone wallpapers or photos: a dark background with the logo somewhere in it. An opaque
// picture gets its dark parts made transparent (glows stay soft), then it is cut to the logo with a soft edge, so it
// sits cleanly on the black start screen at a useful size. Pictures with transparency are only cut.
std::shared_ptr<OverlayImage> PrepareLogoImage(const OverlayImage& src) {
    const int W = src.width, H = src.height;
    if (W <= 0 || H <= 0) return nullptr;
    std::vector<float> al((size_t)W * H), r((size_t)W * H), g((size_t)W * H), b((size_t)W * H);
    bool opaque = true;
    for (size_t i = 0; i < al.size(); ++i) {
        uint32_t p = src.pixels[i];
        al[i] = ((p >> 24) & 255) / 255.f; r[i] = ((p >> 16) & 255) / 255.f; g[i] = ((p >> 8) & 255) / 255.f; b[i] = (p & 255) / 255.f;
        if (al[i] < 0.99f) opaque = false;
    }
    if (opaque)
        for (size_t i = 0; i < al.size(); ++i) {
            float m = std::max({r[i], g[i], b[i]});
            float a = std::clamp((m - 0.08f) / 0.25f, 0.f, 1.f);
            al[i] = a; r[i] *= a; g[i] *= a; b[i] *= a;   // premultiplied
        }
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        if (al[(size_t)y * W + x] > 0.5f) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) return nullptr;
    int mg = (int)(std::max(x1 - x0, y1 - y0) * 0.12f) + 2;
    x0 = std::max(0, x0 - mg); y0 = std::max(0, y0 - mg); x1 = std::min(W - 1, x1 + mg); y1 = std::min(H - 1, y1 + mg);
    auto img = std::make_shared<OverlayImage>();
    img->width = x1 - x0 + 1; img->height = y1 - y0 + 1; img->renderedForScreenHeight = img->height;
    img->pixels.resize((size_t)img->width * img->height);
    float fadeLen = std::max(1.f, mg * 0.8f);
    for (int y = 0; y < img->height; ++y) for (int x = 0; x < img->width; ++x) {
        size_t s = (size_t)(y + y0) * W + (x + x0);
        float d = (float)std::min(std::min(y, img->height - 1 - y), std::min(x, img->width - 1 - x));
        float f = opaque ? std::clamp(d / fadeLen, 0.f, 1.f) : 1.f;   // soft edge: no visible frame from the cut
        auto c8 = [&](float v) { return (uint32_t)std::lround(std::clamp(v * f, 0.f, 1.f) * 255.f); };
        img->pixels[(size_t)y * img->width + x] = (c8(al[s]) << 24) | (c8(r[s]) << 16) | (c8(g[s]) << 8) | c8(b[s]);
    }
    return img;
}
