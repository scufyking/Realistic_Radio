# Realistic Radio – Add-ons erstellen / Creating add-ons

**Deutsch** · [English](#english)

Ab **Realistic Radio 1.7.0** kann jeder eigene **Apps für das Handy im LKW** bauen. Ein Add-on ist eine eigene DLL im `plugins`-Ordner des Spiels (ein normales SCS-Telemetrie-Plugin). Es braucht kein Zusatzprogramm und kein Fenster und kann selbst die Telemetrie des Spiels lesen. Mit Realistic Radio spricht es über die Funktionen in **`rr_addon.h`**.

Beispiele:
- `example/HelloApp.cpp` – ein kleines Beispiel, gut zum Abschauen.
- `addons/jobs/` – Aufträge+ (eigene Aufträge).
- `addons/webapp/` – Web-App (Spotify im Tablet).

## So funktioniert es

1. Dein Add-on startet in `scs_telemetry_init` einen **eigenen Thread** und ruft dort `rr_addon_load(&rr)` auf. Gibt es `0` zurück, ist Realistic Radio (noch) nicht da: einfach jede Sekunde nochmal versuchen.
2. **App anmelden**, und zwar etwa jede Sekunde wieder, sonst verschwindet sie nach 5 Sekunden:
   ```c
   rr.app_register("{\"id\":\"hello\",\"name\":\"Hallo\",\"name_en\":\"Hello\",\"mode\":\"list\",\"color\":\"#34C759\"}");
   ```
   Das Handy zeigt dann ein weiteres Symbol. Mehr als 5 Add-on-Apps passen nicht auf den Startbildschirm.
3. **Ereignisse abholen** (z. B. alle 50 ms): `rr.app_events("hello", buf, sizeof buf)` liefert ein JSON-Array, z. B. `[{"type":"open"},{"type":"click","id":"plus"}]`.
4. **Bildschirm schicken**, je nach Modus.

## Die drei Modi

| Modus | Wer zeichnet | Wofür |
|---|---|---|
| `list` | das Handy (im Look des Radios) | Menüs, Listen, Einstellungen, Formulare – **der einfache Weg** |
| `canvas` | dein Add-on (Pixel, Hochformat) | eigene Grafik, Spiele, Karten |
| `tablet` | dein Add-on (Pixel, großes Querformat-Tablet) | Webseiten, große Ansichten |

### `list`: Bildschirm als JSON

```json
{"screen": "start", "title": "Hallo", "back": false, "hint": "kleine Zeile unten",
 "items": [
   {"type": "section", "title": "ZÄHLER"},
   {"type": "row", "id": "plus", "title": "Plus eins", "value": "3", "text": "graue zweite Zeile", "arrow": true},
   {"type": "search", "id": "q", "placeholder": "Suchen …", "text": ""},
   {"type": "text", "title": "fette Zeile (optional)", "text": "ein Absatz, der umbricht"},
   {"type": "button", "id": "send", "title": "Senden", "enabled": true}
 ]}
```
- Schick den Bildschirm mit `rr.app_ui(id, json)` immer dann, wenn sich etwas ändert. Gleicher Text wird nicht neu gezeichnet.
- `screen`: Name deines aktuellen Bildschirms. Bei einem neuen Namen springt die Auswahl nach oben und das Suchfeld bekommt `text`. Beim gleichen Namen behält das Handy, was der Spieler gerade tippt.
- `back: true`: Zurück (Rechtsklick, Rücktaste, Pfeil oben links) schickt dir `{"type":"back"}`, für deine eigenen Unterseiten. Bei `false` geht Zurück zum Startbildschirm.
- `select`: Nummer der Zeile, die ausgewählt sein soll.
- Ereignisse: `{"type":"click","id":"..."}` (Klick oder Enter), `{"type":"text","id":"q","text":"..."}` (Tippen ins Suchfeld), `{"type":"back"}`, `{"type":"open"}`, `{"type":"close"}`.

### `canvas` / `tablet`: eigene Pixel

- `rr.app_area(id, &w, &h)` sagt, wie groß dein Bild sein soll, und gibt `1` zurück, solange deine App offen ist.
- `rr.app_frame(id, pixels, w, h)` schickt das Bild: 32 Bit BGRA, premultiplied, von oben nach unten.
- Ereignisse:
  - Maustasten: `{"type":"down"|"up","button":0,"x":..,"y":..}`
  - Mausbewegung: `{"type":"move",...}`
  - Mausrad: `{"type":"wheel","delta":120,...}`
  - Tasten: `{"type":"key","vk":13,"ctrl":false,"shift":false,"alt":false}`
  - Zeichen: `{"type":"char","text":"a"}`

  `x`/`y` sind Pixel in deinem Bild. Esc schließt immer das Handy.

## Weitere Funktionen

| Funktion | Zweck |
|---|---|
| `rr.state(buf, size)` | Spielzustand als JSON: Sprache des Radio-Menüs (`language`), Spielzeit, Position, `nearestCity`, aktueller Auftrag … |
| `rr.city("berlin", buf, size)` | Name und Kartenposition einer Stadt |
| `rr.notify(json)` | Hinweis auf dem Navi, z. B. `{"app":"Hallo","title":"Fertig","text":"…","message":true}`. `message: true` legt ihn zusätzlich in den Nachrichten des Handys ab. |
| `rr.log("Mein Add-on: …")` | Zeile in `realistic-radio.log` |
| `rr.media_*` | Für Musik-Add-ons: Titel und Cover aufs Navi, das Radio blendet aus |

Rückgabe der Puffer-Funktionen: Anzahl Bytes, `0` = nichts, negative Zahl = Puffer zu klein (`-Zahl` Bytes nötig).

## Bauen

Als 64-Bit-DLL. Benötigt werden die Header des SCS SDK (`scssdk_telemetry.h`, liegen im Quellcode unter `sdk/include`) und `rr_addon.h`:
```
x86_64-w64-mingw32-g++ -shared -O2 -std=c++17 HelloApp.cpp -I<sdk>/include -I.. -o realistic-radio-hello.dll -static
cl /LD /O2 /std:c++17 HelloApp.cpp /I<sdk>\include /I.. /Fe:realistic-radio-hello.dll
```
Die DLL kommt neben `realistic-radio.dll` in den `plugins`-Ordner. Einen Namen mit `realistic-radio-…` zu wählen ist nicht nötig, macht es aber übersichtlich.

## Regeln

- Nie in `DllMain` oder direkt in `scs_telemetry_init` warten oder `rr_addon_load` aufrufen. Immer im eigenen Thread.
- In `scs_telemetry_shutdown` den Thread beenden.
- Alle Texte sind UTF-8. Nutz `language` aus `rr.state` für deutsche, englische oder andere Texte.
- `rr_addon.h` darf in Add-ons mit jeder Lizenz verwendet werden.

---

## English

From **Realistic Radio 1.7.0** anyone can build their own **apps for the phone in the truck**. An add-on is its own DLL in the game's `plugins` folder (a normal SCS telemetry plugin). It needs no extra program and no window, and it can read the game's telemetry itself. It talks to Realistic Radio through the functions in **`rr_addon.h`**.

Examples:
- `example/HelloApp.cpp` – a small sample to start from.
- `addons/jobs/` – Jobs+ (own jobs).
- `addons/webapp/` – Web-App (Spotify on the tablet).

### How it works

1. In `scs_telemetry_init` your add-on starts **its own thread** and calls `rr_addon_load(&rr)` there. If it returns `0`, Realistic Radio is not there (yet): just try again every second.
2. **Register the app**, again about every second, otherwise it disappears after 5 seconds: `rr.app_register("{\"id\":\"hello\",\"name\":\"Hallo\",\"name_en\":\"Hello\",\"mode\":\"list\",\"color\":\"#34C759\"}")`. The phone then shows one more icon. No more than 5 add-on apps fit on the home screen.
3. **Fetch events** (e.g. every 50 ms): `rr.app_events("hello", buf, sizeof buf)` returns a JSON array such as `[{"type":"open"},{"type":"click","id":"plus"}]`.
4. **Send the screen**, depending on the mode.

### The three modes

| Mode | Who draws | For |
|---|---|---|
| `list` | the phone (in the radio's look) | menus, lists, settings, forms – **the easy way** |
| `canvas` | your add-on (pixels, portrait) | own graphics, games, maps |
| `tablet` | your add-on (pixels, large landscape tablet) | web pages, large views |

**`list`:** send the screen as JSON with `rr.app_ui(id, json)` whenever something changes (the format is shown above in the German part).

- Item types: `section`, `row` (`title`, `value`, `text`, `arrow`), `search` (`placeholder`, `text`), `text` (`title`, `text`), `button` (`title`, `enabled`).
- `screen`: the name of your current screen. A new name puts the selection to the top.
- `back: true` sends you `{"type":"back"}` for your own sub screens.
- Events: `click`, `text`, `back`, `open`, `close`.

**`canvas` / `tablet`:**

- `rr.app_area(id, &w, &h)` gives the picture size and returns `1` while your app is open.
- `rr.app_frame(id, pixels, w, h)` sends it: 32-bit BGRA, premultiplied, top-down.
- Events: `down`, `up`, `move`, `wheel`, `key`, `char` (x/y in pixels of your picture). Esc always closes the phone.

### More functions

- `rr.state` – game state JSON: the radio menu's `language`, game time, position, `nearestCity`, the current job.
- `rr.city` – name and map position of a city.
- `rr.notify` – a notification on the navigation screen, optionally also in the phone's messages.
- `rr.log` – a line in `realistic-radio.log`.
- `rr.media_*` – for music add-ons: title and cover on the navigation screen, the radio fades out.

Functions that fill a buffer return the number of bytes, `0` = nothing, a negative number = buffer too small (`-number` bytes needed).

### Building and rules

- Build it as a 64-bit DLL with the SCS SDK headers and `rr_addon.h` (see the command lines above), and put it next to `realistic-radio.dll`.
- Never wait in `DllMain` or call `rr_addon_load` directly in `scs_telemetry_init`: always use your own thread. End that thread in `scs_telemetry_shutdown`.
- All texts are UTF-8.
- `rr_addon.h` may be used in add-ons of any licence.
