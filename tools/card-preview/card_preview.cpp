#include "shim.h"
struct OverlayImage {
    int width = 0, height = 0;
    int renderedForScreenHeight = 1080;
    std::vector<uint32_t> pixels;
};

struct OverlayInfo {
    std::wstring headline;   // "Jetzt läuft:" or a status text
    std::wstring title;      // station name (orange, like the original)
    std::wstring subtitle;   // country / volume / state
    int bars = -1;           // 0..5 reception, -1 = hidden
    bool favourite = false;
    std::wstring lcd;        // one-line text for the car-radio style display (empty = title)
    int volume = -1;         // >= 0: this is a volume message (0..100)
    bool muted = false;
    std::wstring volumeLabel;   // label above the volume bar on the in-cab screen
    // in-cab screen card
    std::wstring source;     // source chip: "RADIO", "USB", "CD"
    std::wstring detail;     // small third line (e.g. "Sender 3 / 12", album)
    bool media = false;      // USB/CD track: progress bar instead of signal bars
    std::string mediaType;   // usb | cd | sd | cassette | bluetooth | aux: picture when there is no cover
    bool paused = false;
    int posSec = -1, lenSec = -1;   // track position / length in seconds (-1 = unknown)
    bool operator==(const OverlayInfo& o) const { return headline == o.headline && title == o.title && subtitle == o.subtitle && bars == o.bars && favourite == o.favourite && lcd == o.lcd && volume == o.volume && muted == o.muted &&
                                                         source == o.source && mediaType == o.mediaType && detail == o.detail && media == o.media && paused == o.paused && posSec == o.posSec && lenSec == o.lenSec; }
};


enum class MenuItemKind { Toggle, Slider, Choice, Key, Button, Heading };
struct MenuItem {
    MenuItemKind kind = MenuItemKind::Toggle;
    std::wstring label, hint;
    bool on = false;            // toggle
    float frac = 0;             // slider position 0..1
    std::wstring value;         // slider / choice text
    std::wstring key, button;   // key binding row; Button: `button` = text on the button, `value` = info left of it
    bool restart = false;       // takes effect after a game restart
};
struct MenuView {
    std::wstring title, subtitle, section, keyHeader, buttonHeader, restartText;
    std::vector<std::wstring> pages;
    int page = 0;
    std::vector<MenuItem> items;
    int selected = -1;          // keyboard selection
    int firstRow = 0;           // scrolling: first visible row
    int hover = -1;             // row under the mouse
    int capture = 0;            // selected key row waits for: 1 = key, 2 = controller button
    std::wstring captureText;
    std::wstring status;        // left of the buttons
    bool unsaved = false;
    std::wstring btnSave, btnReset, btnDefault, footer;
    int hoverControl = -1;      // MenuHit kind under the mouse for buttons (Save/Reset/Default/Close) or page index + 100
};
struct MenuHit {
    enum Kind { None, Page, Row, Slider, ChoicePrev, ChoiceNext, Toggle, KeyCell, ButtonCell, ItemButton, Save, Reset, Default, Close, Header, Panel } kind = None;
    int index = -1;             // page or row
    float frac = 0;             // slider position under the mouse
};

