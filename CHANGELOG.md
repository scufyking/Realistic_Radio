# Changelog

**English** · [Deutsch](#changelog-deutsch)

## 1.2.0 – Several USB sticks / CDs, radio page and settings menu

### New
- **Settings menu in the game** (**Ctrl + 0**), with mouse and keyboard. Pages on the left, settings on the right:
  - **Radio:** volume, mute, radio on with ignition / engine / always, start automatically, menu language.
  - **Reception:** broadcast range, switch threshold, auto volume, static, tuning sound.
  - **Navigation screen:** on/off, display time, keep the radio page on, and the key that switches between navigation and radio page.
  - **Pop-up overlay:** mode, style, position, size, display time, logos.
  - **USB / CD:** on/off, every folder its own USB, default type, shuffle, repeat, a button that **opens the music folder** in Explorer, and a **type for every music folder** (see below).
  - **Storage:** maximum size of the logo storage, load station logos on/off, used space with a **Clear** button, open the logo folder.
  - **Keys:** click a key or controller field (or Enter) and press the new key, key combination or controller button. Right click or Delete removes a binding. If a key is already used by another function, it is removed there and the menu says so.
  - Changes take effect immediately. **Save** writes them to `realistic-radio.json`, **Revert** goes back to the saved state, **Defaults** restores the default values of the current page. **Esc** or **✕** saves and closes. Pop-up mode and switching the navigation display on/off take effect after a restart (marked in the menu).
  - With the mouse: click, drag sliders, mouse wheel over sliders and choices. With the keyboard: ↑ / ↓ select, ← / → change, Enter, Tab = next page.
  - While the menu is open, keyboard and mouse only control the menu, not the game (the mouse no longer moves the camera; the game's raw input and DirectInput mouse reads are filtered while the menu is open).
- **Media type per music folder.** Every folder in `usb\` can be a **USB stick, CD, SD card, cassette, Bluetooth or AUX** – set in the settings menu on the USB / CD page, saved as `"media": { "types": { "Rock Music": "cd" } }`. The type is shown on the source tag ("CD 2", "KASSETTE"), and tracks without a cover show a matching picture (CD, USB stick, SD card, cassette, Bluetooth symbol, jack plug). Folders without their own type use the default type (`media.label`).
- **Logo storage limit.** Downloaded station logos and USB covers in `realistic-radio\logos\` are kept below **30 MB** (setting `"overlay": { "logoCacheMB": 30 }`, 5–500 MB). When the limit is reached, the logos used longest ago are deleted; they are downloaded again if needed. Only a few dozen logos are kept in memory.
- **Auto volume (speed-dependent volume)**, like GALA in real car radios: the faster the truck, the louder the radio, to cover wind and engine noise. It starts at 30 km/h and reaches the full boost at 90 km/h; changes are smooth (about 3 seconds). Set in the settings menu on the **Reception** page with the slider **Auto volume** (off up to +6 dB), or with `"autoVolume"` (0 to 1) in `realistic-radio.json`. Off by default. The volume never goes above 100 %, so there is no distortion.
- **Radio page on the navigation screen.** The **Insert** key switches the in-cab screen between navigation and a permanent radio page. The page shows the current station or track (logo/cover, title, signal or playing time) and updates live; station changes and volume appear on it too, after a volume change it returns to the page. **Insert** again brings the navigation back. The choice is remembered (`"screenPage"` in `realistic-radio.json`); with the ignition off the navigation is shown.
- **Every folder is its own USB stick / CD.** Folders inside `realistic-radio\usb\` are now separate sources, for example:
  ```
  usb\
  ├── Privat Music\
  ├── Rock Music\
  ├── Rap Music\
  └── YouTube Playlist\
  ```
  Folders inside these folders (e.g. albums) still belong to the same USB / CD. Files placed directly in `usb\` form one more USB, as before.
- **The Home key steps through all sources** like on a car radio: Radio → USB 1 → USB 2 → … → Radio. With only one USB nothing changes: Radio ↔ USB.
- New keys `"nextDisc"` and `"previousDisc"` to change the USB / CD directly (unassigned by default, e.g. `"PageUp"` / `"Next"`). From the radio they jump to the first / last USB.
- **On the navigation screen:** the source tag shows the number (`USB 2`), the header line shows the folder name, e.g. "Rock Music • Titel 3 / 12".
- **Every USB / CD remembers its own last track.** Switching to it continues there (from the beginning of the track). The positions are saved in `"mediaResume"` in `realistic-radio.json`.
- New setting `"media": { "folderPerDisc": true }`. `false` treats the whole folder as one USB, exactly as in 1.1.0.

### Changed
- The read-me in a newly created `usb` folder explains the folder layout.
- The album line on the screen is left out when it only repeats the folder name of the USB / CD.

### Fixed
- With the pop-up overlay off (the default), a station logo that finished downloading while the card was visible only appeared at the next station change. It now appears right away.

### Compatibility
- Settings from 1.1.0 keep working; `"folderPerDisc"`, `"nextDisc"`, `"previousDisc"`, `"screenPage"` and `"menu"` are added automatically on the first start.
- Files directly in `usb\` play as before, and the last track saved by 1.1.0 is resumed.
- If you used sub folders in 1.1.0 (e.g. one per album), each of them is now its own USB. To keep everything on one USB, set `"folderPerDisc": false`.

### Upgrade notes
- Replace `realistic-radio.dll` only. Settings, favourites, own stations, logos and your music are kept.
- If **Insert** is already bound to something in ETS2, choose another key under `"keys": { "screenPage": "…" }`.

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

## 1.2.0 – Mehrere USB-Sticks / CDs, Radio-Seite und Einstellungsmenü

### Neu
- **Einstellungsmenü im Spiel** (**Strg + 0**), mit Maus und Tastatur. Links die Seiten, rechts die Einstellungen:
  - **Radio:** Lautstärke, Stumm, Radio an bei Zündung / Motor / immer, automatisch starten, Sprache des Menüs.
  - **Empfang:** Sendereichweite, Wechselschwelle, Auto-Lautstärke, Rauschen, Umschaltgeräusch.
  - **Navi-Bildschirm:** an/aus, Anzeigedauer, Radio-Seite dauerhaft zeigen und die Taste, die zwischen Navigation und Radio-Seite umschaltet.
  - **Popup-Overlay:** Modus, Stil, Position, Größe, Anzeigedauer, Logos.
  - **USB / CD:** an/aus, jeder Ordner ein eigener USB, Standard-Typ, Zufall, Wiederholen, ein Knopf, der den **Musikordner im Explorer öffnet**, und ein **Typ für jeden Musikordner** (siehe unten).
  - **Speicher:** Höchstgröße des Logo-Speichers, Senderlogos laden an/aus, belegter Platz mit Knopf **Leeren**, Logo-Ordner öffnen.
  - **Tasten:** Feld für Taste oder Controller anklicken (oder Enter) und die neue Taste, Tastenkombination oder den Controller-Knopf drücken. Rechtsklick oder Entf löscht eine Belegung. Ist eine Taste schon bei einer anderen Funktion, wird sie dort entfernt, und das Menü zeigt das an.
  - Änderungen wirken sofort. **Speichern** schreibt sie in die `realistic-radio.json`, **Zurücksetzen** holt den gespeicherten Stand zurück, **Standard** setzt die Werte der aktuellen Seite auf die Standardwerte. **Esc** oder **✕** speichert und schließt. Popup-Modus und das Ein-/Ausschalten der Navi-Anzeige wirken nach einem Neustart (im Menü markiert).
  - Mit der Maus: klicken, Schieberegler ziehen, Mausrad über Reglern und Auswahlfeldern. Mit der Tastatur: ↑ / ↓ auswählen, ← / → ändern, Enter, Tab = nächste Seite.
  - Solange das Menü offen ist, steuern Tastatur und Maus nur das Menü, nicht das Spiel (die Maus dreht nicht mehr die Kamera; die Mausabfragen des Spiels über Raw Input und DirectInput werden dafür gefiltert, solange das Menü offen ist).
- **Medientyp pro Musikordner.** Jeder Ordner in `usb\` kann ein **USB-Stick, eine CD, SD-Karte, Kassette, Bluetooth oder AUX** sein – einstellbar im Einstellungsmenü auf der Seite USB / CD, gespeichert als `"media": { "types": { "Rock Music": "cd" } }`. Der Typ steht auf dem Quellen-Schild („CD 2“, „KASSETTE“), und Titel ohne Cover zeigen ein passendes Bild (CD, USB-Stick, SD-Karte, Kassette, Bluetooth-Zeichen, Klinkenstecker). Ordner ohne eigenen Typ nutzen den Standard-Typ (`media.label`).
- **Begrenzter Logo-Speicher.** Heruntergeladene Senderlogos und USB-Cover in `realistic-radio\logos\` bleiben unter **30 MB** (Einstellung `"overlay": { "logoCacheMB": 30 }`, 5–500 MB). Ist die Grenze erreicht, werden die am längsten nicht benutzten Logos gelöscht; bei Bedarf werden sie neu geladen. Im Arbeitsspeicher bleiben nur wenige Dutzend Logos.
- **Auto-Lautstärke (geschwindigkeitsabhängige Lautstärke)**, wie GALA bei echten Autoradios: Je schneller der LKW fährt, desto lauter wird das Radio, damit es Wind- und Motorgeräusche übertönt. Sie setzt ab 30 km/h ein und erreicht bei 90 km/h die volle Anhebung; Änderungen erfolgen sanft (etwa 3 Sekunden). Einstellbar im Einstellungsmenü auf der Seite **Empfang** mit dem Regler **Auto-Lautstärke** (aus bis +6 dB) oder mit `"autoVolume"` (0 bis 1) in der `realistic-radio.json`. Standardmäßig aus. Die Lautstärke geht nie über 100 %, es übersteuert also nicht.
- **Radio-Seite auf dem Navi-Bildschirm.** Die Taste **Einfg** (Insert) schaltet den Cockpit-Bildschirm zwischen Navigation und einer dauerhaften Radio-Seite um. Die Seite zeigt den aktuellen Sender bzw. Titel (Logo/Cover, Titel, Empfang oder Spielzeit) und aktualisiert sich laufend; Senderwechsel und Lautstärke erscheinen ebenfalls darauf, nach einer Lautstärkeänderung geht es zurück zur Seite. Noch einmal **Einfg** holt die Navigation zurück. Die Wahl wird gespeichert (`"screenPage"` in `realistic-radio.json`); bei Zündung aus ist die Navigation zu sehen.
- **Jeder Ordner ist ein eigener USB-Stick / eine eigene CD.** Ordner in `realistic-radio\usb\` sind jetzt eigene Quellen, zum Beispiel:
  ```
  usb\
  ├── Privat Music\
  ├── Rock Music\
  ├── Rap Music\
  └── YouTube Playlist\
  ```
  Ordner innerhalb dieser Ordner (z. B. Alben) gehören weiter zum selben USB / zur selben CD. Dateien direkt in `usb\` bilden wie bisher einen weiteren USB.
- **Die Taste Pos1 schaltet durch alle Quellen** wie beim Autoradio: Radio → USB 1 → USB 2 → … → Radio. Mit nur einem USB bleibt alles wie vorher: Radio ↔ USB.
- Neue Tasten `"nextDisc"` und `"previousDisc"`, um direkt den USB / die CD zu wechseln (standardmäßig nicht belegt, z. B. `"PageUp"` / `"Next"`). Vom Radio aus springen sie zum ersten bzw. letzten USB.
- **Auf dem Navi:** Das Quellen-Schild zeigt die Nummer (`USB 2`), die Kopfzeile den Ordnernamen, z. B. „Rock Music • Titel 3 / 12“.
- **Jeder USB / jede CD merkt sich den eigenen letzten Titel.** Beim Umschalten geht es dort weiter (am Anfang des Titels). Gespeichert wird das unter `"mediaResume"` in `realistic-radio.json`.
- Neue Einstellung `"media": { "folderPerDisc": true }`. Mit `false` gilt der ganze Ordner wie in 1.1.0 als ein einziger USB.

### Geändert
- Die Anleitung in einem neu angelegten `usb`-Ordner erklärt den Aufbau mit Ordnern.
- Die Album-Zeile auf dem Navi entfällt, wenn sie nur den Ordnernamen des USB / der CD wiederholen würde.

### Behoben
- Bei ausgeschaltetem Popup-Overlay (Standard) erschien ein Senderlogo, das erst während der Anzeige fertig geladen war, erst beim nächsten Senderwechsel. Jetzt erscheint es sofort.

### Kompatibilität
- Einstellungen aus 1.1.0 funktionieren weiter; `"folderPerDisc"`, `"nextDisc"`, `"previousDisc"`, `"screenPage"` und `"menu"` werden beim ersten Start automatisch ergänzt.
- Dateien direkt in `usb\` spielen wie bisher, und der in 1.1.0 gespeicherte letzte Titel wird fortgesetzt.
- Wer in 1.1.0 Unterordner benutzt hat (z. B. einen pro Album), hat jetzt pro Ordner einen eigenen USB. Soll alles auf einem USB bleiben: `"folderPerDisc": false`.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Einstellungen, Favoriten, eigene Sender, Logos und deine Musik bleiben erhalten.
- Ist **Einfg** in ETS2 schon belegt, unter `"keys": { "screenPage": "…" }` eine andere Taste wählen.

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
