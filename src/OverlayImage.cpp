// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "OverlayImage.h"
#include "Logger.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>

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
// Draws white text into the mask, then blends it with the given colour into out.
void DrawText(Gdi& mask, std::vector<uint32_t>& out, HFONT f, const std::wstring& s, RECT r, float R, float G, float B, UINT align = 0) {
    if (s.empty()) return;
    mask.Clear();
    HGDIOBJ o = SelectObject(mask.dc, f);
    DrawTextW(mask.dc, s.c_str(), (int)s.size(), &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER | align);
    SelectObject(mask.dc, o); GdiFlush();
    for (int i = 0; i < mask.w * mask.h; ++i) {
        uint32_t p = mask.bits[i];
        float cov = std::max({(p >> 16) & 255u, (p >> 8) & 255u, p & 255u}) / 255.f;
        if (cov > 0) Over(out[i], cov, R * cov, G * cov, B * cov);
    }
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
