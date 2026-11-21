// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Scufy OS (1.8.0): the pictures of the infotainment system on the navigation screen. Included at the end of
// OverlayImage.cpp (uses its drawing helpers: Canvas, Gdi, DrawText, BlitLogo, DrawIcon ...).
// Look: dark graphite glass, a dock at the side (clock, four app buttons, what plays), the content next to it.

namespace {
struct OsPal {
    float bgTop[3] = {0.075f, 0.085f, 0.115f}, bgBot[3] = {0.032f, 0.037f, 0.052f};
    float panel[3] = {0.115f, 0.13f, 0.165f}, panel2[3] = {0.17f, 0.19f, 0.235f};
    float text[3] = {0.95f, 0.96f, 0.98f}, sub[3] = {0.68f, 0.71f, 0.77f}, muted[3] = {0.47f, 0.50f, 0.57f};
};

// the four built-in app colours: navigation, radio, music, settings
const float kAppColor[5][3] = {{0.5f, 0.5f, 0.55f}, {0.11f, 0.72f, 0.52f}, {1.0f, 0.52f, 0.14f}, {0.97f, 0.26f, 0.44f}, {0.42f, 0.46f, 0.54f}};

void OsIcon(Canvas& cv, int icon, float cx, float cy, float s, const float c[3]) {
    const float r = c[0], g = c[1], b = c[2], w = std::max(1.5f, s * 0.11f);
    switch (icon) {
    case 1:   // navigation arrow
        cv.Poly({{cx, cy - 0.44f * s}, {cx + 0.34f * s, cy + 0.40f * s}, {cx, cy + 0.20f * s}, {cx - 0.34f * s, cy + 0.40f * s}}, r, g, b);
        break;
    case 2: DrawIcon(cv, Icon::Radio, cx, cy, s, c); break;
    case 3:   // two notes with a beam
        cv.Circle(cx - 0.20f * s, cy + 0.27f * s, 0.14f * s, r, g, b);
        cv.Circle(cx + 0.24f * s, cy + 0.17f * s, 0.14f * s, r, g, b);
        cv.Line(cx - 0.07f * s, cy + 0.27f * s, cx - 0.07f * s, cy - 0.30f * s, w, r, g, b);
        cv.Line(cx + 0.37f * s, cy + 0.17f * s, cx + 0.37f * s, cy - 0.40f * s, w, r, g, b);
        cv.Line(cx - 0.07f * s, cy - 0.30f * s, cx + 0.37f * s, cy - 0.40f * s, w * 1.6f, r, g, b);
        break;
    case 4: DrawIcon(cv, Icon::Gear, cx, cy, s, c); break;
    case 5: DrawIcon(cv, Icon::Home, cx, cy, s, c); break;
    case 6:   // back chevron
        cv.Line(cx + 0.12f * s, cy - 0.32f * s, cx - 0.18f * s, cy, w * 1.2f, r, g, b);
        cv.Line(cx - 0.18f * s, cy, cx + 0.12f * s, cy + 0.32f * s, w * 1.2f, r, g, b);
        break;
    case 7: case 8: {   // minus / plus
        cv.Line(cx - 0.28f * s, cy, cx + 0.28f * s, cy, w * 1.2f, r, g, b);
        if (icon == 8) cv.Line(cx, cy - 0.28f * s, cx, cy + 0.28f * s, w * 1.2f, r, g, b);
        break; }
    case 9:   // play
        cv.Poly({{cx - 0.26f * s, cy - 0.36f * s}, {cx + 0.36f * s, cy}, {cx - 0.26f * s, cy + 0.36f * s}}, r, g, b);
        break;
    case 10:  // pause
        cv.Round((int)(cx - 0.28f * s), (int)(cy - 0.34f * s), std::max(1, (int)(0.2f * s)), (int)(0.68f * s), 1.f, r, g, b);
        cv.Round((int)(cx + 0.08f * s), (int)(cy - 0.34f * s), std::max(1, (int)(0.2f * s)), (int)(0.68f * s), 1.f, r, g, b);
        break;
    case 11: case 12: {   // previous / next (two triangles and a bar)
        float d = icon == 12 ? 1.f : -1.f;
        cv.Poly({{cx - d * 0.36f * s, cy - 0.3f * s}, {cx + d * 0.02f * s, cy}, {cx - d * 0.36f * s, cy + 0.3f * s}}, r, g, b);
        cv.Poly({{cx - d * 0.02f * s, cy - 0.3f * s}, {cx + d * 0.36f * s, cy}, {cx - d * 0.02f * s, cy + 0.3f * s}}, r, g, b);
        cv.Line(cx + d * 0.40f * s, cy - 0.3f * s, cx + d * 0.40f * s, cy + 0.3f * s, w, r, g, b);
        break; }
    default: break;
    }
}

// tile of an app: coloured rounded square with a little gloss, the icon (or picture / letter) in white
void OsAppTile(Canvas& cv, Gdi& mask, std::vector<uint32_t>& px, int W, const OsTile& t, int x, int y, int T) {
    const float rad = T * 0.25f;
    cv.Round(x, y + std::max(1, T / 24), T, T, rad, 0.f, 0.f, 0.f, 0.35f);   // shadow
    cv.Round(x, y, T, T, rad, t.color[0], t.color[1], t.color[2]);
    for (int yy = 0; yy < T / 2; ++yy) {   // light from above
        float a = 0.13f * (1.f - yy / (T / 2.f));
        for (int xx = 0; xx < T; ++xx) cv.Blend(x + xx, y + yy, RoundedCoverage(xx, yy, T, T, rad), 1.f, 1.f, 1.f, a);
    }
    const float white[3] = {1.f, 1.f, 1.f};
    if (t.icon > 0) OsIcon(cv, t.icon, x + T / 2.f, y + T / 2.f, T * 0.52f, white);
    else if (t.img) BlitLogo(px, W, t.img.get(), x + T / 6, y + T / 6, T - T / 3, T - T / 3);
    else {
        HFONT f = MakeFont((int)(T * 0.42f), FW_BOLD);
        DrawText(mask, px, f, Initials(t.label), RECT{x, y, x + T, y + T}, 1.f, 1.f, 1.f, DT_CENTER);
        DeleteObject(f);
    }
}

// station logo / cover / initials on a tile
void OsArt(Canvas& cv, Gdi& mask, std::vector<uint32_t>& px, int W, const ScufyView& v, const OsPal& pal, int x, int y, int T) {
    const OverlayImage* logo = v.logo.get();
    const float rad = T * 0.18f;
    if (logo && !v.now.media) {
        cv.Round(x, y, T, T, rad, 0.95f, 0.95f, 0.96f);
        BlitLogo(px, W, logo, x + T / 8, y + T / 8, T - T / 4, T - T / 4);
    } else if (logo) {
        cv.Round(x, y, T, T, rad, pal.panel2[0], pal.panel2[1], pal.panel2[2]);
        BlitLogo(px, W, logo, x + T / 20, y + T / 20, T - T / 10, T - T / 10);
    } else {
        cv.Round(x, y, T, T, rad, pal.panel2[0], pal.panel2[1], pal.panel2[2]);
        if (T >= 80) cv.Glow(x + T * 0.3f, y + T * 0.25f, T * 0.6f, v.accent[0], v.accent[1], v.accent[2], 0.30f);   // not on the small tile in the dock (over the map)
        if (v.now.media) { const float c[3] = {v.accent[0], v.accent[1], v.accent[2]}; OsIcon(cv, 3, x + T / 2.f, y + T / 2.f, T * 0.5f, c); }
        else {
            HFONT f = MakeFont(std::max(8, (int)(T * 0.34f)), FW_BOLD);
            DrawText(mask, px, f, Initials(v.now.title), RECT{x, y, x + T, y + T}, v.accent[0], v.accent[1], v.accent[2], DT_CENTER);
            DeleteObject(f);
        }
    }
}

bool OsHover(const ScufyView& v, int act, int idx) { return v.hoverAct == act && v.hoverIdx == idx; }

// round button: filled with the accent (primary) or glass; ring when highlighted
void OsRoundButton(Canvas& cv, const ScufyView& v, const OsPal& pal, float cx, float cy, float d, int icon, bool primary, bool hover) {
    if (hover && primary) cv.Circle(cx, cy, d / 2 + std::max(2.f, d * 0.08f), 1.f, 1.f, 1.f);
    else if (hover) cv.Circle(cx, cy, d / 2 + std::max(2.f, d * 0.08f), v.accent[0], v.accent[1], v.accent[2]);
    if (primary) cv.Circle(cx, cy, d / 2, v.accent[0], v.accent[1], v.accent[2]);
    else cv.Circle(cx, cy, d / 2, pal.panel2[0], pal.panel2[1], pal.panel2[2]);
    const float white[3] = {1.f, 1.f, 1.f};
    OsIcon(cv, icon, cx, cy, d * 0.5f, white);
}
// pill button with text
void OsPill(Canvas& cv, Gdi& mask, std::vector<uint32_t>& px, const ScufyView& v, const OsPal& pal, HFONT f, const std::wstring& text, RECT r, bool primary, bool hover) {
    const int h = r.bottom - r.top, w = r.right - r.left;
    if (hover) cv.Round(r.left - 3, r.top - 3, w + 6, h + 6, h / 2.f + 3, primary ? 1.f : v.accent[0], primary ? 1.f : v.accent[1], primary ? 1.f : v.accent[2]);
    if (primary) cv.Round(r.left, r.top, w, h, h / 2.f, v.accent[0], v.accent[1], v.accent[2]);
    else cv.Round(r.left, r.top, w, h, h / 2.f, pal.panel2[0], pal.panel2[1], pal.panel2[2]);
    DrawText(mask, px, f, text, r, pal.text[0], pal.text[1], pal.text[2], DT_CENTER);
}
} // namespace

