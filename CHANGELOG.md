# Changelog

**English** · [Deutsch](#changelog-deutsch)

## Navigation screen display

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

# Changelog (Deutsch)

## Anzeige auf dem Navi-Bildschirm

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
