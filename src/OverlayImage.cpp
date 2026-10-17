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

HFONT MakeFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
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

std::shared_ptr<OverlayImage> RenderScreenCard(const OverlayInfo& info, const OverlayImage* logo, int W, int H) {
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.assign((size_t)W * H, 0u);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    const float u = std::min(W / 16.f, H / 9.f) / 40.f;   // layout unit (1.0 at 640x360)
    auto U = [&](float v) { return std::max(1, (int)std::lround(v * u)); };
    const float AR = 1.f, AG = 0.682f, AB = 0.f;           // amber accent
    const float TR = 0.97f, TG = 0.97f, TB = 0.98f;         // main text
    const float MR = 0.60f, MG = 0.63f, MB = 0.69f;         // muted text

    // background: dark blue-grey gradient + a soft amber glow behind the artwork
    for (int y = 0; y < H; ++y) {
        float t = (float)y / std::max(1, H - 1);
        uint32_t v = Pack(1.f, 0.105f - 0.06f * t, 0.115f - 0.065f * t, 0.14f - 0.075f * t);
        std::fill(px.begin() + (size_t)y * W, px.begin() + (size_t)(y + 1) * W, v);
    }
    cv.Glow(U(120), H * 0.62f, (float)U(300), AR, AG, AB, 0.10f);

    Gdi mask(W, H);
    HDC mdc = mask.dc;
    const int padX = U(22);

    // header: source chip + headline, status on the right
    HFONT fChip = MakeFont(U(16), FW_BOLD), fHead = MakeFont(U(19), FW_NORMAL);
    std::wstring chip = Upper(info.source.empty() ? L"Radio" : info.source);
    int chipW = Measure(mdc, fChip, chip).cx + U(26), chipH = U(28), chipY = U(17);
    cv.Round(padX, chipY, chipW, chipH, chipH / 2.f, AR, AG, AB);
    DrawText(mask, px, fChip, chip, RECT{padX, chipY, padX + chipW, chipY + chipH}, 0.07f, 0.07f, 0.09f, DT_CENTER);
    int rightEdge = W - padX, headY0 = chipY, headY1 = chipY + chipH;
    if (info.volume < 0 && !info.media && info.bars >= 0) {   // signal bars
        int bw = U(6), gap = U(4), base = headY1 - U(3);
        int x0 = rightEdge - (5 * bw + 4 * gap);
        for (int i = 0; i < 5; ++i) {
            int bh = U(7 + i * 4), xs = x0 + i * (bw + gap);
            if (i < info.bars) cv.Round(xs, base - bh, bw, bh, U(1.5f), AR, AG, AB);
            else cv.Round(xs, base - bh, bw, bh, U(1.5f), 0.26f, 0.27f, 0.31f);
        }
        rightEdge = x0 - U(14);
    }
    if (info.volume < 0 && info.media) {   // play / pause symbol
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
    cv.Round(padX, U(60), W - 2 * padX, std::max(1, U(1.5f)), 1.f, 1.f, 1.f, 1.f, 0.10f);   // separator
    DeleteObject(fChip); DeleteObject(fHead);

    if (info.volume >= 0) {   // ---- volume ----
        const bool muted = info.muted;
        int v = std::clamp(info.volume, 0, 100);
        // speaker symbol
        const int mid = U(160);
        float s = (float)U(78), sx = (float)padX + U(4), sy = mid - s / 2;
        float ir = muted ? 0.55f : TR, ig = muted ? 0.57f : TG, ib = muted ? 0.62f : TB;
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
                cv.Arc(cx, cyy, s * (0.2f + 0.16f * i), (float)U(5), -0.85f, 0.85f, on ? AR : 0.30f, on ? AG : 0.31f, on ? AB : 0.35f);
            }
        }
        // big percentage (grey while muted: the volume that comes back when unmuting)
        HFONT fBig = MakeFont(U(96), FW_BOLD), fPct = MakeFont(U(40), FW_SEMIBOLD);
        std::wstring num = std::to_wstring(v);
        int nx = (int)(sx + s * 1.25f) + U(20);
        int nw = Measure(mdc, fBig, num).cx;
        DrawText(mask, px, fBig, num, RECT{nx, mid - U(64), nx + nw + U(4), mid + U(60)}, muted ? 0.45f : TR, muted ? 0.47f : TG, muted ? 0.52f : TB);
        DrawText(mask, px, fPct, L"%", RECT{nx + nw + U(6), mid - U(8), nx + nw + U(70), mid + U(42)}, MR, MG, MB);
        DeleteObject(fBig); DeleteObject(fPct);
        // 20 segments = 5 % steps (the step size of the volume keys)
        const int segs = 20, gap = U(5), by = U(262), bh = U(40);
        float segW = (W - 2 * padX - (segs - 1) * gap) / (float)segs;
        int lit = (v + 2) / 5;
        for (int i = 0; i < segs; ++i) {
            int x0 = padX + (int)std::lround(i * (segW + gap)), x1 = padX + (int)std::lround(i * (segW + gap) + segW);
            int h = bh * (35 + 65 * (i + 1) / segs) / 100;   // rising wedge
            if (i < lit && muted) cv.Round(x0, by + bh - h, x1 - x0, h, U(2), 0.40f, 0.41f, 0.46f);
            else if (i < lit) {
                float k = 0.75f + 0.25f * i / (segs - 1);
                cv.Round(x0, by + bh - h, x1 - x0, h, U(2), AR * k + (1 - k) * 0.9f, AG * k, AB);
            } else cv.Round(x0, by + bh - h, x1 - x0, h, U(2), 0.22f, 0.23f, 0.27f);
        }
        return img;
    }

    // ---- station / track ----
    const bool progress = info.media && info.posSec >= 0;
    const int top = U(78), bottom = progress ? U(286) : U(338);
    const int T = std::min(bottom - top, U(200));
    const int tileX = padX, tileY = top + (bottom - top - T) / 2;
    if (logo && !info.media) {   // station logo on a light tile (dark logos stay visible)
        cv.Round(tileX, tileY, T, T, (float)U(18), 0.95f, 0.95f, 0.96f);
        BlitLogo(px, W, logo, tileX + U(16), tileY + U(16), T - U(32), T - U(32));
    } else if (logo) {           // album cover
        cv.Round(tileX, tileY, T, T, (float)U(18), 0.16f, 0.17f, 0.21f);
        BlitLogo(px, W, logo, tileX + U(6), tileY + U(6), T - U(12), T - U(12));
    } else {
        cv.Round(tileX, tileY, T, T, (float)U(18), 0.17f, 0.18f, 0.22f);
        cv.Glow(tileX + T * 0.3f, tileY + T * 0.25f, T * 0.9f, AR, AG, AB, 0.12f);
        const float cx = tileX + T / 2.f, cy = tileY + T / 2.f, L = 0.80f, LG = 0.82f, LB = 0.86f, DR = 0.17f, DG = 0.18f, DB = 0.22f;
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
        } else if (info.media) {   // disc (CD)
            float r = T * 0.36f;
            cv.Circle(cx, cy, r, 0.80f, 0.82f, 0.86f);
            cv.Arc(cx, cy, r * 0.72f, r * 0.10f, 0.35f, 1.25f, 1.f, 1.f, 1.f);
            cv.Circle(cx, cy, r * 0.42f, AR, AG, AB);
            cv.Circle(cx, cy, r * 0.12f, 0.17f, 0.18f, 0.22f);
        } else {
            HFONT fIni = MakeFont((int)(T * 0.36f), FW_BOLD);
            DrawText(mask, px, fIni, Initials(info.title), RECT{tileX, tileY, tileX + T, tileY + T}, AR, AG, AB, DT_CENTER);
            DeleteObject(fIni);
        }
    }

    // text column
    const int tx0 = tileX + T + U(26), tx1 = W - padX, tw = std::max(10, tx1 - tx0);
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
    int subH = info.subtitle.empty() ? 0 : U(34), detH = info.detail.empty() ? 0 : U(28);
    int blockH = titleH + subH + detH;
    int y = tileY + (T - blockH) / 2;
    if (twoLines) DrawText2(mask, px, ft, info.title, RECT{tx0, y, tx1, y + titleH}, TR, TG, TB);
    else DrawText(mask, px, ft, info.title, RECT{tx0, y, tx1, y + titleH}, TR, TG, TB);
    y += titleH;
    DrawText(mask, px, fSub, info.subtitle, RECT{tx0, y, tx1, y + subH}, 0.86f, 0.87f, 0.90f); y += subH;
    DrawText(mask, px, fDet, info.detail, RECT{tx0, y, tx1, y + detH}, MR, MG, MB);
    DeleteObject(fTitle); DeleteObject(fTitle2); DeleteObject(fSub); DeleteObject(fDet);

    if (progress) {   // elapsed - bar - length
        HFONT fTime = MakeFont(U(18), FW_SEMIBOLD);
        int cy = U(318), tWid = U(62);
        DrawText(mask, px, fTime, Clock(info.posSec), RECT{padX, cy - U(14), padX + tWid, cy + U(14)}, MR, MG, MB);
        DrawText(mask, px, fTime, Clock(info.lenSec), RECT{W - padX - tWid, cy - U(14), W - padX, cy + U(14)}, MR, MG, MB, DT_RIGHT);
        int bx = padX + tWid + U(10), bw = W - 2 * (padX + tWid + U(10)), bh = U(8);
        cv.Round(bx, cy - bh / 2, bw, bh, bh / 2.f, 0.24f, 0.25f, 0.29f);
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
