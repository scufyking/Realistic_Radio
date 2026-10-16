# Changelog

**English** · [Deutsch](#changelog-deutsch)

## 1.1.0 – USB / CD mode and new screen design

### New
- **USB / CD mode for your own music.** On the first start the plugin creates the folder `realistic-radio\usb\` (with a short read-me). Copy MP3, WAV, FLAC, M4A, AAC or WMA files into it, sub folders included, and press **Home** to switch between radio and USB. Press **Home** again to go back to the station you last listened to.
- In USB mode: **Right** = next track, **Left** = start of the track or previous track, **End** = pause / resume, **Up / Down** = volume. After the last track it starts again from the first (settable).
- **Your music on the navigation screen:** source tag (USB / CD), "Titel 3 / 12", title, artist, album, cover and a progress bar with elapsed time and length. The time runs while the card is visible.
- Title, artist and album are read from the MP3's ID3 tags; otherwise from the file name (`Artist - Title.mp3`). Covers come from the MP3 or from `cover.jpg` / `folder.jpg` / `front.jpg` in the folder; without a cover a stylised CD is shown.
- New settings block `"media"`: `enabled`, `folder` (also a full path, e.g. another drive), `label` (e.g. `"CD"`), `shuffle`, `repeat` (`all` / `one` / `off`), `textTrack`, `textPaused`, `textEmpty`.
- New key `"source"` (default **Home**).
- Source and last track are remembered and resumed on the next start. With the ignition off the track pauses and continues at the same position.
- The version number is written to the log (`Realistic Radio 1.1.0 loaded …`) and shown in the DLL's file properties.

### Changed
- **New design of the navigation screen cards.** Source tag and status in a header line, logo or cover on the left, larger title with up to two lines, country and "Sender 3 / 12" below it. Signal bars and the favourite heart move to the top right.
- **New volume card:** speaker symbol with sound waves, large percentage and a bar of 20 segments (one per 5 % step). When muted the speaker is crossed out and the volume that comes back is shown in grey.
- Stations without a logo show their initials instead of an empty space.
- The card on the navigation screen now also updates while it is visible (e.g. from "verbinde…" to playing).
- Existing `realistic-radio.json` files get the new key `"source"` and the `"media"` block added automatically on the first start; all other values are kept.

### Fixed
- After a message without a station (e.g. "Zündung aus") the logo of the previous station could remain on the card.

### Upgrade notes
- Replace `realistic-radio.dll` only. Settings, favourites, own stations and logos are kept.
- The USB/CD mode and the new design compile, but have **not been tested in the game yet**. Please report problems with the `realistic-radio.log`.
- If **Home** is already bound to something in ETS2, choose another key under `"keys": { "source": "…" }`.

## 1.0.3 – Navigation screen display

### New
- **Station and volume on the truck's navigation screen.** On every station change the station logo, name, country and signal strength appear on the in-cab navigation screen for 4 seconds; on volume changes a volume bar with percentage. Afterwards the navigation is shown again. The game's own status bar (clock) stays visible.
- Works without any additional plugin: Realistic Radio draws directly into the render target the game uses for the cab screen.
- **Set up for the MAN TGX by default** (ETS2 1.61, navigation screen). New settings block `"screen"` in `realistic-radio.json`:
  `enabled`, `width`, `height`, `format`, `areaX`, `areaY`, `areaW`, `areaH`, `flipX`, `flipY`, `durationSeconds`.
- **Probe mode** (`"probe": true`) for other trucks: marks every candidate render target in turn with a test pattern and its size, so you can see which one is the navigation screen.
- **Grid mode** (`"grid": true`): shows a 10×10 coordinate grid on the chosen render target to find the visible area.

### Changed
- **The pop-up overlay at the top of the screen is now off by default** (`"overlay": { "mode": "off" }`). Turn it on again with `"mode": "auto"`; the car-radio style (`"style": "radio"`) still works as before.
- A newly created `realistic-radio.json` contains the navigation screen settings above.

### Fixed
- The overlay no longer uses `SwapDeviceContextState` to save the game's graphics state. It now saves and restores the state manually; the old method could silently disable the plugin's rendering hooks.
- Rendering hooks are attached to the game's own Direct3D context (taken from the first frame) instead of a self-created test device, which uses a different internal function table.
- The game image and full-scene buffers are excluded from the screen search, so the test pattern no longer covers the whole picture.
- The screen card is rendered at the size of the visible area, so it is no longer stretched; signal bars are drawn as graphics instead of font characters.

### Upgrade notes
- Replace `realistic-radio.dll` only. Existing settings are kept.
- If your `realistic-radio.json` has no `"screen"` block, the MAN TGX defaults are used automatically.
- Your existing file still contains `"overlay": { "mode": "auto" }`, so the pop-up stays on for you. Set `"off"`, or delete the file to recreate it with the new defaults (this also resets volume and keys; favourites and own stations are kept).
- In other trucks the card may not appear, or appear on a different surface. Use probe and grid mode to set it up, or set `"screen": { "enabled": false }`.

---

<a id="changelog-deutsch"></a>
# Changelog (Deutsch)

## 1.1.0 – USB / CD-Modus und neues Design

### Neu
- **USB / CD-Modus für eigene Musik.** Beim ersten Start legt das Plugin den Ordner `realistic-radio\usb\` an (mit einer kurzen Anleitung). MP3-, WAV-, FLAC-, M4A-, AAC- oder WMA-Dateien hineinkopieren, auch in Unterordnern, und mit **Pos1** zwischen Radio und USB umschalten. Noch einmal **Pos1** führt zurück zum zuletzt gehörten Sender.
- Im USB-Modus: **Pfeil rechts** = nächster Titel, **Pfeil links** = Anfang des Titels bzw. vorheriger Titel, **Ende** = Pause / weiter, **Pfeil hoch / runter** = Lautstärke. Nach dem letzten Titel geht es wieder beim ersten los (einstellbar).
- **Deine Musik auf dem Navi-Bildschirm:** Quellen-Schild (USB / CD), „Titel 3 / 12“, Titel, Interpret, Album, Cover und ein Fortschrittsbalken mit Spielzeit und Länge. Die Zeit läuft mit, solange die Anzeige sichtbar ist.
- Titel, Interpret und Album kommen aus den ID3-Tags der MP3, sonst aus dem Dateinamen (`Interpret - Titel.mp3`). Cover kommen aus der MP3 oder aus `cover.jpg` / `folder.jpg` / `front.jpg` im Ordner; ohne Cover erscheint eine stilisierte CD.
- Neuer Einstellungsblock `"media"`: `enabled`, `folder` (auch ein vollständiger Pfad, z. B. auf einer anderen Festplatte), `label` (z. B. `"CD"`), `shuffle`, `repeat` (`all` / `one` / `off`), `textTrack`, `textPaused`, `textEmpty`.
- Neue Taste `"source"` (Standard **Pos1** / `Home`).
- Quelle und letzter Titel werden gespeichert und beim nächsten Start fortgesetzt. Bei Zündung aus wird der Titel angehalten und läuft danach an derselben Stelle weiter.
- Die Versionsnummer steht im Log (`Realistic Radio 1.1.0 loaded …`) und in den Dateieigenschaften der DLL.

### Geändert
- **Neues Design der Navi-Anzeigen.** Quellen-Schild und Status in einer Kopfzeile, Logo bzw. Cover links, größerer Titel mit bis zu zwei Zeilen, darunter Land und „Sender 3 / 12“. Empfangsbalken und Favoriten-Herz stehen jetzt oben rechts.
- **Neue Lautstärke-Anzeige:** Lautsprechersymbol mit Schallwellen, große Prozentzahl und ein Balken aus 20 Segmenten (eins pro 5-%-Schritt). Bei Stumm ist der Lautsprecher durchgestrichen und die Lautstärke, die danach zurückkommt, grau zu sehen.
- Sender ohne Logo zeigen ihre Anfangsbuchstaben statt einer leeren Fläche.
- Die Anzeige auf dem Navi aktualisiert sich jetzt auch, während sie sichtbar ist (z. B. von „verbinde…“ zu läuft).
- In vorhandene `realistic-radio.json`-Dateien werden beim ersten Start die neue Taste `"source"` und der Block `"media"` automatisch eingetragen; alle anderen Werte bleiben.

### Behoben
- Nach einer Meldung ohne Sender (z. B. „Zündung aus“) konnte das Logo des vorherigen Senders auf der Anzeige stehen bleiben.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Einstellungen, Favoriten, eigene Sender und Logos bleiben erhalten.
- USB/CD-Modus und neues Design sind kompiliert, aber **noch nicht im Spiel getestet**. Probleme bitte mit der `realistic-radio.log` melden.
- Ist **Pos1** in ETS2 schon belegt, unter `"keys": { "source": "…" }` eine andere Taste wählen.

## 1.0.3 – Anzeige auf dem Navi-Bildschirm

### Neu
- **Sender und Lautstärke auf dem Navi-Bildschirm im LKW.** Bei jedem Senderwechsel erscheinen Senderlogo, Sendername, Land und Empfangsstärke für 4 Sekunden auf dem Navi im Cockpit, bei Lautstärkeänderungen ein Balken mit Prozentanzeige. Danach ist die Navigation wieder zu sehen. Die Statusleiste des Spiels (Uhrzeit) bleibt sichtbar.
- Kein zusätzliches Plugin nötig: Realistic Radio zeichnet direkt in die Zeichenfläche, die das Spiel für den Cockpit-Bildschirm nutzt.
- **Standardmäßig für den MAN TGX eingerichtet** (ETS2 1.61, Navi-Bildschirm). Neuer Einstellungsblock `"screen"` in `realistic-radio.json`:
  `enabled`, `width`, `height`, `format`, `areaX`, `areaY`, `areaW`, `areaH`, `flipX`, `flipY`, `durationSeconds`.
- **Suchmodus** (`"probe": true`) für andere LKW: markiert alle möglichen Zeichenflächen nacheinander mit einem Testbild und ihrer Größe, so sieht man, welche die Navi ist.
- **Gittermodus** (`"grid": true`): zeigt ein 10×10-Koordinatengitter auf der gewählten Zeichenfläche, um den sichtbaren Ausschnitt zu bestimmen.

### Geändert
- **Das Popup-Overlay oben im Bild ist jetzt standardmäßig aus** (`"overlay": { "mode": "off" }`). Mit `"mode": "auto"` wieder einschaltbar; der Autoradio-Stil (`"style": "radio"`) funktioniert weiterhin.
- Eine neu angelegte `realistic-radio.json` enthält die Navi-Einstellungen von oben.

### Behoben
- Das Overlay sichert den Grafikzustand des Spiels nicht mehr mit `SwapDeviceContextState`, sondern von Hand. Die alte Methode konnte die Zeichen-Hooks des Plugins unbemerkt abschalten.
- Die Hooks hängen jetzt am Direct3D-Kontext des Spiels selbst (aus dem ersten Bild) statt an einem eigenen Testgerät, das intern eine andere Funktionstabelle benutzt.
- Das Spielbild und Flächen in Szenengröße werden bei der Suche ausgeschlossen, das Testbild überdeckt nicht mehr das ganze Bild.
- Die Anzeige wird in der Größe des sichtbaren Ausschnitts gezeichnet und dadurch nicht mehr verzerrt; die Empfangsbalken sind Grafik statt Schriftzeichen.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Bestehende Einstellungen bleiben erhalten.
- Fehlt in deiner `realistic-radio.json` der Block `"screen"`, gelten automatisch die Werte für den MAN TGX.
- In einer vorhandenen Datei steht noch `"overlay": { "mode": "auto" }`, das Popup bleibt also an. Auf `"off"` stellen oder die Datei löschen, damit sie mit den neuen Standards neu angelegt wird (setzt auch Lautstärke und Tasten zurück; Favoriten und eigene Sender bleiben).
- In anderen LKW erscheint die Anzeige eventuell nicht oder auf einer anderen Fläche. Dann mit Such- und Gittermodus einrichten oder `"screen": { "enabled": false }` setzen.
