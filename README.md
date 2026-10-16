# Realistic Radio

**English** · [Deutsch](README.de.md)

Local, real-world radio for **Euro Truck Simulator 2**, as a single native plugin DLL.
While you drive, the plugin works out which region you are in and plays that region's real internet radio stations, with static when reception gets weak and a smooth switch when you cross a border.

By **ScufyKing**. The whole radio runs inside the game:

- no server EXE, no background program
- no browser, no web page, no local HTTP server
- no Python or Node.js for players
- no changes to game files

Copy one file, `realistic-radio.dll`, into the game's `plugins` folder, start ETS2, and the radio plays. Station data and sounds are built into the DLL.

> **Status:** Tested in ETS2 1.61 (Windows x64): the plugin loads, reads the truck position, switches stations by region, plays audio and draws the in-game overlay. American Truck Simulator data is included but ATS is untested.

---

## Contents

- [Features](#features)
- [Installation](#installation)
- [Controls](#controls)
- [Configuration](#configuration)
- [Adding your own stations](#adding-your-own-stations)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Project structure](#project-structure)
- [License and credits](#license-and-credits)

---

## Features

- **Stations by location.** About 2,100 internet radio stations, assigned to 2,064 cities on the base map, the DLC maps and many map mods (ProMods, RusMap and others).
- **Realistic range and switching.** Every city has a broadcast range; regions overlap at borders, and the radio only switches automatically when another region is clearly better received.
- **Reception and static.** The further you are from a region, the more static you hear, and weak signals drop out now and then.
- **Smooth transitions** with a tuning sound.
- **In-game overlay.** On every station change, volume change and so on, a panel appears for a few seconds with the station name, country, signal bars, favourite heart and station logo. Works in fullscreen. Logos are loaded from `radio.scufystudio.de` once and then kept locally.
- **Radio only with the ignition on** (or only with the engine running, or always; your choice).
- **Keyboard and controller controls**, freely configurable.
- **Favourites per country.**
- **Your own stations** in a separate file that survives updates.
- **Automatic reconnect** when a stream drops.
- **Plays MP3, AAC and HLS streams** (MP3 with a built-in decoder, AAC/HLS through Windows Media Foundation) and local audio files.

## Installation

**Requirements:** Windows 10 or 11 (64-bit), Euro Truck Simulator 2 (64-bit), an internet connection for the streams.

1. Download `realistic-radio.dll`.
2. Open your game folder's plugin directory:
   `…\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   If the `plugins` folder does not exist, create it.
3. Copy `realistic-radio.dll` into it. That is all — no other files are needed.
4. Remove any other radio plugin DLL from this folder (e.g. an older `local-radio-native.dll` or `local-radio.dll`), so two radios don't play at once.
5. Start ETS2. The game asks whether to allow "advanced SDK features"; confirm with **OK**.

On the first start the plugin creates the folder `realistic-radio\` next to the DLL and unpacks its station data and sounds into it. The radio starts automatically as soon as the truck has a position and the ignition is on.

Afterwards the folder looks like this (everything except the DLL is created automatically):

```
plugins\
├── realistic-radio.dll
└── realistic-radio\
    ├── ets2.json                    station and city data (ETS2, from the DLL)
    ├── ats.json                     station and city data (ATS, from the DLL)
    ├── custom-stations-ets2.example.json   example for your own stations
    ├── custom-stations-ets2.json    your own stations (optional, created by you)
    ├── audio\                       static and tuning sound (from the DLL)
    ├── realistic-radio.json         settings (created on first start)
    ├── favourites-ets2.json         your favourites (created automatically)
    ├── logos\                       downloaded station logos (cache)
    └── realistic-radio.log          log file
```

**Updating:** close ETS2 and replace `realistic-radio.dll`. If the new DLL contains newer station data, it replaces `ets2.json`, `ats.json` and the sounds on the next start. Your settings, favourites, own stations and logos stay untouched.

**Coming from "ETS2 Local Radio Native"** (the previous name): copy `realistic-radio.dll` into `plugins` and delete `local-radio-native.dll`. On the first start, your settings, favourites, own stations and cached logos are copied over from the old `local-radio\` folder automatically; afterwards you can delete that folder too.

**Uninstalling:** delete `realistic-radio.dll` and the `realistic-radio` folder.

## Controls

| Action | Default key | Setting in `realistic-radio.json` |
|---|---|---|
| Next station | → (Right arrow) | `keys.next` |
| Previous station | ← (Left arrow) | `keys.previous` |
| Stop / start | End | `keys.stop` |
| Volume up (5 % steps) | ↑ (Up arrow) | `keys.volumeUp` |
| Volume down (5 % steps) | ↓ (Down arrow) | `keys.volumeDown` |
| Save current station as favourite for this country | Pause | `keys.makeFavourite` |
| Go to favourite | – | `keys.goToFavourite` |
| Mute | – | `keys.mute` |
| Show overlay | – | `keys.showOverlay` |

"Next" and "Previous" step through every station of every region you currently receive.

Keys only react while ETS2 is the active window, so they do not interfere with other programs.

If the arrow keys are also bound to driving or menus in ETS2, both happen at the same time. In that case use other keys or a combination such as `"Ctrl+Right"`.

**Key names** use the .NET key names: letters `A`–`Z`, digits `0`–`9`, `F1`–`F24`, `NumPad0`–`NumPad9`, `PageUp`, `Next` (= Page Down), `Home`, `End`, `Insert`, `Delete`, `Up`, `Down`, `Left`, `Right`, `Pause`, `Space`, `Oemplus`, `OemMinus`, `Add`, `Subtract`, `MediaNextTrack`, `MediaPlayPause` and more. Combinations work too, e.g. `"Ctrl+Shift+R"`. An empty value `""` disables a key.

**Controller:** set `controllerIndex` (0 = first controller) and give each action a button number under `buttons`, counting from 0. `-1` disables it. The button numbers match those shown in the Windows controller settings minus 1.

## Configuration

All settings are in `realistic-radio\realistic-radio.json`. The file is created with default values on first start; change it while the game is closed. Comments (`// …`) are allowed.

### Radio

| Key | Default | Meaning |
|---|---|---|
| `radius` | `7500` | Base broadcast range of a city in game metres |
| `threshold` | `0.15` | How much better another region must be received before the radio switches automatically. `0` = switch immediately, `1` = never |
| `whitenoise` | `true` | Static when reception is weak |
| `transitionWhitenoise` | `true` | Tuning sound when changing stations |
| `volume` | `0.6` | Volume from 0.0 to 1.0 (saved automatically). The scale is perceptual: 50 % is clearly quieter than full, 10 % is very quiet, 0 is always silent |
| `muted` | `false` | Muted (saved automatically) |
| `autoPlay` | `true` | Start playing automatically |
| `powerSource` | `"ignition"` | `"ignition"` = only with ignition on, `"engine"` = only with the engine running, `"always"` = always |
| `dataFile` | `""` | Different station/city file in the `realistic-radio` folder (empty = `ets2.json` or `ats.json`) |
| `onlyWhenGameFocused` | `true` | Keys only work while the game is in the foreground |
| `reconnectMaxSeconds` | `30` | Maximum wait between reconnect attempts after a stream drops |

### Overlay (`"overlay": { … }`)

| Key | Default | Meaning |
|---|---|---|
| `style` | `"panel"` | `"panel"` = info panel with logo, `"radio"` = car-radio dot-matrix display (see below) |
| `mode` | `"auto"` | `"auto"` (recommended), `"d3d11"` (draw in the game image), `"window"` (transparent window over the game), `"off"` |
| `position` | `"top-center"` | `top-left`, `top-center`, `top-right`, `bottom-left`, `bottom-center`, `bottom-right` |
| `offsetX`, `offsetY` | `0`, `0.07` | Distance from the screen edge as a fraction of screen height (0.07 = 7 %) |
| `scale` | `1.0` | Size; also scales automatically with your resolution |
| `durationSeconds` | `6` | How long the panel stays visible |
| `alwaysVisible` | `false` | Keep the panel on screen all the time |
| `showLogos` | `true` | Show station logos |
| `urlPrefix` | `"https://radio.scufystudio.de/logo/"` | Where missing logos are downloaded from. Downloaded logos are kept in `realistic-radio\logos\` and never downloaded again. Empty = only logos already in that folder, no network access |
| `textNowPlaying`, `textVolume`, `textStopped`, `textMuted`, `textFavourite`, `textConnecting`, `textUnavailable`, `textPowerOff` | German texts | All texts on the overlay. For English, use e.g. `"Now playing:"`, `"Volume"`, `"Radio off"`, `"Muted"`, `"Saved as favourite"`, `"connecting…"`, `"Station unavailable"`, `"Ignition off"` |

#### Car-radio display (`"style": "radio"`)

Instead of the panel, a narrow dot-matrix display like a real car radio appears above the dashboard: station name in capitals, a scrolling marquee for long names, and a signal indicator. Volume, mute and other messages are shown on the same display. Settings under `"overlay": { "radio": { … } }`:

| Key | Default | Meaning |
|---|---|---|
| `position` | `"bottom-center"` | Same values as the panel position |
| `offsetX`, `offsetY` | `0`, `0.20` | Distance from the screen edge as a fraction of screen height |
| `scale` | `1.0` | Size |
| `columns` | `16` | Characters on the display (8–40) |
| `color` | `"amber"` | `amber`, `green`, `blue`, `white`, `red`, `orange` or `#RRGGBB` |
| `scrollSeconds` | `0.3` | Marquee speed: seconds per character |

The dot-matrix characters use the classic 5×7 font from the Adafruit GFX Library (BSD license, see `NOTICE.md`); letters it lacks are shown as their base letter (À → A), other scripts such as Cyrillic are drawn with a Windows font.

### Truck screen display (`"display": { … }`, optional)

The plugin can open a native window called "Realistic Radio Display" that shows the radio as an infotainment screen. The separate plugin [PrismTextureStreamer](https://github.com/Baldywaldy09/PrismTextureStreamer) (not part of this project) can mirror that window onto the truck's GPS or dashboard screen. It is off by default.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Open the display window |
| `width`, `height` | `1280`, `720` | Size in pixels |
| `offscreen` | `true` | Place the window outside the visible desktop. If it stays black in PrismTextureStreamer, set `false` |
| `title` | `"Realistic Radio Display"` | Window title |

## Adding your own stations

Copy `realistic-radio\custom-stations-ets2.example.json` to `custom-stations-ets2.json` in the same folder and put your stations there. Updates never ship a `custom-stations-ets2.json`, so your file is never overwritten.

```jsonc
{
  // key = region from ets2.json, e.g. "germany" (nationwide), "germany-bw", "germany-br",
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

- A station with the **same name** as an existing one in that region replaces it (e.g. to fix a broken stream URL).
- A station with a **new name** is added to the end of that region's list.
- `logo` is optional: a full `https://` address of an image (PNG or JPG), or a file name. A file name is looked up in `realistic-radio\logos\` first and otherwise downloaded from `urlPrefix`.
- `url` can also be a local audio file. A relative path is looked up in the `realistic-radio` folder.

To find the region names, open `ets2.json` and look under `"stations"`. Regions with a hyphen are subregions, e.g. `germany-swr` or `germany-muenchen`. Near the border you often receive several regions at once.

## Troubleshooting

Almost every problem can be diagnosed from `realistic-radio\realistic-radio.log`. When asking for help, always attach this file.

| Problem | Cause and solution |
|---|---|
| No `realistic-radio.log` is created | The DLL was not loaded. Check the folder: it must be `bin\win_x64\plugins\`. Did you confirm the "advanced SDK features" message with OK? |
| Radio still audible at volume 0, or two stations at once | Two copies of the plugin are installed (e.g. `realistic-radio (1).dll`). The log then says `WARNING: another radio plugin DLL is in the plugins folder`; keep only one DLL. A second copy of this version stays inactive by itself. |
| "Data file not found" in the log | The plugin could not write its folder next to the DLL (e.g. no write permission in the game folder). Check the log for "Could not write". |
| No sound, log shows `Connected:` | Check Windows' default audio device (shown in the log as `Audio device: …`). Ignition on? Muted? |
| "MP3 decoder init failed" | The stream is not real MP3. Report the URL in the log, or replace it with your own stations. |
| "Media Foundation cannot open …" | Windows cannot play this format (e.g. some HLS or Ogg streams). Switch to another station or replace the URL. |
| Overlay says "Station unavailable" | The stream cannot be reached right now. The plugin keeps retrying in the background; usually it is quicker to switch stations. |
| No overlay | Look for `Overlay:` in the log. `Present hook installed` and `D3D11 renderer ready` mean it is active. If another overlay tool conflicts, try `"mode": "window"`. |
| Overlay hides part of the HUD | Change `position`, `offsetY` or `scale`. |
| No station logo | Logos are downloaded from `radio.scufystudio.de` on first use; without internet, or if the server is down, the panel is shown without a logo and the download is retried after 10 minutes. |
| Keys do nothing | Is the game the active window? Another program may be using the key; pick a different one. |
| A station I expected is missing | It is not assigned to the region you are in. You only receive regions within range. Add it as your own station. |

## Known limitations

- **No text on the truck's built-in radio display.** The SCS Telemetry SDK can read data and provide input devices, but cannot change the cockpit. Instead there is the overlay, plus the optional display window for PrismTextureStreamer.
- **The game's own radio is not controlled.** The plugin plays its own audio through Windows.
- **Not every stream works.** Station URLs change over time. Some AAC/HLS streams cannot be opened by Windows Media Foundation; you can replace any URL with your own stations.
- **The overlay hooks into the game's DirectX 11 output.** With the OpenGL renderer, the plugin falls back to a transparent window, which is not guaranteed over exclusive fullscreen. Conflicts with other overlay tools (Steam overlay, RTSS, ReShade, multiplayer clients) are possible.
- **The DLL stays loaded until the game exits** once the overlay is active. This is intentional, so the game never calls into unloaded code.
- **3 stations have no logo** (their logos only exist as SVG).
- **American Truck Simulator** data is included and detected automatically, but ATS has not been tested.

## How it works

Everything a radio of this kind needs runs inside the plugin, with no separate program:

| Part | Implementation |
|---|---|
| Truck position, ignition, engine | Read directly from the SCS Telemetry SDK (`truck.world.placement`, `truck.electric.enabled`, `truck.engine.enabled`) |
| Station choice, reception, static | `RadioManager` and `ReceptionCalculator` in C++ |
| Playback | miniaudio (WASAPI), WinHTTP and Windows Media Foundation |
| Controls, favourites, settings | Radio thread in the DLL; settings and favourites as JSON files |
| Display | Own Direct3D 11 overlay |

```
ETS2 ──scs_telemetry_init──► Plugin.cpp ── stores only position and ignition (atomic, never blocks)
                                 │
                                 ▼
                           radio thread (every 50 ms)
                           ├─ InputManager        keyboard / controller
                           ├─ RadioManager        station logic ported from main.js
                           │   ├─ StationDatabase, Favourites, custom stations
                           │   └─ ReceptionCalculator
                           ├─ Overlay             panel rendering, logo cache
                           │   └─ D3D11Overlay    hook on IDXGISwapChain::Present
                           ├─ DisplayWindow       optional window for PrismTextureStreamer
                           └─ AudioEngine         mixer: stream + static + tuning sound
                               └─ StreamSession   one thread per station, 4 s buffer, reconnect
```

Design rules:

- `DllMain` does nothing. Threads, audio device and network start only in `scs_telemetry_init`, as the SDK intends.
- Game callbacks only store values and never wait for audio or network.
- Stream threads hold a reference on the DLL, so a slow network call cannot crash the game when the plugin is unloaded.
- The overlay saves and restores the game's complete Direct3D pipeline state (`SwapDeviceContextState`) around its own drawing.

## Building from source

**Requirements:** CMake 3.16 or newer and either Visual Studio 2019/2022 (x64) or a MinGW-w64 / llvm-mingw toolchain. All libraries are included in the repository; nothing has to be downloaded.

**Visual Studio (Windows):**

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

**Cross-compile on Linux** (the release DLLs are built this way, with llvm-mingw 20241217):

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is a single `realistic-radio.dll`. The files in `data/` (station lists, sounds, example file) are embedded into it via `src/resources.rc.in`, so after changing station data just rebuild.

**Regenerating the station data:** `tools/convert_data.js` converts station and city lists in the JavaScript format they were published in into the plugin's JSON format (Node.js is only needed for this step, never by players):

```sh
git clone <station data repository>
node tools/convert_data.js <path>/web data
```

**Regression test for station selection:** `tools/equivalence-test/` runs a JavaScript reference implementation and the C++ code side by side at thousands of positions and compares the results.

### Libraries used

| Library | Version | License | Purpose |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.25 | Public domain or MIT-0 | Audio output, MP3 decoding, resampling |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | Reading the JSON files |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) (5×7 font only) | – | BSD | Dot-matrix characters of the car-radio display |
| SCS Telemetry SDK | 1.x | MIT-style, see `sdk/sdk_license.txt` | Interface to the game |
| Windows system libraries | – | – | WinHTTP, Media Foundation, Direct3D 11, DXGI, WIC, GDI, WinMM, URLMon; `d3dcompiler_47.dll` at runtime (part of Windows 10/11) |

The DLL is statically linked; no Visual C++ redistributable is needed.

### Logo server

The plugin downloads a station's logo from `<urlPrefix><logo path>`, where the logo path is the `logo` value from the station data, e.g.
`stations/images-europe/germany/80s80s.png` → `https://radio.scufystudio.de/logo/stations/images-europe/germany/80s80s.png`.
Spaces and special characters are URL-encoded (UTF-8), e.g. `Radio Bollerwagen.png` → `Radio%20Bollerwagen.png`.
The folder `server/logo/` contains all logos (downscaled to 200 px) in exactly this folder structure. Downloaded logos are stored locally in `realistic-radio\logos\` and are not downloaded again.

## Project structure

```
├── CMakeLists.txt             build definition
├── toolchain-mingw.cmake      toolchain for cross-compiling
├── src/                       C++ source of the plugin
│   ├── Plugin.cpp             SDK entry points, telemetry, radio thread
│   ├── RadioManager.*         station logic
│   ├── ReceptionCalculator.*  range and reception
│   ├── StationDatabase.*      stations, cities, favourites, own stations
│   ├── AudioEngine.*          mixer and audio device
│   ├── StreamSource.*         network streams, decoding, reconnect
│   ├── NetReader.h            makes the start of a live stream rewindable
│   ├── Overlay.*, OverlayImage.*, D3D11Overlay.*   in-game overlay
│   ├── DisplayWindow.*        optional display window
│   ├── InputManager.*         keyboard and controller
│   ├── Config.*, Logger.*     settings and log
│   ├── EmbeddedData.*, resources.rc.in   embedded data files, unpacked on first start
│   └── MiniaudioImpl.cpp      miniaudio compilation unit
├── data/                      station/city data and sounds (embedded into the DLL)
├── server/logo/               station logos for the logo server (radio.scufystudio.de/logo/)
├── sdk/                       SCS Telemetry SDK headers
├── third_party/               miniaudio, nlohmann/json, 5x7 font (Adafruit GFX)
└── tools/                     data converter, equivalence test
```

## License and credits

Realistic Radio © 2026 **ScufyKing**. Licensed under the **Mozilla Public License 2.0** (MPL-2.0), see [`LICENSE`](LICENSE).

The station lists, city coordinates, the static and tuning sounds and parts of the station selection logic are taken from MPL-2.0 licensed third-party sources. The license requires their copyright notices to be kept; they are listed in [`NOTICE.md`](NOTICE.md), together with the notices for the libraries used. Station logos and radio streams belong to their respective broadcasters.

This project is not affiliated with or endorsed by SCS Software. Euro Truck Simulator 2 and American Truck Simulator are trademarks of SCS Software.

**Contributing:** Issues and pull requests are welcome. Please attach `realistic-radio.log` to bug reports, and report broken station URLs together with the station name and region.
