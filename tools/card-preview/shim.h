// Minimal GDI stand-in (FreeType) so the card renderer can be previewed on Linux. Preview only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <memory>
#include <ft2build.h>
#include FT_FREETYPE_H
typedef unsigned int UINT; typedef uint32_t DWORD; typedef long LONG;
struct RECT { LONG left, top, right, bottom; };
struct SIZE { LONG cx, cy; };
struct FontObj { int px, weight; };
struct BmpObj { uint32_t* bits; int w, h; };
struct DCObj { BmpObj* bmp = nullptr; FontObj* font = nullptr; };
typedef DCObj* HDC; typedef FontObj* HFONT; typedef BmpObj* HBITMAP; typedef void* HGDIOBJ;
struct BITMAPINFOHEADER { DWORD biSize; LONG biWidth, biHeight; int biPlanes, biBitCount, biCompression; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; };
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define TRANSPARENT 1
#define RGB(r,g,b) 0
#define FALSE 0
#define FW_LIGHT 300
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define DEFAULT_CHARSET 0
#define OUT_TT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 0
#define DEFAULT_PITCH 0
#define DT_CENTER 1
#define DT_RIGHT 2
#define DT_VCENTER 4
#define DT_WORDBREAK 0x10
#define DT_SINGLELINE 0x20
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define DT_EDITCONTROL 0x2000
#define DT_END_ELLIPSIS 0x8000
inline HDC CreateCompatibleDC(void*) { return new DCObj; }
inline void DeleteDC(HDC d) { delete d; }
inline HBITMAP CreateDIBSection(HDC, BITMAPINFO* bi, int, void** bits, void*, int) {
    auto* b = new BmpObj; b->w = bi->bmiHeader.biWidth; b->h = -bi->bmiHeader.biHeight; b->bits = new uint32_t[(size_t)b->w * b->h](); *bits = b->bits; return b; }
inline HGDIOBJ SelectObject(HDC d, void* o) {
    // fonts and bitmaps are distinguished by a tag kept in a side table
    extern std::vector<void*> g_fonts; if (!o) return nullptr;
    if (std::find(g_fonts.begin(), g_fonts.end(), o) != g_fonts.end()) { auto old = d->font; d->font = (FontObj*)o; return old; }
    auto old = d->bmp; d->bmp = (BmpObj*)o; return old; }
inline void DeleteObject(void*) {}
inline void SetBkMode(HDC, int) {}
inline void SetTextColor(HDC, int) {}
inline void GdiFlush() {}
HFONT CreateFontW(int h, int, int, int, int weight, int, int, int, int, int, int, int, int, const wchar_t*);
int DrawTextW(HDC dc, const wchar_t* s, int n, RECT* r, UINT fmt);
inline DWORD CharUpperBuffW(wchar_t* s, DWORD n) { for (DWORD i = 0; i < n; ++i) s[i] = towupper(s[i]); return n; }
