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
    OverlayInfo m; m.source = L"USB 2"; m.media = true; m.headline = L"Rock Music  \u2022  Titel 3 / 12"; m.title = L"Highway Through the Night (Extended Version)"; m.subtitle = L"The Midnight Truckers"; m.detail = L"Long Haul"; m.posSec = 83; m.lenSec = 247;
    Save(RenderScreenCard(m, cover.get(), W, H), "c3.raw");
    OverlayInfo m2 = m; m2.title = L"Mein Lied"; m2.subtitle = L"Unbekannt"; m2.detail = L""; m2.paused = true; m2.headline = L"YouTube Playlist  •  Titel 7 / 12  •  Pause"; m2.posSec = 12; m2.lenSec = 185; m2.source = L"USB 4";
    Save(RenderScreenCard(m2, nullptr, W, H), "c4.raw");
    OverlayInfo v; v.source = L"Radio"; v.volume = 65; v.volumeLabel = L"Lautstärke";
    Save(RenderScreenCard(v, nullptr, W, H), "c5.raw");
    OverlayInfo v2 = v; v2.muted = true; v2.volumeLabel = L"Stumm"; v2.source = L"USB";
    Save(RenderScreenCard(v2, nullptr, W, H), "c6.raw");
    MenuView mv; mv.title = L"Realistic Radio"; mv.subtitle = L"Einstellungen  •  by ScufyKing  •  v1.2.0";
    mv.pages = {L"Radio", L"Empfang", L"Navi-Bildschirm", L"Popup-Overlay", L"USB / CD", L"Speicher", L"Tasten"};
    mv.page = 0; mv.section = L"Wiedergabe"; mv.keyHeader = L"Taste"; mv.buttonHeader = L"Controller"; mv.restartText = L"NEUSTART";
    MenuItem a; a.kind = MenuItemKind::Slider; a.label = L"Lautstärke"; a.frac = 0.65f; a.value = L"65 %"; mv.items.push_back(a);
    MenuItem b; b.kind = MenuItemKind::Toggle; b.label = L"Stumm"; b.on = false; mv.items.push_back(b);
    MenuItem c; c.kind = MenuItemKind::Choice; c.label = L"Radio an bei"; c.hint = L"Wann das Radio spielt"; c.value = L"Zündung"; mv.items.push_back(c);
    MenuItem d; d.kind = MenuItemKind::Toggle; d.label = L"Automatisch starten"; d.hint = L"Beim Spielstart sofort spielen"; d.on = true; mv.items.push_back(d);
    MenuItem f; f.kind = MenuItemKind::Choice; f.label = L"Modus"; f.value = L"Automatisch"; f.restart = true; mv.items.push_back(f);
    mv.selected = 2; mv.hover = 0; mv.unsaved = true; mv.status = L"Nicht gespeicherte Änderungen";
    mv.footer = L"Änderungen wirken sofort.  Esc / ✕ speichert und schließt.  Tab = nächste Seite";
    mv.btnSave = L"Speichern"; mv.btnReset = L"Zurücksetzen"; mv.btnDefault = L"Standard"; mv.hoverControl = MenuHit::Save;
    Save(RenderMenu(mv, 1080), "menu1.raw");
    MenuView kv = mv; kv.items.clear(); kv.page = 6; kv.section = L"Tastatur und Controller"; kv.hoverControl = 101;
    const wchar_t* rows[][3] = {{L"Nächster Sender / Titel", L"→", L"Knopf 6"}, {L"Vorheriger Sender / Titel", L"←", L"Knopf 5"},
        {L"Lauter", L"↑", L"–"}, {L"Leiser", L"↓", L"–"}, {L"Stumm", L"–", L"–"}, {L"Radio aus/an • USB Pause", L"Ende", L"–"},
        {L"Quelle: Radio / USB", L"Pos1", L"Knopf 8"}, {L"Nächster USB / CD", L"–", L"–"}, {L"Vorheriger USB / CD", L"–", L"–"},
        {L"Navi ↔ Radio-Seite", L"Einfg", L"–"}, {L"Als Favorit speichern", L"Pause", L"–"}, {L"Zum Favoriten", L"–", L"–"},
        {L"Overlay einblenden", L"–", L"–"}, {L"Dieses Menü", L"Strg + 0", L"–"}};
    for (auto& r : rows) { MenuItem it; it.kind = MenuItemKind::Key; it.label = r[0]; it.key = r[1]; it.button = r[2]; kv.items.push_back(it); }
    kv.selected = 6; kv.capture = 1; kv.captureText = L"Taste drücken …"; kv.status = L"Neue Taste oder Controller-Knopf drücken …  (Esc = abbrechen)";
    Save(RenderMenu(kv, 1080), "menu2.raw");

    MenuView uv = mv; uv.items.clear(); uv.page = 4; uv.section = L"Eigene Musik"; uv.hoverControl = MenuHit::ItemButton; uv.selected = 7; uv.hover = 5; uv.firstRow = 2;
    auto T = [](MenuItemKind k, const wchar_t* l, const wchar_t* h, const wchar_t* val, bool on = false) { MenuItem it; it.kind = k; it.label = l; it.hint = h; it.value = val; it.on = on; return it; };
    uv.items.push_back(T(MenuItemKind::Toggle, L"USB / CD-Modus", L"Musik aus dem Ordner usb (Taste Pos1)", L"", true));
    uv.items.push_back(T(MenuItemKind::Toggle, L"Jeder Ordner ein eigener USB", L"Aus = der ganze Ordner ist ein USB", L"", true));
    uv.items.push_back(T(MenuItemKind::Choice, L"Standard-Typ", L"Für Ordner ohne eigene Einstellung", L"USB"));
    uv.items.push_back(T(MenuItemKind::Toggle, L"Zufallswiedergabe", L"", L""));
    uv.items.push_back(T(MenuItemKind::Choice, L"Wiederholen", L"", L"Alle Titel"));
    { MenuItem b = T(MenuItemKind::Button, L"Musikordner", L"Hier Musik hinzufügen (öffnet den Explorer)", L""); b.button = L"Öffnen"; uv.items.push_back(b); }
    uv.items.push_back(T(MenuItemKind::Heading, L"Musikordner und ihr Typ", L"", L""));
    uv.items.push_back(T(MenuItemKind::Choice, L"Privat Music", L"42 Titel", L"CD"));
    uv.items.push_back(T(MenuItemKind::Choice, L"Rock Music", L"118 Titel", L"Standard"));
    uv.items.push_back(T(MenuItemKind::Choice, L"Rap Music", L"57 Titel", L"Kassette"));
    uv.items.push_back(T(MenuItemKind::Choice, L"YouTube Playlist", L"230 Titel", L"Bluetooth"));
    uv.unsaved = false; uv.status = L"Alles gespeichert";
    Save(RenderMenu(uv, 1080), "menu3.raw");
    MenuView sv = mv; sv.items.clear(); sv.page = 5; sv.section = L"Logos und Cover"; sv.selected = -1; sv.hover = 2; sv.hoverControl = MenuHit::ItemButton; sv.unsaved = false; sv.status = L"Alles gespeichert";
    { MenuItem it = T(MenuItemKind::Slider, L"Logo-Speicher höchstens", L"Älteste Logos werden automatisch gelöscht", L"30 MB"); it.frac = 0.05f; sv.items.push_back(it); }
    sv.items.push_back(T(MenuItemKind::Toggle, L"Senderlogos laden", L"Aus = keine Downloads, keine Logos", L"", true));
    { MenuItem b = T(MenuItemKind::Button, L"Belegt", L"Logos und USB-Cover im Ordner logos", L"12,4 MB  •  341 Dateien"); b.button = L"Leeren"; sv.items.push_back(b); }
    { MenuItem b = T(MenuItemKind::Button, L"Logo-Ordner", L"", L""); b.button = L"Öffnen"; sv.items.push_back(b); }
    Save(RenderMenu(sv, 1080), "menu4.raw");
    const char* types[] = {"usb", "sd", "cassette", "bluetooth", "aux", "cd"};
    for (int k = 0; k < 6; ++k) {
        OverlayInfo t; t.source = L"X"; t.media = true; t.mediaType = types[k]; t.headline = L"Titel 1 / 9"; t.title = L"Ohne Cover"; t.subtitle = L"Interpret"; t.posSec = 30; t.lenSec = 200;
        char n[32]; snprintf(n, 32, "t%d.raw", k); Save(RenderScreenCard(t, nullptr, 819, 410), n);
    }
    { OverlayInfo e; e.source = L"Medien"; e.media = true; e.mediaType = "none"; e.paused = true; e.title = L"Kein Medientyp erkannt";
      e.subtitle = L"Musik in realistic-radio\\usb ablegen"; e.detail = L"Ctrl+0 → USB / CD → Musikordner öffnen"; Save(RenderScreenCard(e, nullptr, 819, 410), "empty.raw"); }
    Save(RenderMenuCursor(1080), "cursor.raw");
}