std::shared_ptr<OverlayImage> RenderScreenCard(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const std::string& design = "modern", float scale = 1.f, float logoSize = 1.f);
std::shared_ptr<OverlayImage> RenderScreenGrid(int W, int H, bool flipX = false, bool flipY = false);
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
struct CardTheme {
    float bgTop[3], bgBottom[3], accent[3], chipText[3], text[3], sub[3], muted[3], tile[3], inactive[3], art[3];
    float glow;
    const wchar_t* font;
};
const CardTheme kThemes[] = {
    // modern (default): dark blue-grey, amber
    {{0.105f, 0.115f, 0.14f}, {0.045f, 0.05f, 0.065f}, {1.f, 0.682f, 0.f}, {0.07f, 0.07f, 0.09f}, {0.97f, 0.97f, 0.98f}, {0.86f, 0.87f, 0.90f},
     {0.60f, 0.63f, 0.69f}, {0.17f, 0.18f, 0.22f}, {0.26f, 0.27f, 0.31f}, {0.80f, 0.82f, 0.86f}, 0.10f, L"Segoe UI"},
    // ocean: navy, light blue
    {{0.05f, 0.10f, 0.18f}, {0.02f, 0.04f, 0.09f}, {0.25f, 0.72f, 1.f}, {0.02f, 0.05f, 0.10f}, {0.97f, 0.98f, 1.f}, {0.80f, 0.87f, 0.95f},
     {0.52f, 0.63f, 0.76f}, {0.08f, 0.15f, 0.26f}, {0.15f, 0.25f, 0.38f}, {0.78f, 0.86f, 0.95f}, 0.12f, L"Segoe UI"},
    // sport: black-red
    {{0.12f, 0.05f, 0.06f}, {0.04f, 0.02f, 0.02f}, {0.95f, 0.22f, 0.20f}, {1.f, 1.f, 1.f}, {0.98f, 0.97f, 0.97f}, {0.90f, 0.83f, 0.83f},
     {0.66f, 0.56f, 0.56f}, {0.20f, 0.10f, 0.11f}, {0.33f, 0.19f, 0.20f}, {0.86f, 0.82f, 0.82f}, 0.10f, L"Segoe UI"},
    // light: white, blue
    {{0.96f, 0.97f, 0.98f}, {0.86f, 0.88f, 0.91f}, {0.f, 0.46f, 0.86f}, {1.f, 1.f, 1.f}, {0.08f, 0.09f, 0.11f}, {0.24f, 0.26f, 0.30f},
     {0.42f, 0.45f, 0.50f}, {0.80f, 0.83f, 0.88f}, {0.70f, 0.73f, 0.79f}, {0.36f, 0.39f, 0.45f}, 0.f, L"Segoe UI"},
    // retro: green LCD on black, monospace
    {{0.02f, 0.035f, 0.02f}, {0.01f, 0.02f, 0.01f}, {0.35f, 1.f, 0.45f}, {0.01f, 0.05f, 0.02f}, {0.55f, 1.f, 0.60f}, {0.42f, 0.86f, 0.47f},
     {0.26f, 0.58f, 0.31f}, {0.04f, 0.10f, 0.05f}, {0.08f, 0.22f, 0.10f}, {0.42f, 0.86f, 0.47f}, 0.05f, L"Consolas"},
};
const CardTheme& ThemeFor(const std::string& d) {
    if (d == "ocean") return kThemes[1];
    if (d == "sport") return kThemes[2];
    if (d == "light") return kThemes[3];
    if (d == "retro") return kThemes[4];
    return kThemes[0];
}
} // namespace

static std::shared_ptr<OverlayImage> RenderScreenCardImpl(const OverlayInfo& info, const OverlayImage* logo, int W, int H, const std::string& design,
                                                          float uLayout, float logoSize) {
    const CardTheme& th = ThemeFor(design);
    FontScope fontScope(th.font);
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
    if (logo && !info.media) {   // station logo on a light tile (dark logos stay visible)
        cv.Round(tileX, tileY, T, T, (float)U(18), 0.95f, 0.95f, 0.96f);
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
    DrawText(mask, px, fSub, info.subtitle, RECT{tx0, y, tx1, y + subH}, th.sub[0], th.sub[1], th.sub[2]); y += subH;
    DrawText(mask, px, fDet, info.detail, RECT{tx0, y, tx1, y + detH}, MR, MG, MB);
    DeleteObject(fTitle); DeleteObject(fTitle2); DeleteObject(fSub); DeleteObject(fDet);

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
                                               float logoSize) {
    float base = std::min(W / 16.f, H / 9.f) / 40.f;
    float u = std::min(base * std::clamp(scale, 0.4f, 2.f), H / 360.f);
    logoSize = std::clamp(logoSize, 0.4f, 1.5f);
    int h2 = (int)std::ceil(360.f * u);
    if (h2 >= H - 2) return RenderScreenCardImpl(info, logo, W, H, design, u, logoSize);
    auto inner = RenderScreenCardImpl(info, logo, W, h2, design, u, logoSize);
    auto img = std::make_shared<OverlayImage>();
    img->width = W; img->height = H; img->renderedForScreenHeight = H;
    img->pixels.resize((size_t)W * H);
    int off = (H - h2) / 2;
    for (int y = 0; y < H; ++y) {
        int sy = std::clamp(y - off, 0, h2 - 1);   // above / below: the first / last row of the background
        std::copy(inner->pixels.begin() + (size_t)sy * W, inner->pixels.begin() + (size_t)(sy + 1) * W, img->pixels.begin() + (size_t)y * W);
    }
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
