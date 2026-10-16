# Realistic Radio

[English](README.md) · **Deutsch**

Echtes, regionales Radio für **Euro Truck Simulator 2**, als einzelne native Plugin-DLL.
Während du fährst, erkennt das Plugin, in welcher Region du bist, und spielt die echten Internet-Radiosender dieser Region. Bei schwachem Empfang rauscht es, an der Grenze wechselt der Sender mit sanftem Übergang.

Von **ScufyKing**. Das ganze Radio läuft im Spiel:

- keine Server-EXE, kein Hintergrundprogramm
- kein Browser, keine Webseite, kein lokaler HTTP-Server
- kein Python oder Node.js für Spieler
- keine Änderung an Spieldateien

Eine einzige Datei, `realistic-radio.dll`, in den `plugins`-Ordner des Spiels kopieren, ETS2 starten, das Radio läuft. Senderdaten und Sounds stecken in der DLL.

> **Stand:** Getestet in ETS2 1.61 (Windows x64): Das Plugin wird geladen, liest die LKW-Position, wechselt Sender nach Region, spielt Ton ab und zeichnet das Ingame-Overlay. Daten für American Truck Simulator sind enthalten, ATS ist aber ungetestet.

---

## Inhalt

- [Funktionen](#funktionen)
- [Installation](#installation)
- [Bedienung](#bedienung)
- [Einstellungen](#einstellungen)
- [Eigene Sender hinzufügen](#eigene-sender-hinzufügen)
- [Fehlersuche](#fehlersuche)
- [Bekannte Einschränkungen](#bekannte-einschränkungen)
- [Wie es funktioniert](#wie-es-funktioniert)
- [Selbst kompilieren](#selbst-kompilieren)
- [Projektstruktur](#projektstruktur)
- [Lizenz und Danksagung](#lizenz-und-danksagung)

---

## Funktionen

- **Sender nach Standort.** Rund 2.100 Internet-Radiosender, zugeordnet zu 2.064 Städten auf der Grundkarte, den DLC-Karten und vielen Karten-Mods (ProMods, RusMap und weitere).
- **Realistische Reichweite und Umschaltung.** Jede Stadt hat eine Sendereichweite; an Grenzen überlappen Regionen, und das Radio wechselt nur automatisch, wenn eine andere Region deutlich besser empfangen wird.
- **Empfang und Rauschen.** Je weiter du von einer Region entfernt bist, desto mehr rauscht es, und bei schwachem Signal setzt der Ton ab und zu aus.
- **Sanfte Übergänge** mit Umschaltgeräusch.
- **Ingame-Overlay.** Bei jedem Senderwechsel, bei Lautstärke usw. erscheint für einige Sekunden ein Panel mit Sendername, Land, Empfangsbalken, Favoriten-Herz und Senderlogo. Funktioniert auch im Vollbild. Die Logos werden einmal von `radio.scufystudio.de` geladen und danach lokal gespeichert.
- **Radio nur bei eingeschalteter Zündung** (oder nur bei laufendem Motor, oder immer; einstellbar).
- **Steuerung per Tastatur und Controller**, frei belegbar.
- **Favoriten pro Land.**
- **Eigene Sender** in einer separaten Datei, die bei Updates erhalten bleibt.
- **Automatische Wiederverbindung**, wenn ein Stream abbricht.
- **Spielt MP3-, AAC- und HLS-Streams** (MP3 mit eingebautem Decoder, AAC/HLS über Windows Media Foundation) sowie lokale Audiodateien.

## Installation

**Voraussetzungen:** Windows 10 oder 11 (64 Bit), Euro Truck Simulator 2 (64 Bit), Internetverbindung für die Streams.

1. `realistic-radio.dll` herunterladen.
2. Den Plugin-Ordner des Spiels öffnen:
   `…\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   Gibt es den Ordner `plugins` noch nicht, lege ihn an.
3. `realistic-radio.dll` hineinkopieren. Das ist alles – weitere Dateien braucht es nicht.
4. Andere Radio-Plugin-DLLs aus diesem Ordner entfernen (z. B. eine ältere `local-radio-native.dll` oder `local-radio.dll`), damit nicht zwei Radios gleichzeitig spielen.
5. ETS2 starten. Das Spiel fragt, ob „erweiterte SDK-Funktionen“ erlaubt werden sollen; mit **OK** bestätigen.

Beim ersten Start legt das Plugin neben der DLL den Ordner `realistic-radio\` an und entpackt Senderdaten und Sounds dorthin. Das Radio startet automatisch, sobald der LKW eine Position hat und die Zündung an ist.

Danach sieht der Ordner so aus (alles außer der DLL wird automatisch angelegt):

```
plugins\
├── realistic-radio.dll
└── realistic-radio\
    ├── ets2.json                    Sender- und Städtedaten (ETS2, aus der DLL)
    ├── ats.json                     Sender- und Städtedaten (ATS, aus der DLL)
    ├── custom-stations-ets2.example.json   Beispiel für eigene Sender
    ├── custom-stations-ets2.json    deine eigenen Sender (optional, legst du selbst an)
    ├── audio\                       Rauschen und Umschaltgeräusch (aus der DLL)
    ├── realistic-radio.json         Einstellungen (wird beim ersten Start angelegt)
    ├── favourites-ets2.json         deine Favoriten (wird automatisch angelegt)
    ├── logos\                       heruntergeladene Senderlogos (Zwischenspeicher)
    └── realistic-radio.log          Logdatei
```

**Aktualisieren:** ETS2 beenden und `realistic-radio.dll` ersetzen. Enthält die neue DLL neuere Senderdaten, ersetzt sie beim nächsten Start `ets2.json`, `ats.json` und die Sounds. Einstellungen, Favoriten, eigene Sender und Logos bleiben unangetastet.

**Umstieg von „ETS2 Local Radio Native“** (der frühere Name): `realistic-radio.dll` nach `plugins` kopieren und `local-radio-native.dll` löschen. Beim ersten Start werden Einstellungen, Favoriten, eigene Sender und gespeicherte Logos automatisch aus dem alten Ordner `local-radio\` übernommen; danach kannst du auch diesen Ordner löschen.

**Deinstallieren:** `realistic-radio.dll` und den Ordner `realistic-radio` löschen.

## Bedienung

| Aktion | Standardtaste | Einstellung in `realistic-radio.json` |
|---|---|---|
| Nächster Sender | → (Pfeil rechts) | `keys.next` |
| Vorheriger Sender | ← (Pfeil links) | `keys.previous` |
| Stopp / Start | Ende | `keys.stop` |
| Lauter (5-%-Schritte) | ↑ (Pfeil hoch) | `keys.volumeUp` |
| Leiser (5-%-Schritte) | ↓ (Pfeil runter) | `keys.volumeDown` |
| Aktuellen Sender als Favorit für dieses Land speichern | Pause | `keys.makeFavourite` |
| Zum Favoriten | – | `keys.goToFavourite` |
| Stumm | – | `keys.mute` |
| Overlay einblenden | – | `keys.showOverlay` |

„Nächster“ und „Vorheriger“ schalten durch alle Sender aller Regionen, die du gerade empfängst.

Tasten reagieren nur, wenn ETS2 das aktive Fenster ist. So stören sie keine anderen Programme.

Sind die Pfeiltasten in ETS2 auch zum Fahren oder für Menüs belegt, passiert beides gleichzeitig. Dann andere Tasten oder eine Kombination wie `"Ctrl+Right"` verwenden.

**Tastennamen** sind die .NET-Tastennamen: Buchstaben `A`–`Z`, Ziffern `0`–`9`, `F1`–`F24`, `NumPad0`–`NumPad9`, `PageUp`, `Next` (= Bild ↓), `Home`, `End`, `Insert`, `Delete`, `Up`, `Down`, `Left`, `Right`, `Pause`, `Space`, `Oemplus`, `OemMinus`, `Add`, `Subtract`, `MediaNextTrack`, `MediaPlayPause` und weitere. Kombinationen gehen auch, z. B. `"Ctrl+Shift+R"`. Ein leerer Wert `""` schaltet eine Taste ab.

**Controller:** `controllerIndex` setzen (0 = erster Controller) und unter `buttons` jeder Aktion eine Tastennummer geben, gezählt ab 0. `-1` schaltet sie ab. Die Nummern entsprechen denen in den Windows-Controller-Einstellungen minus 1.

## Einstellungen

Alle Einstellungen stehen in `realistic-radio\realistic-radio.json`. Die Datei wird beim ersten Start mit Standardwerten angelegt; ändere sie bei geschlossenem Spiel. Kommentare (`// …`) sind erlaubt.

### Radio

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `radius` | `7500` | Grund-Sendereichweite einer Stadt in Spielmetern |
| `threshold` | `0.15` | Wie viel besser eine andere Region empfangen werden muss, bevor automatisch gewechselt wird. `0` = sofort wechseln, `1` = nie |
| `whitenoise` | `true` | Rauschen bei schwachem Empfang |
| `transitionWhitenoise` | `true` | Umschaltgeräusch beim Senderwechsel |
| `volume` | `0.6` | Lautstärke von 0.0 bis 1.0 (wird automatisch gespeichert). Die Skala ist gehörgerecht: 50 % ist deutlich leiser als voll, 10 % sehr leise, 0 ist immer stumm |
| `muted` | `false` | Stumm (wird automatisch gespeichert) |
| `autoPlay` | `true` | Automatisch losspielen |
| `powerSource` | `"ignition"` | `"ignition"` = nur bei Zündung an, `"engine"` = nur bei laufendem Motor, `"always"` = immer |
| `dataFile` | `""` | Andere Sender-/Städtedatei im Ordner `realistic-radio` (leer = `ets2.json` bzw. `ats.json`) |
| `onlyWhenGameFocused` | `true` | Tasten wirken nur, wenn das Spiel im Vordergrund ist |
| `reconnectMaxSeconds` | `30` | Maximale Wartezeit zwischen Verbindungsversuchen nach einem Stream-Abbruch |

### Overlay (`"overlay": { … }`)

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `style` | `"panel"` | `"panel"` = Info-Panel mit Logo, `"radio"` = Autoradio-Display mit Punktmatrix (siehe unten) |
| `mode` | `"auto"` | `"auto"` (empfohlen), `"d3d11"` (ins Spielbild zeichnen), `"window"` (transparentes Fenster über dem Spiel), `"off"` |
| `position` | `"top-center"` | `top-left`, `top-center`, `top-right`, `bottom-left`, `bottom-center`, `bottom-right` |
| `offsetX`, `offsetY` | `0`, `0.07` | Abstand vom Bildschirmrand als Anteil der Bildschirmhöhe (0.07 = 7 %) |
| `scale` | `1.0` | Größe; passt sich zusätzlich automatisch an die Auflösung an |
| `durationSeconds` | `6` | Wie lange das Panel sichtbar bleibt |
| `alwaysVisible` | `false` | Panel dauerhaft anzeigen |
| `showLogos` | `true` | Senderlogos anzeigen |
| `urlPrefix` | `"https://radio.scufystudio.de/logo/"` | Woher fehlende Logos geladen werden. Geladene Logos bleiben in `realistic-radio\logos\` und werden nie erneut geladen. Leer = nur Logos, die schon in diesem Ordner liegen, kein Netzzugriff |
| `textNowPlaying`, `textVolume`, `textStopped`, `textMuted`, `textFavourite`, `textConnecting`, `textUnavailable`, `textPowerOff` | deutsche Texte | Alle Texte im Overlay, frei änderbar |

#### Autoradio-Display (`"style": "radio"`)

Statt des Panels erscheint über dem Armaturenbrett ein schmales Punktmatrix-Display wie bei einem echten Autoradio: Sendername in Großbuchstaben, Laufschrift bei langen Namen und eine Empfangsanzeige. Lautstärke, Stumm und andere Meldungen erscheinen auf demselben Display. Einstellungen unter `"overlay": { "radio": { … } }`:

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `position` | `"bottom-center"` | Gleiche Werte wie bei der Panel-Position |
| `offsetX`, `offsetY` | `0`, `0.20` | Abstand vom Bildschirmrand als Anteil der Bildschirmhöhe |
| `scale` | `1.0` | Größe |
| `columns` | `16` | Zeichen auf dem Display (8–40) |
| `color` | `"amber"` | `amber`, `green`, `blue`, `white`, `red`, `orange` oder `#RRGGBB` |
| `scrollSeconds` | `0.3` | Tempo der Laufschrift: Sekunden pro Zeichen |

Die Punktmatrix-Zeichen nutzen die klassische 5×7-Schrift der Adafruit GFX Library (BSD-Lizenz, siehe `NOTICE.md`); fehlende Buchstaben werden als Grundbuchstabe gezeigt (À → A), andere Schriften wie Kyrillisch zeichnet eine Windows-Schrift.

### Anzeige auf dem LKW-Bildschirm (`"display": { … }`, optional)

Das Plugin kann ein natives Fenster „Realistic Radio Display“ öffnen, das das Radio als Infotainment-Bildschirm zeigt. Das separate Plugin [PrismTextureStreamer](https://github.com/Baldywaldy09/PrismTextureStreamer) (nicht Teil dieses Projekts) kann dieses Fenster auf den GPS- oder Dashboard-Bildschirm im LKW spiegeln. Standardmäßig ist es aus.

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `enabled` | `false` | Display-Fenster öffnen |
| `width`, `height` | `1280`, `720` | Größe in Pixeln |
| `offscreen` | `true` | Fenster außerhalb des sichtbaren Desktops platzieren. Bleibt es in PrismTextureStreamer schwarz, auf `false` setzen |
| `title` | `"Realistic Radio Display"` | Fenstertitel |

## Eigene Sender hinzufügen

Kopiere `realistic-radio\custom-stations-ets2.example.json` im selben Ordner nach `custom-stations-ets2.json` und trage dort deine Sender ein. Updates enthalten nie eine `custom-stations-ets2.json`, deine Datei wird also nie überschrieben.

```jsonc
{
  // Schlüssel = Region aus ets2.json, z. B. "germany" (bundesweit), "germany-bw", "germany-br",
  // "austria", "netherlands", "uk", "france", ...
  "germany": [
    {
      "name": "Radio Bollerwagen",
      "url": "https://stream.ffn.de/radiobollerwagen/mp3-192",
      "logo": "stations/images-europe/germany-nds/Radio Bollerwagen.png"
    }
  ]
}
```

- Ein Sender mit **gleichem Namen** wie ein vorhandener in dieser Region ersetzt ihn (z. B. um eine kaputte Stream-URL zu korrigieren).
- Ein Sender mit **neuem Namen** wird ans Ende der Liste dieser Region angehängt.
- `logo` ist optional: eine vollständige `https://`-Adresse eines Bildes (PNG oder JPG) oder ein Dateiname. Ein Dateiname wird zuerst in `realistic-radio\logos\` gesucht und sonst von `urlPrefix` geladen.
- `url` kann auch eine lokale Audiodatei sein. Ein relativer Pfad wird im Ordner `realistic-radio` gesucht.

Die Regionsnamen findest du in `ets2.json` unter `"stations"`. Regionen mit Bindestrich sind Teilgebiete, z. B. `germany-swr` oder `germany-muenchen`. In Grenznähe empfängst du oft mehrere Regionen gleichzeitig.

## Fehlersuche

Fast jedes Problem lässt sich an `realistic-radio\realistic-radio.log` erkennen. Wenn du um Hilfe bittest, häng diese Datei immer an.

| Problem | Ursache und Lösung |
|---|---|
| Es wird keine `realistic-radio.log` angelegt | Die DLL wurde nicht geladen. Ordner prüfen: Er muss `bin\win_x64\plugins\` heißen. Wurde die Meldung zu den „erweiterten SDK-Funktionen“ mit OK bestätigt? |
| Radio bei Lautstärke 0 noch hörbar oder zwei Sender gleichzeitig | Das Plugin ist doppelt installiert (z. B. `realistic-radio (1).dll`). Im Log steht dann `WARNING: another radio plugin DLL is in the plugins folder`; nur eine DLL behalten. Eine zweite Kopie dieser Version bleibt von selbst inaktiv. |
| „Data file not found“ im Log | Das Plugin konnte seinen Ordner neben der DLL nicht schreiben (z. B. keine Schreibrechte im Spielordner). Im Log nach „Could not write“ suchen. |
| Kein Ton, Log zeigt `Connected:` | Standard-Audiogerät von Windows prüfen (steht im Log als `Audio device: …`). Zündung an? Stumm geschaltet? |
| „MP3 decoder init failed“ | Der Stream ist kein echtes MP3. URL aus dem Log melden oder über eigene Sender ersetzen. |
| „Media Foundation cannot open …“ | Windows kann dieses Format nicht abspielen (z. B. manche HLS- oder Ogg-Streams). Anderen Sender wählen oder URL ersetzen. |
| Overlay zeigt „Sender nicht erreichbar“ | Der Stream ist gerade nicht erreichbar. Das Plugin versucht es im Hintergrund weiter; meist ist Weiterschalten schneller. |
| Kein Overlay | Im Log nach `Overlay:` suchen. `Present hook installed` und `D3D11 renderer ready` bedeuten, dass es aktiv ist. Bei Konflikten mit einem anderen Overlay-Tool `"mode": "window"` probieren. |
| Overlay verdeckt einen Teil des HUD | `position`, `offsetY` oder `scale` ändern. |
| Kein Senderlogo | Logos werden beim ersten Gebrauch von `radio.scufystudio.de` geladen; ohne Internet oder bei Serverausfall erscheint das Panel ohne Logo, und nach 10 Minuten wird es erneut versucht. |
| Tasten tun nichts | Ist das Spiel das aktive Fenster? Eventuell belegt ein anderes Programm die Taste; eine andere wählen. |
| Ein erwarteter Sender fehlt | Er ist der Region, in der du bist, nicht zugeordnet. Du empfängst nur Regionen in Reichweite. Als eigenen Sender ergänzen. |

## Bekannte Einschränkungen

- **Kein Text im eingebauten Radio-Display des LKW.** Das SCS-Telemetrie-SDK kann Daten lesen und Eingabegeräte bereitstellen, aber das Cockpit nicht verändern. Stattdessen gibt es das Overlay und optional das Display-Fenster für PrismTextureStreamer.
- **Das spieleigene Radio wird nicht gesteuert.** Das Plugin spielt seinen eigenen Ton über Windows ab.
- **Nicht jeder Stream funktioniert.** Sender-URLs ändern sich mit der Zeit. Manche AAC/HLS-Streams kann Windows Media Foundation nicht öffnen; jede URL lässt sich über eigene Sender ersetzen.
- **Das Overlay hängt sich in die DirectX-11-Ausgabe des Spiels.** Mit dem OpenGL-Renderer weicht das Plugin auf ein transparentes Fenster aus; über exklusivem Vollbild ist das nicht garantiert. Konflikte mit anderen Overlay-Tools (Steam-Overlay, RTSS, ReShade, Multiplayer-Clients) sind möglich.
- **Die DLL bleibt bis zum Spielende geladen**, sobald das Overlay aktiv ist. Das ist Absicht, damit das Spiel nie in entladenen Code springt.
- **3 Sender haben kein Logo** (deren Logos gibt es nur als SVG).
- **American Truck Simulator:** Daten sind enthalten und werden automatisch erkannt, ATS ist aber ungetestet.

## Wie es funktioniert

Alles, was so ein Radio braucht, läuft im Plugin selbst, ohne separates Programm:

| Teil | Umsetzung |
|---|---|
| LKW-Position, Zündung, Motor | Direkt aus dem SCS-Telemetrie-SDK (`truck.world.placement`, `truck.electric.enabled`, `truck.engine.enabled`) |
| Senderwahl, Empfang, Rauschen | `RadioManager` und `ReceptionCalculator` in C++ |
| Wiedergabe | miniaudio (WASAPI), WinHTTP und Windows Media Foundation |
| Bedienung, Favoriten, Einstellungen | Radio-Thread in der DLL; Einstellungen und Favoriten als JSON-Dateien |
| Anzeige | Eigenes Direct3D-11-Overlay |

```
ETS2 ──scs_telemetry_init──► Plugin.cpp ── speichert nur Position und Zündung (atomar, blockiert nie)
                                 │
                                 ▼
                           Radio-Thread (alle 50 ms)
                           ├─ InputManager        Tastatur / Controller
                           ├─ RadioManager        Senderlogik aus main.js
                           │   ├─ StationDatabase, Favoriten, eigene Sender
                           │   └─ ReceptionCalculator
                           ├─ Overlay             Panel-Darstellung, Logo-Zwischenspeicher
                           │   └─ D3D11Overlay    Hook auf IDXGISwapChain::Present
                           ├─ DisplayWindow       optionales Fenster für PrismTextureStreamer
                           └─ AudioEngine         Mischer: Stream + Rauschen + Umschaltgeräusch
                               └─ StreamSession   ein Thread pro Sender, 4 s Puffer, Wiederverbindung
```

Grundregeln im Code:

- `DllMain` tut nichts. Threads, Audiogerät und Netzwerk starten erst in `scs_telemetry_init`, wie vom SDK vorgesehen.
- Spiel-Callbacks speichern nur Werte und warten nie auf Ton oder Netzwerk.
- Stream-Threads halten eine Referenz auf die DLL, damit ein langsamer Netzwerkaufruf beim Entladen des Plugins das Spiel nicht abstürzen lässt.
- Das Overlay sichert vor dem Zeichnen den kompletten Direct3D-Zustand des Spiels und stellt ihn danach wieder her (`SwapDeviceContextState`).

## Selbst kompilieren

**Voraussetzungen:** CMake 3.16 oder neuer und entweder Visual Studio 2019/2022 (x64) oder eine MinGW-w64-/llvm-mingw-Toolchain. Alle Bibliotheken liegen im Repository; es muss nichts heruntergeladen werden.

**Visual Studio (Windows):**

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

**Cross-Kompilierung unter Linux** (so werden die Release-DLLs gebaut, mit llvm-mingw 20241217):

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Ergebnis ist eine einzige `realistic-radio.dll`. Die Dateien aus `data/` (Senderlisten, Sounds, Beispieldatei) werden über `src/resources.rc.in` eingebettet; nach Änderungen an den Senderdaten einfach neu bauen.

**Senderdaten neu erzeugen:** `tools/convert_data.js` wandelt Sender- und Städtelisten aus dem JavaScript-Format, in dem sie veröffentlicht wurden, in das JSON-Format des Plugins um (Node.js wird nur für diesen Schritt gebraucht, nie von Spielern):

```sh
git clone <Repository mit den Senderdaten>
node tools/convert_data.js <Pfad>/web data
```

**Regressionstest der Senderwahl:** `tools/equivalence-test/` lässt eine JavaScript-Referenzimplementierung und den C++-Code nebeneinander an Tausenden Positionen laufen und vergleicht die Ergebnisse.

### Verwendete Bibliotheken

| Bibliothek | Version | Lizenz | Zweck |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.25 | Public Domain oder MIT-0 | Audioausgabe, MP3-Decoding, Resampling |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | Lesen der JSON-Dateien |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) (nur 5×7-Schrift) | – | BSD | Punktmatrix-Zeichen des Autoradio-Displays |
| SCS-Telemetrie-SDK | 1.x | MIT-ähnlich, siehe `sdk/sdk_license.txt` | Schnittstelle zum Spiel |
| Windows-Systembibliotheken | – | – | WinHTTP, Media Foundation, Direct3D 11, DXGI, WIC, GDI, WinMM, URLMon; `d3dcompiler_47.dll` zur Laufzeit (Teil von Windows 10/11) |

Die DLL ist statisch gelinkt; ein Visual-C++-Redistributable wird nicht benötigt.

### Logo-Server

Das Plugin lädt das Logo eines Senders von `<urlPrefix><Logo-Pfad>`. Der Logo-Pfad ist der `logo`-Wert aus den Senderdaten, z. B.
`stations/images-europe/germany/80s80s.png` → `https://radio.scufystudio.de/logo/stations/images-europe/germany/80s80s.png`.
Leerzeichen und Sonderzeichen werden URL-kodiert (UTF-8), z. B. `Radio Bollerwagen.png` → `Radio%20Bollerwagen.png`.
Der Ordner `server/logo/` enthält alle Logos (verkleinert auf 200 px) in genau dieser Ordnerstruktur. Geladene Logos werden lokal in `realistic-radio\logos\` gespeichert und nicht erneut geladen.

## Projektstruktur

```
├── CMakeLists.txt             Build-Definition
├── toolchain-mingw.cmake      Toolchain für Cross-Kompilierung
├── src/                       C++-Quellcode des Plugins
│   ├── Plugin.cpp             SDK-Einstiegspunkte, Telemetrie, Radio-Thread
│   ├── RadioManager.*         Senderlogik
│   ├── ReceptionCalculator.*  Reichweite und Empfang
│   ├── StationDatabase.*      Sender, Städte, Favoriten, eigene Sender
│   ├── AudioEngine.*          Mischer und Audiogerät
│   ├── StreamSource.*         Netzwerk-Streams, Decoding, Wiederverbindung
│   ├── NetReader.h            macht den Anfang eines Live-Streams zurückspulbar
│   ├── Overlay.*, OverlayImage.*, D3D11Overlay.*   Ingame-Overlay
│   ├── DisplayWindow.*        optionales Display-Fenster
│   ├── InputManager.*         Tastatur und Controller
│   ├── Config.*, Logger.*     Einstellungen und Log
│   ├── EmbeddedData.*, resources.rc.in   eingebettete Datendateien, beim ersten Start entpackt
│   └── MiniaudioImpl.cpp      Übersetzungseinheit für miniaudio
├── data/                      Sender-/Städtedaten und Sounds (in die DLL eingebettet)
├── server/logo/               Senderlogos für den Logo-Server (radio.scufystudio.de/logo/)
├── sdk/                       Header des SCS-Telemetrie-SDK
├── third_party/               miniaudio, nlohmann/json, 5×7-Schrift (Adafruit GFX)
└── tools/                     Datenkonverter, Vergleichstest
```

## Lizenz und Danksagung

Realistic Radio © 2026 **ScufyKing**. Lizenziert unter der **Mozilla Public License 2.0** (MPL-2.0), siehe [`LICENSE`](LICENSE).

Die Senderlisten, Städtekoordinaten, das Rausch- und Umschaltgeräusch sowie Teile der Senderwahl-Logik stammen aus Drittquellen unter MPL-2.0. Die Lizenz verlangt, deren Urheberhinweise beizubehalten; sie stehen in [`NOTICE.md`](NOTICE.md), zusammen mit den Hinweisen zu den verwendeten Bibliotheken. Senderlogos und Radiostreams gehören den jeweiligen Sendern.

Dieses Projekt steht in keiner Verbindung zu SCS Software und wird nicht von SCS Software unterstützt. Euro Truck Simulator 2 und American Truck Simulator sind Marken von SCS Software.

**Mitmachen:** Issues und Pull Requests sind willkommen. Bitte hänge bei Fehlermeldungen die `realistic-radio.log` an und melde kaputte Sender-URLs mit Sendername und Region.