std::shared_ptr<OverlayImage> RenderScufyOS(const ScufyView& v, int W, int H, std::vector<OsSpot>* spots, RECT* appArea, int* visibleRows) {
    FontScope fontScope(L"Segoe UI");
    OsPal pal;
    auto img = NewCard(W, H);
    auto& px = img->pixels;
    Canvas cv{px, W, H};
    Gdi mask(W, H);
    const float u = std::max(0.3f, std::min(W / 820.f, H / 410.f));
    auto U = [&](float x) { return std::max(1, (int)std::lround(x * u)); };
    const float* A = v.accent;
    std::vector<OsSpot> sp;
    auto spot = [&](RECT r, int act, int idx) { sp.push_back({r, act, idx}); };
    if (appArea) *appArea = RECT{};
    if (visibleRows) *visibleRows = 0;
    const bool navi = v.screen == OsScreen::Navi;

    // background: dark gradient with a soft glow of the accent colour (the navigation app keeps the map visible)
    if (!navi) {
        for (int y = 0; y < H; ++y) {
            float t = (float)y / std::max(1, H - 1);
            uint32_t c = Pack(1.f, pal.bgTop[0] + (pal.bgBot[0] - pal.bgTop[0]) * t, pal.bgTop[1] + (pal.bgBot[1] - pal.bgTop[1]) * t, pal.bgTop[2] + (pal.bgBot[2] - pal.bgTop[2]) * t);
            std::fill(px.begin() + (size_t)y * W, px.begin() + (size_t)(y + 1) * W, c);
        }
        cv.Glow(v.dockRight ? W * 0.12f : W * 0.88f, -H * 0.15f, W * 0.55f, A[0], A[1], A[2], 0.16f);
        cv.Glow(v.dockRight ? W * 0.95f : W * 0.05f, H * 1.1f, W * 0.35f, A[0], A[1], A[2], 0.07f);
    }

    // ---- dock: clock, route, four app buttons, what plays ----
    const int m = U(8), dockW = U(112);
    const int dx = v.dockRight ? W - m - dockW : m, dy = m, dh = H - 2 * m;
    cv.Round(dx, dy + U(2), dockW, dh, (float)U(18), 0.f, 0.f, 0.f, navi ? 0.45f : 0.25f);
    cv.Round(dx, dy, dockW, dh, (float)U(18), pal.panel[0], pal.panel[1], pal.panel[2], navi ? 0.96f : 0.82f);
    cv.Round(dx + U(10), dy + U(1), dockW - U(20), 1, 0.f, 1.f, 1.f, 1.f, 0.08f);
    HFONT fClock = MakeFont(U(30), FW_SEMIBOLD), fDay = MakeFont(U(12), FW_NORMAL), fSmall = MakeFont(U(12), FW_SEMIBOLD), fTiny = MakeFont(U(11), FW_NORMAL);
    int y = dy + U(8);
    DrawText(mask, px, fClock, v.clock, RECT{dx, y, dx + dockW, y + U(36)}, pal.text[0], pal.text[1], pal.text[2], DT_CENTER);
    y += U(36);
    DrawText(mask, px, fDay, v.day, RECT{dx, y, dx + dockW, y + U(16)}, pal.muted[0], pal.muted[1], pal.muted[2], DT_CENTER);
    y += U(20);
    if (!v.navDist.empty()) {   // the route: distance and time to the destination
        cv.Round(dx + U(10), y, dockW - U(20), U(34), (float)U(9), A[0], A[1], A[2], 0.22f);
        DrawText(mask, px, fSmall, v.navDist, RECT{dx + U(10), y + U(2), dx + dockW - U(10), y + U(17)}, pal.text[0], pal.text[1], pal.text[2], DT_CENTER);
        DrawText(mask, px, fTiny, v.navTime, RECT{dx + U(10), y + U(16), dx + dockW - U(10), y + U(32)}, pal.sub[0], pal.sub[1], pal.sub[2], DT_CENTER);
        y += U(40);
    }
    const int miniH = U(76);
    const int nB = 4, B = U(44);
    int gap = std::clamp((dy + dh - miniH - U(6) - y - nB * B) / (nB + 1), U(4), U(12));
    y += gap;
    static const int dockIcons[4] = {5, 1, 2, 3};
    int active = v.screen == OsScreen::Home ? 0 : v.screen == OsScreen::Navi ? 1 : v.screen == OsScreen::Radio ? 2 : v.screen == OsScreen::Music ? 3 : -1;
    for (int i = 0; i < nB; ++i) {
        int bx = dx + (dockW - B) / 2;
        bool hover = OsHover(v, OA_Dock, i);
        if (hover) cv.Round(bx - U(3), y - U(3), B + U(6), B + U(6), (float)U(15), A[0], A[1], A[2]);
        if (i == active) cv.Round(bx, y, B, B, (float)U(13), A[0], A[1], A[2]);
        else cv.Round(bx, y, B, B, (float)U(13), hover ? 0.10f : pal.panel2[0], hover ? 0.11f : pal.panel2[1], hover ? 0.14f : pal.panel2[2]);
        const float ic[3] = {1.f, 1.f, 1.f}, icDim[3] = {0.82f, 0.85f, 0.9f};
        OsIcon(cv, dockIcons[i], bx + B / 2.f, y + B / 2.f, B * 0.5f, i == active ? ic : icDim);
        spot(RECT{bx - U(4), y - U(2), bx + B + U(4), y + B + U(2)}, OA_Dock, i);
        y += B + gap;
    }
    {   // what plays: logo tile and name (opens Radio / Music)
        const int T = U(42), my = dy + dh - miniH;
        RECT mr{dx + U(6), my, dx + dockW - U(6), dy + dh - U(6)};
        if (OsHover(v, OA_Dock, 4)) cv.Round(mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top, (float)U(12), A[0], A[1], A[2], 0.35f);
        OsArt(cv, mask, px, W, v, pal, dx + (dockW - T) / 2, my + U(4), T);
        if (v.playing) {   // little "playing" bars on the tile corner
            const int bx = dx + (dockW + T) / 2 - U(6), by = my + U(4) + T;
            cv.Circle((float)bx, (float)by - U(2), (float)U(8), A[0], A[1], A[2]);
            for (int k = 0; k < 3; ++k) cv.Round(bx - U(5) + k * U(4), by - U(2) - (k == 1 ? U(5) : U(3)), U(2), k == 1 ? U(8) : U(5), 0.5f, 1.f, 1.f, 1.f);
        }
        std::wstring name = v.now.title.empty() ? std::wstring(L10n::Pick(L"Radio", L"Radio", v.en)) : v.now.title;
        DrawText(mask, px, fTiny, name, RECT{dx + U(6), my + U(4) + T + U(4), dx + dockW - U(6), my + U(4) + T + U(20)}, pal.sub[0], pal.sub[1], pal.sub[2], DT_CENTER);
        spot(mr, OA_Dock, 4);
    }
    DeleteObject(fClock); DeleteObject(fDay); DeleteObject(fSmall); DeleteObject(fTiny);

    // ---- content ----
    const int cx0 = v.dockRight ? m + U(4) : dx + dockW + U(14), cx1 = v.dockRight ? dx - U(14) : W - m - U(4);
    const int cy0 = m + U(4), cy1 = H - m - U(2);
    const int cw = cx1 - cx0, ch = cy1 - cy0;
    HFONT fTitle = MakeFont(U(26), FW_SEMIBOLD), fHead = MakeFont(U(20), FW_SEMIBOLD), fBody = MakeFont(U(15), FW_NORMAL), fBodyB = MakeFont(U(15), FW_SEMIBOLD);
    HFONT fLabel = MakeFont(U(13), FW_NORMAL), fChip = MakeFont(U(12), FW_BOLD);

    // header of a sub screen: back button + title (the dock buttons are the main way, this is for the mouse)
    auto header = [&](const std::wstring& title) {
        const int d = U(32);
        OsRoundButton(cv, v, pal, cx0 + d / 2.f, cy0 + d / 2.f, (float)d, 6, false, OsHover(v, OA_Back, 0));
        spot(RECT{cx0 - U(2), cy0 - U(2), cx0 + d + U(2), cy0 + d + U(2)}, OA_Back, 0);
        DrawText(mask, px, fHead, title, RECT{cx0 + d + U(12), cy0, cx1, cy0 + d}, pal.text[0], pal.text[1], pal.text[2]);
        return cy0 + d + U(10);
    };
    // a list of rows (Music, Settings, add-on lists); act = action of a click on row i
    auto list = [&](int top, int act) {
        int yy = top, shown = 0;
        const int bottom = cy1 - (v.hint.empty() ? 0 : U(20));
        const int n = (int)v.rows.size();
        for (int i = std::max(0, v.scroll); i < n; ++i) {
            const OsRow& r = v.rows[i];
            const bool twoLines = r.type == "row" && !r.text.empty();
            int rh = r.type == "section" ? U(26) : r.type == "text" ? U(44) : twoLines ? U(48) : U(40);
            if (yy + rh > bottom && shown > 0) break;
            RECT rr{cx0, yy, cx1 - U(10), yy + rh - U(4)};
            const bool hover = OsHover(v, act, i) || (act == OA_Item && OsHover(v, OA_Search, i));
            if (r.type == "section") {
                std::wstring t = r.title; if (!t.empty()) CharUpperBuffW(&t[0], (DWORD)t.size());
                DrawText(mask, px, fChip, t, RECT{rr.left + U(4), rr.top + U(4), rr.right, rr.bottom}, pal.muted[0], pal.muted[1], pal.muted[2]);
            } else if (r.type == "text") {
                DrawText2(mask, px, fLabel, r.text.empty() ? r.title : r.text, RECT{rr.left + U(4), rr.top + U(2), rr.right, rr.bottom}, pal.sub[0], pal.sub[1], pal.sub[2]);
            } else if (r.type == "button") {
                OsPill(cv, mask, px, v, pal, fBodyB, r.title, RECT{rr.left, rr.top, rr.left + std::min((int)(rr.right - rr.left), U(260)), rr.bottom}, r.enabled, hover);
                spot(rr, act, i);
            } else if (r.type == "search") {
                if (hover) cv.Round(rr.left - 2, rr.top - 2, rr.right - rr.left + 4, rr.bottom - rr.top + 4, (float)U(12), A[0], A[1], A[2]);
                cv.Round(rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top, (float)U(10), 0.06f, 0.07f, 0.09f);
                bool empty = v.search.empty();
                DrawText(mask, px, fBody, empty ? r.value : v.search + (hover ? L"|" : L""), RECT{rr.left + U(14), rr.top, rr.right - U(10), rr.bottom},
                         empty ? pal.muted[0] : pal.text[0], empty ? pal.muted[1] : pal.text[1], empty ? pal.muted[2] : pal.text[2]);
                spot(rr, OA_Search, i);
            } else {
                if (hover) cv.Round(rr.left - 2, rr.top - 2, rr.right - rr.left + 4, rr.bottom - rr.top + 4, (float)U(12), A[0], A[1], A[2]);
                cv.Round(rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top, (float)U(10), pal.panel[0] + (r.active ? 0.03f : 0.f), pal.panel[1] + (r.active ? 0.03f : 0.f), pal.panel[2] + (r.active ? 0.04f : 0.f), 0.95f);
                if (r.active) cv.Round(rr.left + U(5), rr.top + U(8), U(4), rr.bottom - rr.top - U(16), (float)U(2), A[0], A[1], A[2]);
                int vx = rr.right - U(14) - (r.arrow ? U(14) : 0);
                int vw = r.value.empty() ? 0 : std::min((int)Measure(mask.dc, fLabel, r.value).cx, (int)((rr.right - rr.left) * 0.45f));
                const float* tc = r.enabled ? pal.text : pal.muted;
                if (twoLines) {
                    DrawText(mask, px, fBodyB, r.title, RECT{rr.left + U(16), rr.top + U(4), vx - vw - U(10), rr.top + U(24)}, tc[0], tc[1], tc[2]);
                    DrawText(mask, px, fLabel, r.text, RECT{rr.left + U(16), rr.top + U(22), vx - vw - U(10), rr.bottom - U(3)}, pal.sub[0], pal.sub[1], pal.sub[2]);
                } else DrawText(mask, px, fBodyB, r.title, RECT{rr.left + U(16), rr.top, vx - vw - U(10), rr.bottom}, tc[0], tc[1], tc[2]);
                if (vw) DrawText(mask, px, fLabel, r.value, RECT{vx - vw, rr.top, vx, rr.bottom}, r.active ? A[0] : pal.sub[0], r.active ? A[1] : pal.sub[1], r.active ? A[2] : pal.sub[2], DT_RIGHT);
                if (r.arrow) { const float c[3] = {pal.muted[0], pal.muted[1], pal.muted[2]}; float ax = rr.right - U(16.f), ay = (rr.top + rr.bottom) / 2.f;
                               cv.Line(ax - U(4), ay - U(6), ax + U(2), ay, (float)U(2), c[0], c[1], c[2]); cv.Line(ax + U(2), ay, ax - U(4), ay + U(6), (float)U(2), c[0], c[1], c[2]); }
                if (r.enabled) spot(rr, act, i);
            }
            yy += rh; ++shown;
        }
        if (visibleRows) *visibleRows = shown;
        if (n > shown && shown > 0) {   // scroll bar
            int th = bottom - top, bh = std::max(U(20), th * shown / n), by = top + (th - bh) * std::max(0, v.scroll) / std::max(1, n - shown);
            cv.Round(cx1 - U(4), top, U(4), th, (float)U(2), 1.f, 1.f, 1.f, 0.08f);
            cv.Round(cx1 - U(4), by, U(4), bh, (float)U(2), A[0], A[1], A[2], 0.9f);
        }
        if (!v.hint.empty()) DrawText(mask, px, fLabel, v.hint, RECT{cx0, cy1 - U(18), cx1, cy1}, pal.muted[0], pal.muted[1], pal.muted[2]);
    };
    // play controls: previous, play / pause, next (radio: source, band; always: volume)
    auto controls = [&](int x0, int x1, int cy, bool radio) {
        const int d = U(44), dp = U(52), g = U(12);
        float x = (float)x0;
        OsRoundButton(cv, v, pal, x + d / 2.f, (float)cy, (float)d, 11, false, OsHover(v, OA_Prev, 0)); spot(RECT{(int)x, cy - d / 2, (int)x + d, cy + d / 2}, OA_Prev, 0); x += d + g;
        OsRoundButton(cv, v, pal, x + dp / 2.f, (float)cy, (float)dp, v.playing ? 10 : 9, true, OsHover(v, OA_PlayPause, 0)); spot(RECT{(int)x, cy - dp / 2, (int)x + dp, cy + dp / 2}, OA_PlayPause, 0); x += dp + g;
        OsRoundButton(cv, v, pal, x + d / 2.f, (float)cy, (float)d, 12, false, OsHover(v, OA_Next, 0)); spot(RECT{(int)x, cy - d / 2, (int)x + d, cy + d / 2}, OA_Next, 0); x += d + g * 2;
        const int ph = U(34);
        auto pill = [&](const std::wstring& t, int act) {
            int w = (int)Measure(mask.dc, fBodyB, t).cx + U(30);
            if (x + w > x1 - 2 * (U(38) + g)) return;
            RECT r{(int)x, cy - ph / 2, (int)x + w, cy + ph / 2};
            OsPill(cv, mask, px, v, pal, fBodyB, t, r, false, OsHover(v, act, 0)); spot(r, act, 0); x += w + g;
        };
        pill(L10n::Pick(L"Quelle", L"Source", v.en), OA_Source);
        if (radio) pill(L"FM / DAB+", OA_Band);
        const int dv = U(38);
        float vx = (float)x1 - dv;
        OsRoundButton(cv, v, pal, vx + dv / 2.f, (float)cy, (float)dv, 8, false, OsHover(v, OA_VolUp, 0)); spot(RECT{(int)vx, cy - dv / 2, (int)vx + dv, cy + dv / 2}, OA_VolUp, 0);
        vx -= dv + g;
        OsRoundButton(cv, v, pal, vx + dv / 2.f, (float)cy, (float)dv, 7, false, OsHover(v, OA_VolDown, 0)); spot(RECT{(int)vx, cy - dv / 2, (int)vx + dv, cy + dv / 2}, OA_VolDown, 0);
    };

    switch (v.screen) {
    case OsScreen::Home: {
        const int n = (int)v.apps.size();
        if (n == 0) break;
        const int cols = n <= 4 ? n : n <= 8 ? 4 : 5, rows = (n + cols - 1) / cols;
        const int labelH = U(24), gx = U(22), gy = U(14);
        int T = std::min({U(86), (cw - (cols - 1) * gx) / cols, (ch - rows * labelH - (rows - 1) * gy) / std::max(1, rows)});
        T = std::max(U(30), T);
        const int gridW = cols * T + (cols - 1) * gx, gridH = rows * (T + labelH) + (rows - 1) * gy;
        const int gx0 = cx0 + (cw - gridW) / 2, gy0 = cy0 + (ch - gridH) / 2;
        for (int i = 0; i < n; ++i) {
            int c = i % cols, r = i / cols;
            int x = gx0 + c * (T + gx), yy = gy0 + r * (T + labelH + gy);
            if (OsHover(v, OA_App, i)) cv.Round(x - U(5), yy - U(5), T + U(10), T + U(10), T * 0.25f + U(5), A[0], A[1], A[2]);
            if (OsHover(v, OA_App, i)) cv.Round(x - U(2), yy - U(2), T + U(4), T + U(4), T * 0.25f + U(2), pal.bgTop[0], pal.bgTop[1], pal.bgTop[2]);
            OsAppTile(cv, mask, px, W, v.apps[i], x, yy, T);
            DrawText(mask, px, fLabel, v.apps[i].label, RECT{x - gx / 2, yy + T + U(4), x + T + gx / 2, yy + T + labelH}, pal.text[0], pal.text[1], pal.text[2], DT_CENTER);
            spot(RECT{x - U(4), yy - U(4), x + T + U(4), yy + T + labelH}, OA_App, i);
        }
        break; }
    case OsScreen::Navi: break;   // the game's navigation shows through
    case OsScreen::Radio: {
        const bool media = v.now.media;
        const bool tiles = !media && !v.now.tiles.empty();
        const int tilesH = tiles ? std::min(U(52), ch / 4) : media ? U(30) : 0;
        const int ctrlH = U(58);
        int T = std::min({ch - tilesH - ctrlH - U(16), (int)(cw * 0.32f), U(170)});
        T = std::max(U(40), T);
        const int blockH = T + U(10) + ctrlH + U(6) + tilesH;   // centred in the height that is there
        const int tx = cx0, ty = cy0 + std::max(U(4), (ch - blockH) / 2);
        OsArt(cv, mask, px, W, v, pal, tx, ty, T);
        int x0 = tx + T + U(18), yy = ty + std::max(0, (T - U(112)) / 2);
        std::wstring chip = v.now.source.empty() ? std::wstring(L"RADIO") : v.now.source;
        CharUpperBuffW(&chip[0], (DWORD)chip.size());
        int chipW = (int)Measure(mask.dc, fChip, chip).cx + U(16);
        cv.Round(x0, yy, chipW, U(20), (float)U(10), A[0], A[1], A[2], 0.25f);
        DrawText(mask, px, fChip, chip, RECT{x0, yy, x0 + chipW, yy + U(20)}, A[0] * 0.5f + 0.5f, A[1] * 0.5f + 0.5f, A[2] * 0.5f + 0.5f, DT_CENTER);
        if (!v.now.frequency.empty()) DrawText(mask, px, fLabel, v.now.frequency, RECT{x0 + chipW + U(10), yy, cx1, yy + U(20)}, pal.sub[0], pal.sub[1], pal.sub[2]);
        yy += U(26);
        DrawText(mask, px, fTitle, v.now.title, RECT{x0, yy, cx1, yy + U(36)}, pal.text[0], pal.text[1], pal.text[2]);
        yy += U(36);
        if (!v.now.song.empty()) { DrawText(mask, px, fBodyB, L"♪  " + v.now.song, RECT{x0, yy, cx1, yy + U(22)}, A[0] * 0.6f + 0.4f, A[1] * 0.6f + 0.4f, A[2] * 0.6f + 0.4f); yy += U(24); }
        DrawText(mask, px, fBody, v.now.subtitle, RECT{x0, yy, cx1, yy + U(22)}, pal.sub[0], pal.sub[1], pal.sub[2]);
        yy += U(22);
        if (!v.now.detail.empty()) DrawText(mask, px, fLabel, v.now.detail, RECT{x0, yy, cx1, yy + U(20)}, pal.muted[0], pal.muted[1], pal.muted[2]);
        controls(cx0, cx1, ty + T + U(10) + ctrlH / 2, !media);
        int by = ty + T + U(10) + ctrlH + U(6);
        if (tiles) {   // station buttons 1 - 9
            const int n = (int)v.now.tiles.size(), g = U(8);
            const int tw = (cw - (n - 1) * g) / std::max(1, n), th = std::clamp(cy1 - by, U(28), U(52));
            for (int i = 0; i < n; ++i) {
                const auto& t = v.now.tiles[i];
                int x = cx0 + i * (tw + g);
                bool hover = OsHover(v, OA_Station, t.button);
                if (hover) cv.Round(x - 2, by - 2, tw + 4, th + 4, (float)U(11), A[0], A[1], A[2]);
                if (t.active) cv.Round(x, by, tw, th, (float)U(9), A[0], A[1], A[2]);
                else cv.Round(x, by, tw, th, (float)U(9), pal.panel2[0], pal.panel2[1], pal.panel2[2], hover ? 1.f : 0.85f);
                DrawText(mask, px, fChip, std::to_wstring(t.button), RECT{x + U(7), by + U(3), x + tw, by + U(18)}, t.active ? 1.f : pal.muted[0], t.active ? 1.f : pal.muted[1], t.active ? 1.f : pal.muted[2]);
                DrawText(mask, px, fLabel, t.name.empty() ? std::wstring(L"–") : t.name, RECT{x + U(5), by + U(14), x + tw - U(5), by + th - U(2)}, pal.text[0], pal.text[1], pal.text[2], DT_CENTER);
                spot(RECT{x, by, x + tw, by + th}, OA_Station, t.button);
            }
        } else if (media && v.now.lenSec > 0) {   // track progress
            int bw = cw, py = by + U(8);
            float f = std::clamp(v.now.posSec / (float)v.now.lenSec, 0.f, 1.f);
            cv.Round(cx0, py, bw, U(6), (float)U(3), 1.f, 1.f, 1.f, 0.12f);
            cv.Round(cx0, py, std::max(U(6), (int)(bw * f)), U(6), (float)U(3), A[0], A[1], A[2]);
            DrawText(mask, px, fLabel, Clock(v.now.posSec), RECT{cx0, py + U(8), cx0 + U(80), py + U(26)}, pal.sub[0], pal.sub[1], pal.sub[2]);
            DrawText(mask, px, fLabel, Clock(v.now.lenSec), RECT{cx1 - U(80), py + U(8), cx1, py + U(26)}, pal.sub[0], pal.sub[1], pal.sub[2], DT_RIGHT);
        }
        break; }
    case OsScreen::Music: {
        int top = header(v.title);
        if (v.now.media) {   // what plays and its controls
            const int T = U(48);
            OsArt(cv, mask, px, W, v, pal, cx0, top, T);
            DrawText(mask, px, fBodyB, v.now.title, RECT{cx0 + T + U(12), top + U(2), cx1 - U(250), top + U(24)}, pal.text[0], pal.text[1], pal.text[2]);
            DrawText(mask, px, fLabel, v.now.subtitle, RECT{cx0 + T + U(12), top + U(24), cx1 - U(250), top + U(44)}, pal.sub[0], pal.sub[1], pal.sub[2]);
            const int d = U(38), cy = top + T / 2;
            float x = (float)cx1 - 3 * d - 2 * U(10);
            OsRoundButton(cv, v, pal, x + d / 2.f, (float)cy, (float)d, 11, false, OsHover(v, OA_Prev, 0)); spot(RECT{(int)x, cy - d / 2, (int)x + d, cy + d / 2}, OA_Prev, 0); x += d + U(10);
            OsRoundButton(cv, v, pal, x + d / 2.f, (float)cy, (float)d, v.playing ? 10 : 9, true, OsHover(v, OA_PlayPause, 0)); spot(RECT{(int)x, cy - d / 2, (int)x + d, cy + d / 2}, OA_PlayPause, 0); x += d + U(10);
            OsRoundButton(cv, v, pal, x + d / 2.f, (float)cy, (float)d, 12, false, OsHover(v, OA_Next, 0)); spot(RECT{(int)x, cy - d / 2, (int)x + d, cy + d / 2}, OA_Next, 0);
            top += T + U(12);
        }
        list(top, OA_Disc);
        break; }
    case OsScreen::Settings: list(header(v.title), OA_Setting); break;
    case OsScreen::AddonList: list(header(v.title), OA_Item); break;
    case OsScreen::AddonCanvas: {
        int top = header(v.title);
        RECT r{cx0, top, cx1, cy1};
        cv.Round(r.left, r.top, r.right - r.left, r.bottom - r.top, (float)U(12), 0.f, 0.f, 0.f, 0.6f);
        if (!v.appHasFrame) DrawText(mask, px, fBody, v.hint, r, pal.sub[0], pal.sub[1], pal.sub[2], DT_CENTER);
        if (appArea) *appArea = r;
        spot(r, OA_AppArea, 0);
        break; }
    }
    DeleteObject(fTitle); DeleteObject(fHead); DeleteObject(fBody); DeleteObject(fBodyB); DeleteObject(fLabel); DeleteObject(fChip);
    if (spots) *spots = std::move(sp);
    return img;
}

