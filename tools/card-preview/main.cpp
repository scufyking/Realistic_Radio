#include "card_preview.cpp"
#include <cstdio>
std::shared_ptr<OverlayImage> LoadRaw(const char* p) {
    FILE* f = fopen(p, "rb"); if (!f) return nullptr;
    auto img = std::make_shared<OverlayImage>(); int wh[2]; fread(wh, 4, 2, f); img->width = wh[0]; img->height = wh[1];
    img->pixels.resize((size_t)wh[0] * wh[1]); fread(img->pixels.data(), 4, img->pixels.size(), f); fclose(f); return img;
}
void Save(const std::shared_ptr<OverlayImage>& img, const char* p) {
    FILE* f = fopen(p, "wb"); int wh[2] = {img->width, img->height}; fwrite(wh, 4, 2, f); fwrite(img->pixels.data(), 4, img->pixels.size(), f); fclose(f);
}
int main() {
    const int W = 819, H = 410;   // MAN TGX visible area (1024 * 0.8 x 1024 * 0.4)
    auto logo = LoadRaw("logo.raw"), cover = LoadRaw("cover.raw");
    OverlayInfo r; r.source = L"Radio"; r.headline = L"Jetzt läuft:"; r.title = L"ROCK ANTENNE"; r.subtitle = L"Deutschland"; r.detail = L"Sender 3 / 41"; r.bars = 4; r.favourite = true;
    Save(RenderScreenCard(r, logo.get(), W, H), "c1.raw");
    OverlayInfo r2 = r; r2.title = L"Radio Bollerwagen"; r2.favourite = false; r2.bars = 2; r2.subtitle = L"Deutschland  •  verbinde…";
    Save(RenderScreenCard(r2, nullptr, W, H), "c2.raw");
    OverlayInfo m; m.source = L"USB"; m.media = true; m.headline = L"Titel 3 / 12"; m.title = L"Highway Through the Night (Extended Version)"; m.subtitle = L"The Midnight Truckers"; m.detail = L"Long Haul"; m.posSec = 83; m.lenSec = 247;
    Save(RenderScreenCard(m, cover.get(), W, H), "c3.raw");
    OverlayInfo m2 = m; m2.title = L"Mein Lied"; m2.subtitle = L"Unbekannt"; m2.detail = L""; m2.paused = true; m2.headline = L"Titel 7 / 12  •  Pause"; m2.posSec = 12; m2.lenSec = 185; m2.source = L"CD";
    Save(RenderScreenCard(m2, nullptr, W, H), "c4.raw");
    OverlayInfo v; v.source = L"Radio"; v.volume = 65; v.volumeLabel = L"Lautstärke";
    Save(RenderScreenCard(v, nullptr, W, H), "c5.raw");
    OverlayInfo v2 = v; v2.muted = true; v2.volumeLabel = L"Stumm"; v2.source = L"USB";
    Save(RenderScreenCard(v2, nullptr, W, H), "c6.raw");
}
