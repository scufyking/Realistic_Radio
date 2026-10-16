#include "shim.h"
#include <map>
std::vector<void*> g_fonts;
static FT_Library lib;
static std::map<std::string, FT_Face> faces;
static FT_Face Face(const char* path) {
    if (!lib) FT_Init_FreeType(&lib);
    auto it = faces.find(path); if (it != faces.end()) return it->second;
    FT_Face f; if (FT_New_Face(lib, path, 0, &f)) { fprintf(stderr, "font %s missing\n", path); exit(1); }
    faces[path] = f; return f;
}
static FT_Face FaceFor(int weight) {
    const char* p = weight >= 700 ? "/usr/share/fonts/opentype/inter/Inter-Bold.otf" : weight >= 600 ? "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf" :
                    weight <= 300 ? "/usr/share/fonts/opentype/inter/Inter-Light.otf" : "/usr/share/fonts/opentype/inter/Inter-Regular.otf";
    return Face(p);
}
HFONT CreateFontW(int h, int, int, int, int weight, int, int, int, int, int, int, int, int, const wchar_t*) {
    auto* f = new FontObj{h < 0 ? -h : h, weight}; g_fonts.push_back(f); return f;
}
struct G { FT_Face face; };
static FT_Face Pick(FontObj* f, wchar_t c) {
    FT_Face a = FaceFor(f->weight);
    if (FT_Get_Char_Index(a, c)) return a;
    return Face(f->weight >= 600 ? "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
}
static int Adv(FontObj* f, wchar_t c) {
    FT_Face fc = Pick(f, c); FT_Set_Pixel_Sizes(fc, 0, f->px);
    FT_Load_Char(fc, c, FT_LOAD_DEFAULT); return (int)(fc->glyph->advance.x >> 6);
}
static int Width(FontObj* f, const std::wstring& s) { int w = 0; for (wchar_t c : s) w += Adv(f, c); return w; }
static int LineH(FontObj* f) { return (int)std::lround(f->px * 1.33); }
static void Draw(HDC dc, FontObj* f, const std::wstring& s, int x, int y, const RECT& clip) {
    int base = y + (int)std::lround(f->px * 1.07);
    for (wchar_t c : s) {
        FT_Face fc = Pick(f, c); FT_Set_Pixel_Sizes(fc, 0, f->px);
        FT_Load_Char(fc, c, FT_LOAD_RENDER);
        auto& bm = fc->glyph->bitmap;
        int gx = x + fc->glyph->bitmap_left, gy = base - fc->glyph->bitmap_top;
        for (unsigned yy = 0; yy < bm.rows; ++yy) for (unsigned xx = 0; xx < bm.width; ++xx) {
            int X = gx + xx, Y = gy + yy;
            if (X < clip.left || X >= clip.right || Y < clip.top || Y >= clip.bottom) continue;
            if (X < 0 || Y < 0 || X >= dc->bmp->w || Y >= dc->bmp->h) continue;
            uint32_t v = bm.buffer[yy * bm.pitch + xx];
            uint32_t& p = dc->bmp->bits[(size_t)Y * dc->bmp->w + X];
            uint32_t o = p & 255; if (v > o) p = (v << 16) | (v << 8) | v;
        }
        x += (int)(fc->glyph->advance.x >> 6);
    }
}
static std::wstring Ellipsis(FontObj* f, std::wstring s, int maxW) {
    if (Width(f, s) <= maxW) return s;
    while (!s.empty() && Width(f, s + L"…") > maxW) s.pop_back();
    return s + L"…";
}
int DrawTextW(HDC dc, const wchar_t* str, int n, RECT* r, UINT fmt) {
    FontObj* f = dc->font; std::wstring s(str, n);
    int lh = LineH(f);
    if (fmt & DT_WORDBREAK) {
        std::vector<std::wstring> lines; std::wstring cur, word;
        int maxW = r->right - r->left;
        auto flushWord = [&]() {
            std::wstring t = cur.empty() ? word : cur + L" " + word;
            if (!cur.empty() && Width(f, t) > maxW) { lines.push_back(cur); cur = word; } else cur = t;
            word.clear();
        };
        for (wchar_t c : s) { if (c == L' ') flushWord(); else word.push_back(c); }
        flushWord(); if (!cur.empty()) lines.push_back(cur);
        if (fmt & DT_CALCRECT) { r->bottom = r->top + lh * (int)lines.size(); return r->bottom - r->top; }
        int y = r->top;
        for (auto& l : lines) {
            if ((fmt & DT_EDITCONTROL) && y + lh > r->bottom) break;
            Draw(dc, f, (fmt & DT_END_ELLIPSIS) ? Ellipsis(f, l, maxW) : l, r->left, y, *r); y += lh;
        }
        return y - r->top;
    }
    if (fmt & DT_CALCRECT) { r->right = r->left + Width(f, s); r->bottom = r->top + lh; return lh; }
    std::wstring t = (fmt & DT_END_ELLIPSIS) ? Ellipsis(f, s, r->right - r->left) : s;
    int w = Width(f, t);
    int x = (fmt & DT_CENTER) ? r->left + (r->right - r->left - w) / 2 : (fmt & DT_RIGHT) ? r->right - w : r->left;
    int y = (fmt & DT_VCENTER) ? r->top + (r->bottom - r->top - lh) / 2 : r->top;
    Draw(dc, f, t, x, y, *r);
    return lh;
}
