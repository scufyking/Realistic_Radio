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

> **Version 1.4.7** · [Changes](CHANGELOG.md)
>
> **Status:** Tested in ETS2 1.61 (Windows x64) with several MAN and Scania models and mod trucks: the plugin loads, reads the truck position, switches stations by region, plays audio and shows station and volume on the navigation screen. American Truck Simulator data is included but ATS is untested.

---

## Contents

- [Features](#features)
- [Installation](#installation)
- [Controls](#controls)
- [Settings menu (Ctrl + 0)](#settings-menu-ctrl--0)
- [Configuration](#configuration)
- [USB / CD: your own music](#usb--cd-your-own-music)
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
- **Auto volume:** gets louder at higher speed like a real car radio (adjustable, off by default).
- **Realistic range and switching.** Every city has a broadcast range; regions overlap at borders. The radio keeps the station you are listening to as long as it is in range and only then changes to another region (or, if you prefer, as soon as another region is clearly better received). The same programme from several regions (e.g. SWR1 BW / SWR1 Rheinland-Pfalz, ENERGY Stuttgart / ENERGY München) counts as one station: the radio simply stays on it.
- **FM and DAB+.** Switch the band like on a real radio: FM with static on weak reception, DAB+ with a longer range, no static and the typical drop-outs at the edge of coverage.
- **Station buttons 1–9** on the numpad like on a car radio: hold to save, tap to select – pre-stored with the stations received where you start.
- **Reception and static.** The further you are from a region, the more static you hear, and weak signals drop out now and then.
- **Smooth transitions** with a tuning sound.
- **Radio page on the navigation screen.** Press **Insert** to switch between the navigation and a permanent radio page that shows the station or track live.
- **Display on the truck's navigation screen.** On every station change the logo, station name, country, signal and "station 3 / 12" appear on the in-cab navigation screen for a few seconds, on volume changes a speaker symbol, a large percentage and a 20-segment bar; then the navigation is back. Works in trucks with a modern infotainment/navigation screen (tested: several MAN and Scania models and mod trucks; see "In-cab screen").
- **Live song title.** Artist and title of the song the station is playing right now appear on the navigation screen – switched exactly when the song changes in what you hear, and scrolling when the title is long (settings menu → Radio → **Live song title**).
- **Radio memory per truck** (optional): every truck keeps its own stations, favourites, volume, source and USB track.
- **Truck presets:** the position and design of the screen display can be saved per truck; the right preset is loaded automatically by the truck id from the telemetry.
- **Start screen** when the ignition is switched on: the truck's manufacturer fades in on black, like on modern infotainment systems (your own logo pictures can be added).
- **Optional pop-up overlay** (off by default): a panel at the top of the screen or a car-radio style dot-matrix display. Works in fullscreen.
- **Radio only with the ignition on** (or only with the engine running, or always; your choice).
- **Keyboard and controller controls**, freely configurable.
- **Settings menu in the game** (**Ctrl + 0**) with mouse and keyboard: volume, reception, navigation display, overlay, USB/CD and key bindings, without editing the JSON file.
- **Favourites per country.**
- **USB / CD mode for your own music.** Put music files into `realistic-radio\usb\`; every sub folder is its own USB stick or CD. **Home** steps through: radio → USB 1 → USB 2 → … → radio. Title, artist, album, cover, track number and playing time appear on the navigation screen.
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
    ├── trucks-ets2.json             radio memory per truck (if switched on)
    ├── tunnels-own.json             your own tunnels (tunnel key, optional)
    ├── usb\                         your own music for the USB/CD mode (you put it there)
    ├── logos\                       downloaded station logos and covers (cache)
    ├── truck-logos\                 logos for the start screen (delivered ones + your own): a folder per brand (man\, scania\, …) or mod truck
    └── realistic-radio.log          log file
```

**Updating:** close ETS2 and replace `realistic-radio.dll`. If the new DLL contains newer station data, it replaces `ets2.json`, `ats.json` and the sounds on the next start. Your settings, favourites, own stations and logos stay untouched.

**Coming from "ETS2 Local Radio Native"** (the previous name): copy `realistic-radio.dll` into `plugins` and delete `local-radio-native.dll`. On the first start, your settings, favourites, own stations and cached logos are copied over from the old `local-radio\` folder automatically; afterwards you can delete that folder too.

**Uninstalling:** delete `realistic-radio.dll` and the `realistic-radio` folder.

### Together with Trucker Chat

**Trucker Chat** (stream live chat from Twitch / YouTube on the navigation screen, also by ScufyKing) runs as its own plugin next to Realistic Radio: `realistic-radio.dll` and `trucker-chat.dll` both go into `plugins\`.

- **From 1.4.3 Realistic Radio draws the chat.** Trucker Chat (0.1.1 Alpha or newer) detects the radio at start and then does not hook Direct3D itself; it hands its pictures to the radio. Two hooks on the same functions could crash the game; now there is only one. `trucker-chat.log` then says `Realistic Radio: linked`.
- The chat draws where the radio draws: drawing surface, area and flip come from the radio's truck presets, so the navigation area is set up once.
- While the radio shows a card (station change, volume, start logo), a chat message appears afterwards. If the radio display is pinned on the screen, the chat is drawn on top.
- Typing in the chat menu (**Ctrl + 9**) does not control the radio.
- For developers: the interface (`rr_api_version`, `rr_chat_screen_image` …) is described in `src/PluginApi.cpp`.

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
| Switch source: radio → USB 1 → USB 2 → … → radio | Home | `keys.source` |
| Next / previous USB or CD | – | `keys.nextDisc`, `keys.previousDisc` |
| Navigation screen: navigation ↔ radio page | Insert | `keys.screenPage` |
| Open the settings menu | Ctrl + 0 **or** Ctrl + F1 | `keys.menu`, `keys.menu2` |
| Band: FM ↔ DAB+ | Shift + Home | `keys.band` |
| Station buttons 1–9: tap = select, hold = save | NumPad 1–9 | `keys.memory1` … `keys.memory9` |
| Frequency radio: tune 0.1 MHz up / down | Shift + → / Shift + ← | `keys.tuneUp`, `keys.tuneDown` |
| Mark an own tunnel (press at the entry and at the exit) | – | `keys.tunnelMark` |

"Next" and "Previous" step through every station of every region you currently receive; the same programme from several regions appears only once. In USB/CD mode the keys work differently, see [USB / CD](#usb--cd-your-own-music).

Keys only react while ETS2 is the active window, so they do not interfere with other programs.

If the arrow keys are also bound to driving or menus in ETS2, both happen at the same time. In that case use other keys or a combination such as `"Ctrl+Right"`.

**Changing keys in the game:** **Ctrl + 0** opens the [settings menu](#settings-menu-ctrl--0). On the **Keys** page click the field of a function and press the new key, key combination or controller button.

### No reception in tunnels

Drive into a tunnel and the radio loses its signal like a real one: **FM** fades into static within about two seconds, **DAB+** stutters for a moment and then goes silent; at the exit the programme comes back. USB / CD keeps playing. Short underpasses and bridges do not count – only covered stretches of 200 m and more.

The tunnels come from the databases of **ETS2 Tunnel Radio** by Tal (bohbotoly, MIT license, see NOTICE.md), built from the game's own map data: **ETS2 1.60 with all map DLCs** (4,238 tunnel pieces) and **ProMods 2.83 + Middle-East** (5,446). The plugin reads `game.log.txt` in your documents folder and uses the ProMods database when ProMods is loaded. For map mods or a tunnel that is missing, assign a key to **Mark an own tunnel** (page Keys), press it at the tunnel entry and again at the exit – the tunnel is saved in `realistic-radio\tunnels-own.json`. Settings menu → Reception → **No reception in tunnels** switches it off. ATS has no tunnel database (own tunnels work).

### Frequency radio (like in the old days)

Settings menu → Radio → **Tuning (FM): Frequency**. Every station has an FM frequency, and the radio stays on the frequency you tuned in – like a car radio before RDS:

- **Next / Previous** seek to the next frequency with a station, **Shift + → / ←** tune in 0.1 MHz steps (on an empty frequency there is only static).
- The screen shows the frequency, e.g. "94,7 MHz".
- Drive on and the station on your frequency can fade into static, or **another station takes the frequency over**.
- At borders two stations can be on the same frequency: the weaker one **breaks through for a few seconds** now and then, with extra noise.
- **Station buttons store the frequency**, not the station: in another area the button plays whatever is broadcast on that frequency there.
- **RDS AF** (off by default) makes the radio follow the programme to its frequency in the next area, like a modern radio.

The frequencies are planned by the plugin (the same every time; stations of one region never share a frequency, neighbouring regions mostly avoid each other's). Own or corrected frequencies can be set in the custom stations file with `"frequency": 94.7`. DAB+ has no frequencies: in DAB+ the station list is used.

### Station buttons (numpad)

Like the preset buttons of a car radio: **hold** a numpad key (1–9) for 1.5 seconds and the current station is saved on it; **tap** it to tune in. The screen shows "Button 3" next to a saved station. If the saved station is not in range where you are, the radio uses the same programme from another region (SWR1 BW → SWR1 Rheinland-Pfalz) or tells you it is out of range. Settings menu → Radio: switch the station buttons off or change the hold time; the keys can be changed on the Keys page. NumLock must be on (with NumLock off Windows reports the numpad keys as End, arrows and so on). With **Radio memory per truck** every truck has its own station buttons.

The buttons come **pre-stored**: on the first start they are filled with the nine best received stations where you are (one per programme). Settings menu → Radio → **Store station buttons again** fills them anew with the stations in range – handy in a new country.

You can still enter keys in the file as well. **Key names** use the .NET key names: letters `A`–`Z`, digits `0`–`9`, `F1`–`F24`, `NumPad0`–`NumPad9`, `PageUp`, `Next` (= Page Down), `Home`, `End`, `Insert`, `Delete`, `Up`, `Down`, `Left`, `Right`, `Pause`, `Space`, `Oemplus`, `OemMinus`, `Add`, `Subtract`, `MediaNextTrack`, `MediaPlayPause` and more. Combinations work too, e.g. `"Ctrl+Shift+R"`. An empty value `""` disables a key.

**Controller:** every connected controller works – steering wheels (also with several devices such as Moza or Fanatec: base, wheel, button box), pads and button boxes, with up to 128 buttons and the hat switch (POV). Easiest in the settings menu: Keys page, click the **Controller** column and press the button. In the file, `buttons` holds `"Device name/12"` (button 12 of that controller, `"Device name/POV1 up"` for the hat switch), a plain number (counted from 0, any controller; older files) or `-1` (none). The log lists the controllers found (`Controller: … found`).

## Settings menu (Ctrl + 0)

The most important settings can be changed right in the game. **Ctrl + 0** (or **Ctrl + F1**) opens the menu in the middle of the screen; it works with mouse and keyboard.

| Page | Contents |
|---|---|
| Radio | Volume, mute, radio on with (ignition / engine running / always), start automatically, menu language, **band FM / DAB+** and its key, **station buttons** on/off and hold time, **tuning: station list or frequency**, **RDS AF** |
| Reception | Broadcast range, switch threshold, **change region** (only when the station is lost / on better reception), **DAB+ range**, auto volume, static, tuning sound, **no reception in tunnels**, key to mark an own tunnel |
| Navigation screen | Display on/off, display time, **design**, **start logo** on/off, time and size, **size of text and pictures**, **size of the station logo**, **drawing surface** (if the display appears twice), keep the radio page on, key for navigation ↔ radio page, **test mode**, **automatic detection (beta)** and the position of the display (area X / Y / width / height, flip, mirror) |
| Trucks | Current truck (from the telemetry), **radio memory per truck**, **start logo** (choice, preview, logo folder), new preset, active preset, change automatically per truck, link / unlink, delete, load a saved preset |
| Pop-up overlay | Mode, style, position, size, display time, station logos |
| USB / CD | Mode on/off, every folder its own USB, default type, shuffle, repeat, open the music folder, type of every music folder (USB, CD, SD card, cassette, Bluetooth, AUX) |
| Storage | Maximum size of the logo storage, load station logos, used space and clear, open the logo folder |
| Keys | Key and controller button for every function |

- **Mouse:** click pages and switches, drag sliders, use the wheel over sliders and choices. On the Keys page click a field and press the new key or controller button; right click clears it.
- **Keyboard:** ↑ / ↓ select, ← / → change, Enter toggles or starts recording a key, Tab / Page Up / Page Down changes the page, Delete clears a key binding.
- Changes take effect immediately. **Save** writes them to `realistic-radio.json`, **Revert** goes back to the saved state, **Defaults** restores the current page's default values. **Esc** or **✕** saves and closes.
- Settings marked **Restart** (pop-up mode, navigation display on/off) take effect after restarting the game.
- While the menu is open, keyboard and mouse do not reach the game; the mouse moves the menu's own cursor.
- Everything else (e.g. screen area, texts, folder) stays in the file as before, see below.

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
| `autoVolume` | `0` | Auto volume: louder at higher speed (from 30 km/h, full at 90 km/h). `0` = off, `1` = up to +6 dB. Never louder than 100 % |
| `muted` | `false` | Muted (saved automatically) |
| `autoPlay` | `true` | Start playing automatically |
| `powerSource` | `"ignition"` | `"ignition"` = only with ignition on, `"engine"` = only with the engine running, `"always"` = always |
| `dataFile` | `""` | Different station/city file in the `realistic-radio` folder (empty = `ets2.json` or `ats.json`) |
| `onlyWhenGameFocused` | `true` | Keys only work while the game is in the foreground |
| `reconnectMaxSeconds` | `30` | Maximum wait between reconnect attempts after a stream drops |
| `source` | `"radio"` | Last chosen source, `"radio"` or `"usb"` (saved automatically, resumed on start) |
| `mediaTrack` | – | Last USB track played (saved automatically) |
| `mediaResume` | – | Last track per USB / CD (saved automatically) |
| `menuLanguage` | `"de"` | Language of the settings menu: `"de"` or `"en"` |
| `band` | `"fm"` | Band: `"fm"` (static on weak reception) or `"dab"` (DAB+: longer range, no static, drop-outs at the edge of coverage). Saved automatically |
| `dabRange` | `1.5` | How much further DAB+ reaches than FM (1.0–3.0) |
| `switchMode` | `"lost"` | `"lost"` = change to another region only when the current station is out of range; `"better"` = as soon as another region is received better by `threshold` (behaviour up to 1.3) |
| `memoryButtons` | `true` | Station buttons 1–9 (numpad) |
| `memoryHoldSeconds` | `1.5` | How long to hold a station button to save |
| `stationButtons` | – | The saved station buttons (saved automatically; filled with the stations in range on the first start) |
| `tuning` | `"list"` | FM: `"list"` = Next / Previous step through the stations in range; `"frequency"` = frequency radio like in the old days (see below) |
| `rdsAf` | `false` | Frequency radio: follow the programme to its other frequency when it is lost (RDS AF). `false` = old style: the dial stays where it is |
| `tunedFreq` | – | Last frequency in 0.1 MHz, e.g. `947` (saved automatically) |
| `songTitle` | `true` | Live song title of the station on the navigation screen (and in the dot-matrix display). Read from the stream (ICY `StreamTitle`), switched exactly when the song changes in the audio; for AAC streams it is asked for every 20 s. Stations that send no title show none |
| `tunnelSignal` | `true` | No reception in tunnels (FM: static, DAB+: drop-outs, then silence) |
| `screenPage` | `"navi"` | `"radio"` = radio page permanently on the navigation screen (switched with the `screenPage` key and saved) |

### Overlay (`"overlay": { … }`)

| Key | Default | Meaning |
|---|---|---|
| `style` | `"panel"` | `"panel"` = info panel with logo, `"radio"` = car-radio dot-matrix display (see below) |
| `mode` | `"off"` | `"off"` = no pop-up (default; the in-cab screen still works), `"auto"` = turn the pop-up on (recommended), `"d3d11"` (draw in the game image), `"window"` (transparent window over the game) |
| `position` | `"top-center"` | `top-left`, `top-center`, `top-right`, `bottom-left`, `bottom-center`, `bottom-right` |
| `offsetX`, `offsetY` | `0`, `0.07` | Distance from the screen edge as a fraction of screen height (0.07 = 7 %) |
| `scale` | `1.0` | Size; also scales automatically with your resolution |
| `durationSeconds` | `6` | How long the panel stays visible |
| `alwaysVisible` | `false` | Keep the panel on screen all the time |
| `showLogos` | `true` | Show station logos (off = no logos are downloaded either) |
| `logoCacheMB` | `30` | Maximum size of the `logos` folder (station logos and USB covers) in MB, 5–500. Above it, the logos used longest ago are deleted |
| `urlPrefix` | `"https://radio.scufystudio.de/logo/"` | Where missing logos are downloaded from. Downloaded logos are kept in `realistic-radio\logos\` and never downloaded again. Empty = only logos already in that folder, no network access |
| `textNowPlaying`, `textVolume`, `textStopped`, `textMuted`, `textFavourite`, `textConnecting`, `textUnavailable`, `textPowerOff`, `textRadio`, `textStations` | German texts | All texts on the overlay and the navigation screen. For English, use e.g. `"Now playing:"`, `"Volume"`, `"Radio off"`, `"Muted"`, `"Saved as favourite"`, `"connecting…"`, `"Station unavailable"`, `"Ignition off"`, `"Radio"`, `"Station"` (`textRadio` = label of the source tag, `textStations` = the word in "Station 3 / 12") |

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

### In-cab screen (`"screen": { … }`, experimental)

Shows the station change (logo, name, country, signal) or a volume bar with percentage **on the truck's own infotainment/GPS screen** for a few seconds; afterwards the navigation is visible again. It needs no other plugin: the game renders each cab screen into its own render target, and when the game has finished drawing into the matching one, the plugin draws its card on top of it.

**Radio page:** with the `screenPage` key (default **Insert**) the card stays on the screen permanently instead of disappearing after a few seconds: a radio page with the station or USB track that keeps updating. Press again to bring the navigation back. The game's own menus are not switched (the SDK offers no way to do that); the radio page is drawn over the navigation in the configured area.

Because every truck and mod uses different render targets, the right one must be found once:

1. Set `"enabled": true` and `"probe": true`, start the game and sit in the cab (with the navigation visible).
2. After about 20 s the plugin marks one candidate after another with a magenta checkerboard and a label such as `3/7  1024x600  f28` (every 5 s, also written to the log).
3. When the pattern appears on the screen you want, note width, height and format, then set `"probe": false`, `"width"`, `"height"` and `"format"` accordingly.
4. If the text is upside down, set `"flipY": true`. If the screen texture holds several screens, limit the area with `areaX`, `areaY`, `areaW`, `areaH` (fractions 0–1).

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Turn the in-cab screen display on |
| `width`, `height`, `format` | `1024`, `1024`, `29` | Render target to draw into (from the probe; default = MAN TGX navigation screen) |
| `probe` | `false` | Search mode with test patterns |
| `probeSeconds` | `5` | Time per candidate in search mode |
| `durationSeconds` | `4` | How long the card stays on the screen before the navigation returns |
| `areaX`, `areaY`, `areaW`, `areaH` | `0.1, 0.3, 0.8, 0.4` | Part of the render target to cover |
| `flipY` | `false` | Turn the card upside down – needed on drawing surface 2 (`target` 2), not on surface 1. Choosing the drawing surface in the settings menu sets it accordingly |
| `flipX` | `false` | Mirror horizontally (`flipX` + `flipY` = rotated by 180°) |
| `grid` | `false` | Test mode (easiest in the settings menu): a 10×10 coordinate grid over the whole render target and an orange frame showing where the display is drawn, with its values in the corners. Move the frame with the area sliders until it lies fully on the visible part of the screen. Not saved as on |
| `design` | `"modern"` | Look of the display: `"modern"` (dark, amber), `"ocean"` (navy, light blue), `"sport"` (black, red), `"light"` (white, blue), `"retro"` (green LCD look) |
| `volumeStyle` | `"card"` | How the volume is shown on the navigation screen: `"card"` = large card, `"bar"` = slim bar at the top like on modern infotainment systems ("20%" and a thin level line; the navigation stays visible). Settings menu → Navigation screen → **Volume display** |
| `startLogo` | `true` | Start screen when the ignition is switched on (also with the radio off): the manufacturer's logo fades in on black, like on modern infotainment systems. Logos come from `realistic-radio\truck-logos\` (delivered for DAF, Iveco, MAN, Mercedes-Benz, Scania, Volvo) (see [Truck logos](#truck-logos-start-screen)); without one the brand name and model are shown |
| `startLogoFile` | `""` | Picture to show, relative to `truck-logos\` (e.g. `"mercedes\\gold.png"`); `""` = the first picture for the truck, `"text"` = the name only. Stored in the truck preset (`"logo"`) – easiest in the settings menu, page Trucks |
| `startLogoScale` | `0.55` | Height of the start logo as part of the display area (0.2–1) |
| `target` | `1` | Some trucks render several textures of the screen's size per frame (e.g. navigation and instrument display), so the display could appear twice. `1` = draw into the first one only, `2` = the second, … `0` = into all (behaviour up to 1.3). Easiest in the settings menu with test mode switched on; stored per truck in the preset |
| `scale` | `1.0` | Size of text and pictures on the display (0.6–1.6). The content never gets taller than the area; if it is lower, it is centred |
| `logoSize` | `1.0` | Size of the station logo / cover tile (0.5–1.3) |
| `startLogoSeconds` | `3.5` | How long the start screen stays (1.5–10 s) |

Example (MAN TGX, ETS2 1.61, navigation screen): `"width": 1024, "height": 1024, "format": 29, "flipY": false, "target": 1, "areaX": 0.11, "areaY": 0.24, "areaW": 0.78, "areaH": 0.58`. Other trucks use other render targets.

Independent of the pop-up overlay, which is off by default (`"overlay": { "mode": "auto" }` turns it on in addition). This mode hooks into the game's Direct3D 11 rendering; in probe mode the test pattern can briefly appear on other surfaces too. Untested combinations with other graphics mods are possible.

### Truck presets (`"presets"`, `"activePreset"`, `"presetAuto"`)

Different trucks have different screens – the area that fits a MAN may be cut off in a Scania. Presets store the screen settings per truck.

**Standard presets** are included for the MAN TGX, Scania R and S, Mercedes-Benz New Actros, DAF NGD and Iveco S-Way – these trucks need no setting up. Scania R and S share the cab: the standard is the normal screen; for a Scania with the **smart dashboard** choose the preset **Scania R/S Smart Dashboard** (page Trucks → Load, or link it to the truck). If you change a standard preset, it becomes your own preset with the same name (marked *customised*); deleting it brings back the standard values. Standard presets are not written to the file.

For other trucks, set up a preset once in the settings menu, page **Trucks**:

1. Get into the truck, set up the display on the **Navigation screen** page (test mode, sliders, design).
2. On **Trucks** click **New preset**. The preset is named after the truck and linked to it via the truck id from the telemetry (e.g. `vehicle.scania.s_2016`).
3. Repeat in the other trucks. With **Change automatically per truck** (on by default) the matching preset loads as soon as you get into a truck.

Changes on the **Navigation screen** page are stored in the active preset right away. **Active preset** / **Load** chooses a preset by hand, **Link to this truck** / **Unlink** change the link, **Delete active preset** removes an own preset (click twice; the screen values stay). A truck without a preset keeps the current screen settings and does not change the presets of other trucks.

| Key | Default | Meaning |
|---|---|---|
| `presets` | `[]` | List of presets: `{ "name": "Scania S", "trucks": ["vehicle.scania.s_2016"], "screen": { "width", "height", "format", "areaX", "areaY", "areaW", "areaH", "flipX", "flipY", "design", "durationSeconds" } }` |
| `activePreset` | `""` | Name of the active preset (`""` = none) |
| `presetAuto` | `true` | Load the preset of the truck automatically when you get in |

### Truck logos (start screen)

When the ignition is switched on, the navigation screen shows the manufacturer's logo for a few seconds. Logos for **DAF, Iveco, MAN, Mercedes-Benz (8 colours), Scania and Volvo** come with the plugin: they are inside the DLL and are put into `realistic-radio\truck-logos\` on the first start (each one only once – logos you delete or replace stay that way, see `delivered-logos.txt`). For Renault, mod trucks or other pictures, put your own into the same folder. The plugin creates the folder itself, with a sub folder for every manufacturer of the game and – as soon as you drive one – for every mod brand, so you only have to drop the pictures in. Settings menu → Trucks → **Logo folder** opens the folder of the truck you are in:

- one folder per brand id: `man\`, `scania\`, `volvo\`, `daf\`, `mercedes\`, `iveco\`, `renault\` – with any number of pictures (PNG, JPG, BMP), e.g. `mercedes\gold.png`, `mercedes\silver.png`
- or files directly in `truck-logos\`: `scania.png`, `scania-neon.png`
- **mod trucks:** a folder named after the mod's brand id, or after its truck id for that model only. Both appear in `realistic-radio.log` in the line `Start logo: …`.

Phone wallpapers and photos work as they are: a dark background is made transparent and the picture is cut to the logo automatically. If there are several pictures, choose one in the settings menu → **Trucks** → **Start logo** (automatic / name only / one of the pictures); the choice is stored in the active truck preset, so every truck can have its own. **Preview on the screen** shows it right away.

### Radio memory per truck (`"truckMemory"`)

Settings menu → **Trucks** → **Radio memory per truck** (off by default): every truck then has its own radio like a real one. It keeps the station it played last in every country, its own favourites (**Pause**), volume and mute, the source (radio or which USB / CD) and the track on every USB / CD. Get into another truck and its radio carries on where you left it; a truck you drive for the first time takes over the current radio settings. Trucks are told apart by the truck id from the telemetry. The memory is stored in `realistic-radio\trucks-ets2.json` (`trucks-ats.json` in ATS); favourites without a truck favourite still come from the general favourites file.

| Key | Default | Meaning |
|---|---|---|
| `truckMemory` | `false` | Radio memory per truck on / off |

### USB / CD (`"media": { … }`)

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Allow the USB/CD mode. `false` = the `source` key does nothing |
| `folder` | `"usb"` | Music folder: relative to the `realistic-radio` folder, or a full path such as `"D:\\Music\\Truck"` (double backslashes or `/` in JSON) |
| `folderPerDisc` | `true` | Every sub folder of the music folder is its own USB / CD. `false` = the whole folder is one USB (as in 1.1.0) |
| `label` | `"USB"` | Default type for folders without their own type: `"USB"`, `"CD"`, `"SD"`, `"Cassette"`, `"Bluetooth"`, `"AUX"` or any text. With several of the same, the number is added (`USB 2`) |
| `types` | `{}` | Type per music folder, e.g. `{ "Rock Music": "cd", "Rap Music": "cassette" }` (`usb`, `cd`, `sd`, `cassette`, `bluetooth`, `aux`); easiest in the settings menu |
| `shuffle` | `false` | Shuffle (a new order every time you switch to USB) |
| `repeat` | `"all"` | `"all"` = start again after the last track, `"one"` = repeat the track, `"off"` = stop after the last track |
| `textTrack`, `textPaused`, `textEmpty` | `"Titel"`, `"Pause"`, `"Kein Medientyp erkannt"` | Texts on the screen; in English e.g. `"Track"`, `"Paused"`, `"No media detected"` |

### Truck screen display (`"display": { … }`, optional)

The plugin can open a native window called "Realistic Radio Display" that shows the radio as an infotainment screen. The separate plugin [PrismTextureStreamer](https://github.com/Baldywaldy09/PrismTextureStreamer) (not part of this project) can mirror that window onto the truck's GPS or dashboard screen. It is off by default.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Open the display window |
| `width`, `height` | `1280`, `720` | Size in pixels |
| `offscreen` | `true` | Place the window outside the visible desktop. If it stays black in PrismTextureStreamer, set `false` |
| `title` | `"Realistic Radio Display"` | Window title |

## USB / CD: your own music

1. Copy music files into `plugins\realistic-radio\usb\`. The folder is created automatically on the first start (with a short `LIES-MICH - READ-ME.txt`).
2. **Every folder in it is its own USB stick / CD**, named after the folder:
   ```
   usb\
   ├── Privat Music\        → USB 1
   ├── Rock Music\          → USB 2
   │   ├── Album A\         (part of USB 2)
   │   └── Album B\         (part of USB 2)
   ├── Rap Music\           → USB 3
   └── YouTube Playlist\    → USB 4
   ```
   Files placed directly in `usb\` form one more USB (which then is USB 1). Empty folders are skipped.
3. In the game press **Home**: the radio goes quiet and USB 1 starts with the track last played on it. Every further **Home** goes to the next USB, after the last one back to the radio, on the station you last listened to. To jump between USBs directly, assign `keys.nextDisc` and `keys.previousDisc`.
4. The folder is read again when you switch from the radio to USB, so you can add files and folders while the game is running.

To make the whole folder one single USB as in version 1.1.0 (sub folders = albums), set `"media": { "folderPerDisc": false }`.

| Key in USB/CD mode | Effect |
|---|---|
| → (Right arrow) | Next track |
| ← (Left arrow) | Back to the start of the track; within the first 3 seconds to the previous track (like a CD player) |
| End | Pause / resume (keeps the position) |
| ↑ / ↓, mute | Volume, same as the radio |
| Pause (favourite) | no function, favourites are for stations only |

- **Formats:** MP3, WAV and FLAC are decoded by the plugin itself; M4A, AAC and WMA through Windows Media Foundation (included in Windows 10/11; not in "N" editions without the Media Feature Pack).
- **Order:** USBs and tracks are sorted by name like Windows Explorer ("2" before "10"); within a USB the files of a folder come first, then its sub folders. Up to 5,000 files in total, 8 folder levels.
- **Resume:** every USB remembers its last track. When the game starts, the last chosen source is resumed.
- **Display:** title, artist and album come from the ID3 tags of MP3 files. Other formats and MP3s without tags use the file name: `Artist - Title.mp3` (a leading track number such as `01 - ` is removed); the folder name is used as album.
- **Cover:** a picture embedded in the MP3, or a `cover.jpg`, `folder.jpg` or `front.jpg` (also `.png`) in the track's folder. Embedded covers are written once to `realistic-radio\logos\`. Without a cover the screen shows a picture of the folder's type (CD, USB stick, SD card, cassette, Bluetooth, jack plug).
- **Type per folder:** in the settings menu (USB / CD page) every music folder can be set to USB, CD, SD card, cassette, Bluetooth or AUX; the **Open** button there opens the music folder in Explorer.
- With the ignition off the track pauses and continues at the same position when it comes back on. After switching sources or starting the game, the remembered track starts from the beginning.
- In USB mode there is no static and no automatic station change; reception keeps being calculated in the background, so switching back gives you the right station straight away.

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
- `frequency` is optional: the FM frequency for the frequency radio, e.g. `"frequency": 94.7` (also to correct the frequency of an existing station: same name, same region).

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
| No pop-up overlay | It is off by default: set `"overlay": { "mode": "auto" }`. If it is on but not visible, look for `Overlay:` in the log; `Present hook installed` and `D3D11 renderer ready` mean it is active. If another overlay tool conflicts, try `"mode": "window"`. |
| Menu invisible or navigation-screen display missing with Snowymoon, ReShade or another graphics tool | Realistic Radio draws after the tool's effects (log: `graphics tool detected`). If the tool calls Windows' picture output directly (Snowymoon), Realistic Radio switches to code hooks after 5 s (log: `code hooks on Present … are on`) – from 1.4.7. A menu that cannot be drawn closes again after 2.5 s. If the tool uses **End** for its own menu, move Realistic Radio's Stop key (`keys.stop`) to another key. Still a problem? Send the log (lines starting with `Overlay:`). |
| Overlay hides part of the HUD | Change `position`, `offsetY` or `scale`. |
| No station logo | Logos are downloaded from `radio.scufystudio.de` on first use; without internet, or if the server is down, the panel is shown without a logo and the download is retried after 10 minutes. |
| Keys do nothing | Is the game the active window? Another program may be using the key; pick a different one. |
| A station I expected is missing | It is not assigned to the region you are in. You only receive regions within range. Add it as your own station. |
| "Kein Medientyp erkannt" (no media detected) | There is no file with a supported extension in `realistic-radio\usb\` (or the folder set in `media.folder`). The log shows `USB: 0 file(s) in …` with the exact path. |
| A USB track is skipped | The file could not be played (`Cannot play file:` in the log), e.g. a copy-protected WMA. If no file plays at all, USB mode stops. |
| Wrong title or artist | Check the MP3's ID3 tags (e.g. with Mp3tag). For other formats name the file `Artist - Title`. |

## Known limitations

- **USB/CD mode:** tags are read from MP3 files only (ID3v1/ID3v2); no seeking within a track; Ogg/Opus are not supported.
- **The in-cab screen display needs a modern infotainment screen.** The default values (found in the MAN TGX, ETS2 1.61) worked in all MAN and Scania models and mod trucks tested so far, all of them with a modern infotainment/navigation screen. Trucks with an old-style radio have not been tested; if the display does not appear, use the probe and grid modes to set it up, or turn the screen display off. The SCS Telemetry SDK itself offers no way to write to cab screens; this works by hooking the game's Direct3D 11 rendering.
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
| Truck position, ignition, engine, speed | Read directly from the SCS Telemetry SDK (`truck.world.placement`, `truck.electric.enabled`, `truck.engine.enabled`, `truck.speed`) |
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
                           ├─ RadioManager        station logic ported from main.js, source radio/USB
                           │   ├─ StationDatabase, Favourites, custom stations
                           │   ├─ ReceptionCalculator
                           │   └─ MediaLibrary    USB folder, ID3 tags, covers
                           ├─ Overlay             panel rendering, logo cache
                           │   └─ D3D11Overlay    hook on IDXGISwapChain::Present
                           ├─ DisplayWindow       optional window for PrismTextureStreamer
                           └─ AudioEngine         mixer: stream + static + tuning sound
                               └─ StreamSession   one thread per station or USB track, 4 s buffer, reconnect
```

Design rules:

- `DllMain` does nothing. Threads, audio device and network start only in `scs_telemetry_init`, as the SDK intends.
- Game callbacks only store values and never wait for audio or network.
- Stream threads hold a reference on the DLL, so a slow network call cannot crash the game when the plugin is unloaded.
- The overlay saves the parts of the game's Direct3D state it changes by hand and restores them after its own drawing.

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
| [MinHook](https://github.com/TsudaKageyu/minhook) | git 8af6b4a (Sept. 2026) | BSD 2-Clause | Fallback code hooks for graphics tools such as Snowymoon |
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
│   ├── MediaLibrary.*         USB/CD mode: folder scan, ID3 tags, covers
│   ├── AudioEngine.*          mixer and audio device
│   ├── StreamSource.*         network streams, decoding, reconnect
│   ├── NetReader.h            makes the start of a live stream rewindable
│   ├── Overlay.*, OverlayImage.*, D3D11Overlay.*   in-game overlay
│   ├── DisplayWindow.*        optional display window
│   ├── InputManager.*         keyboard and controller
│   ├── InputBlock.*           keeps the mouse from the game while the menu is open
│   ├── PluginApi.cpp          drawing interface for Trucker Chat (rr_… exports)
│   ├── SettingsMenu.*         settings menu (Ctrl + 0)
│   ├── MenuInput.*            keyboard and mouse for the menu (kept from the game)
│   ├── Config.*, Logger.*     settings and log
│   ├── EmbeddedData.*, resources.rc.in   embedded data files, unpacked on first start
│   └── MiniaudioImpl.cpp      miniaudio compilation unit
├── data/                      station/city data and sounds (embedded into the DLL)
├── server/logo/               station logos for the logo server (radio.scufystudio.de/logo/)
├── sdk/                       SCS Telemetry SDK headers
├── third_party/               miniaudio, nlohmann/json, 5x7 font (Adafruit GFX), MinHook
└── tools/                     data converter, equivalence test
```

## License and credits

Realistic Radio © 2026 **ScufyKing**. Licensed under the **Mozilla Public License 2.0** (MPL-2.0), see [`LICENSE`](LICENSE).

The station lists, city coordinates, the static and tuning sounds and parts of the station selection logic are taken from MPL-2.0 licensed third-party sources. The license requires their copyright notices to be kept; they are listed in [`NOTICE.md`](NOTICE.md), together with the notices for the libraries used. Station logos and radio streams belong to their respective broadcasters.

This project is not affiliated with or endorsed by SCS Software. Euro Truck Simulator 2 and American Truck Simulator are trademarks of SCS Software.

**Contributing:** Issues and pull requests are welcome. Please attach `realistic-radio.log` to bug reports, and report broken station URLs together with the station name and region.