std::shared_ptr<OverlayImage> RenderScufyBoot(int W, int H, float t, const float A[3]) {
    FontScope fontScope(L"Segoe UI");
    auto img = NewCard(W, H);
    auto& px = img->pixels;
    std::fill(px.begin(), px.end(), Pack(1.f, 0.02f, 0.022f, 0.03f));
    Canvas cv{px, W, H};
    Gdi mask(W, H);
    const float u = std::max(0.3f, std::min(W / 820.f, H / 410.f));
    auto U = [&](float x) { return std::max(1, (int)std::lround(x * u)); };
    t = std::clamp(t, 0.f, 1.f);
    const float in = std::min(1.f, t / 0.25f), out = t > 0.85f ? std::max(0.f, 1.f - (t - 0.85f) / 0.15f) : 1.f, k = in * out;
    const float cx = W / 2.f, cy = H * 0.40f, R = (float)U(52);
    cv.Glow(cx, cy, R * 3.2f, A[0], A[1], A[2], 0.22f * k);
    // ring that fills up while the system "starts", an S in the middle
    cv.Arc(cx, cy, R, (float)U(6), -3.1416f, 3.1416f, A[0] * 0.25f * k, A[1] * 0.25f * k, A[2] * 0.25f * k);
    float sweep = std::min(1.f, t / 0.8f) * 6.2832f;
    if (sweep > 0.01f) {   // Arc() takes angles counter-clockwise from the right: start at the top, run clockwise
        float a1 = 1.5708f, a0 = a1 - sweep;
        if (a0 < -3.1416f) { cv.Arc(cx, cy, R, (float)U(6), -3.1416f, a1, A[0] * k, A[1] * k, A[2] * k); cv.Arc(cx, cy, R, (float)U(6), a0 + 6.2832f, 3.1416f, A[0] * k, A[1] * k, A[2] * k); }
        else cv.Arc(cx, cy, R, (float)U(6), a0, a1, A[0] * k, A[1] * k, A[2] * k);
    }
    HFONT fS = MakeFont(U(62), FW_BOLD), fName = MakeFont(U(30), FW_SEMIBOLD), fSub = MakeFont(U(13), FW_NORMAL);
    DrawText(mask, px, fS, L"S", RECT{(int)(cx - R), (int)(cy - R) - U(4), (int)(cx + R), (int)(cy + R) - U(4)}, k, k, k, DT_CENTER);
    DrawText(mask, px, fName, L"Scufy OS", RECT{0, (int)(cy + R + U(16)), W, (int)(cy + R + U(56))}, 0.96f * k, 0.97f * k, 0.99f * k, DT_CENTER);
    DrawText(mask, px, fSub, L"Realistic Radio " REALISTIC_RADIO_VERSION_W, RECT{0, (int)(cy + R + U(58)), W, (int)(cy + R + U(78))}, 0.45f * k, 0.48f * k, 0.55f * k, DT_CENTER);
    DeleteObject(fS); DeleteObject(fName); DeleteObject(fSub);
    return img;
}
