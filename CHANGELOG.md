# Changelog

**English** · [Deutsch](#changelog-deutsch)

## 1.6.0 – Virtual phone

### New
- **Virtual phone** (settings menu → new page **Phone**, setting `"phone": { … }`, off by default). After the radio starts, a notification "Connected" with the name of your phone appears at the top of the navigation screen – like on a smartphone: a banner with an app icon, the navigation (or the pinned radio page) stays visible below, and a short notification tone plays. The name is your own text ("Phone" by default).
- **Messages from the customer.** When the delivery time of the current job is running out (default: 60 game minutes before the end of the delivery window), the customer sends a message to the phone: sender and text are your own, `{city}`, `{company}`, `{cargo}` and `{time}` are filled in. Once per job; a **Test** button shows the message at once. The time comes from the telemetry (delivery window and game time).
- **The phone itself (Shift + Insert).** A smartphone comes up at the bottom right of the screen, like in GTA, and is worked with mouse and keyboard while it is open:
  - **Messages:** conversations with the customer, the dispatcher, the police and the mailbox.
  - **Phone:** when you take a new job and after every delivery the dispatcher calls – the phone rings (also with the radio off), the navigation screen shows "Incoming call", answer with the phone key and Enter or a click. A Windows voice reads the call, filtered to sound like a phone; the text appears as subtitles. Not answered within 25 s: missed call, the text goes to the mailbox. The call list can play a call again.
  - **Job:** cargo, from → to with companies, time left, distance, arrival time and pay, plus a small map with the cities around, your truck and the destination.
  - **Music:** the music folders set to **Bluetooth** (settings menu → USB / CD) – choose a folder, play / pause, next / previous.
  - **Fines** come as a message from the police with the reason and amount; a cancelled job as a message from the dispatcher with the penalty.
  - Messages and calls are kept in `phone-ets2.json` / `phone-ats.json`. Settings: phone key, dispatcher calls, caller name, voice, fines as messages and a **test call** on the **Phone** page.
- **Per truck.** The phone settings are kept in the truck presets like the screen settings: every truck can have its own phone name, messages or none at all.
- **Text fields in the settings menu.** Click a text field (or press Enter), type, Enter applies, Esc cancels, Ctrl + V pastes – with the keyboard layout of the game (umlauts, @ with AltGr).

### Upgrade notes
- Replace `realistic-radio.dll` only. Old configurations keep working; the phone is off until you switch it on.

## 1.5.0 – New designs, own styles, sound from the cab, works with the game's radio

### New
- **Three new designs for the navigation screen** (settings menu → Navigation screen → **Design**, setting `"screen": { "design": … }`):
  - **Classic truck radio** (`"classic"`): a radio faceplate with CD slot, eject and play buttons, two knobs, a red source button and station buttons 1–6 (the active one lit). The station, country and live song title are on a backlit green LCD in dot-matrix letters, with signal, band and "PRESET 3" in the status row; long song titles scroll. The volume appears in the LCD with a large number and 20 segments.
  - **Smart dashboard** (`"smart"`): source chip, "Now playing" and signal at the top, an icon bar on the left, a glass panel with a large station logo, station name, country, the song title in the accent colour and "station 3 / 41 • button 3".
  - **Station list** (`"presets"`): the station name between arrows, the song title below it, and the station buttons as tiles with logo and name – the current station is framed, page marks show where you are among buttons 1–9. Logos of the buttons are loaded one after another.
- **Own styles.** Every JSON file in `realistic-radio\styles\` becomes a design of its own: it picks a layout (standard, classic, smart, station list), starts from the colours of a built-in design and changes colours, font, glow and optionally sets a background picture. Own styles appear in the settings menu next to the built-in designs; a button opens the folder, and a commented template (`_vorlage-template.json`) is put there. Changes to a style file show up within a few seconds, mistakes are written to the log (`Styles: …`). See README → *Own styles*.
- **Sound from the cab (beta).** Realistic Radio can now play inside the game's own sound system (FMOD) on the game's radio bus instead of straight on the Windows audio device. It then sounds like the game's own radio: cab acoustics, louder with the window open, quieter when you walk outside the truck, and the game's radio volume applies. Settings menu → Radio → **Sound output**: *Windows device* (as before, default) or *The game's cab (beta)*; setting `"audioOutput": "cabin"`. If the game's sound system cannot be used, the sound simply stays on the Windows device. Log: `Game audio: the radio plays in the cab`.
- **Quieter outside the truck.** Walk away from the truck – for example with a walk mod – and the radio no longer sounds like headphones: outside the cab it is muffled by the closed cab, narrower in stereo and fades with the distance until it is silent after 40 m. With an open window more gets through and it carries twice as far. While driving nothing changes. The game's sound system reports whether the camera is in the cab, where it is and how far the windows are open; the distance is measured from the driver's seat. Works with both sound outputs. Settings menu → Radio → **Quieter outside the truck** (on by default) and **Range outside**; settings `"outsideFade"`, `"outsideDistance"`.
- **Works together with the game's built-in radio.** While Realistic Radio plays (radio or USB / CD), the game's own radio is muted, so two radios never play at once. Switch Realistic Radio off, mute or pause it, and the game's radio is audible again. Settings menu → Radio → **Mute the game's radio** (on by default), setting `"muteGameRadio"`.
- **The display dims at night (beta).** With the parking light or low beam on, the radio display on the navigation screen follows the dashboard brightness like a real screen at night, instead of glaring at full brightness; smooth transition. Settings menu → Navigation screen → **Dims with the lights (beta)** (on by default), setting `"screen": { "dimWithLights": true }`.

### Upgrade notes
- Replace `realistic-radio.dll` only. Old configurations keep working; the sound stays on the Windows device until you choose the cab.

## 1.4.7 – Works with Snowymoon

### Fixed
- **Menu and navigation display with Snowymoon.** Snowymoon calls Windows' picture output directly, past the function table Realistic Radio hooks – so the plugin never saw a frame: the settings menu stayed invisible and the radio display on the navigation screen was missing. Now, when a graphics tool is detected and no frame arrives within 5 s, Realistic Radio hooks the code of Present and of the render-target functions itself (MinHook, BSD license, see NOTICE.md) and draws after Snowymoon's effects. Without a graphics tool nothing changes. Log: `code hooks on Present/Present1 are on`, `code hooks on the render-target functions are on`.

### Upgrade notes
- Replace `realistic-radio.dll` only.

## 1.4.6 – Crash fix

### Fixed
- **Crash of 1.4.5 fixed.** The game could crash right after loading. 1.4.5 created a test swap chain at start to find graphics tools; tools such as Snowymoon take over such a swap chain, and when Realistic Radio released it again, the game crashed. This fallback is removed again; drawing works as in 1.4.4.
- **Log:** lists the DLLs loaded from the game folder (`Overlay: DLLs from the game folder: …`), so graphics tools such as Snowymoon can be recognised – also when they load through `dinput8.dll`.

### Upgrade notes
- Replace `realistic-radio.dll` only. Everyone on 1.4.5 should update.

## 1.4.5 – Snowymoon: second way to draw, more details in the log

> **Note:** This fix was not tested with Snowymoon.

### Fixed
- **Snowymoon (second attempt).** Snowymoon puts its own `dxgi.dll` next to the game. Since 1.4.4 Realistic Radio draws at Windows' own swap chain; if no frames arrive there, it now also draws at Snowymoon's swap chain (`Overlay: the graphics tool's swap chain … is hooked as a fallback`). The menu and the navigation display should then appear again – possibly with Snowymoon's effects (e.g. anti-aliasing) over them.
- **More details in the log** to find the cause if it still does not work: which swap chain the first frame came from (`Overlay: first frame …`, with size, format and swap effect) and after 20 s how many frames arrived at Windows' swap chain and at the graphics tool's (`Overlay: status after 20 s …`).

### Upgrade notes
- Replace `realistic-radio.dll` only.

## 1.4.4 – Live song title, modern volume bar, works with Snowymoon and other graphics tools

> **Note:** The fix for Snowymoon was not tested with Snowymoon.

### New
- **Live song title.** The navigation screen shows artist and title of the song the station is playing right now, in its own line under the station; long titles scroll. Most internet radios send the title inside the stream (ICY `StreamTitle`); the plugin takes it out at its exact position in the audio, so the title changes in the second the song changes in what you hear – not when the data arrives. For AAC streams the title is asked for every 20 s. Also in the dot-matrix display of the overlay. Settings menu → Radio → **Live song title** (on by default), setting `"songTitle"`.
- **Volume as a slim bar at the top.** New second style for the volume on the navigation screen, like on modern infotainment systems: a bar at the top edge with "20%" and a thin level line, the navigation stays visible below (over the radio page when it is pinned). Colours follow the chosen design. Settings menu → Navigation screen → **Volume display**: *Large card* (as before, default) or *Bar at the top (modern)*; setting `"screen": { "volumeStyle": "bar" }`.

### Fixed
- **Works with graphics tools such as Snowymoon or ReShade.** These tools put their own `d3d11.dll` / `dxgi.dll` next to the game. With Snowymoon the settings menu stayed invisible (the game seemed frozen until Esc) and the radio display on the navigation screen was missing. Realistic Radio now draws at Windows' own swap chain, after the tool's effects. The log names a detected tool (`graphics tool detected`) and shows after 20 s how many frames reached the overlay.
- **The settings menu no longer blocks the game when it cannot be drawn.** If no picture reaches the overlay within 2.5 s after opening, the menu closes again and the log says why.
- **Steering wheels like Moza can be assigned.** Until now only the first controller with up to 32 buttons was read (an old Windows interface), so with wheels made of several devices (Moza, Fanatec: base, wheel, button box) or with higher button numbers nothing happened on the Keys page. Now every connected controller is read through DirectInput: up to 128 buttons and the hat switch (POV) per controller. A button is stored with its controller (`"buttons": { "next": "MOZA R9 Base/12" }`); the Keys page shows e.g. "MOZA 12". Older button numbers keep working (any controller). The log lists the controllers found.

### Upgrade notes
- Replace `realistic-radio.dll` only. The live song title is on by default (setting `songTitle`).

## 1.4.3 – Runs together with Trucker Chat

### New
- **Drawing interface for other plugins (Trucker Chat).** Realistic Radio can now draw pictures of another plugin in the same game, so that only one plugin hooks Direct3D. Two plugins hooking the same Direct3D functions could crash the game. Trucker Chat 0.1.1 Alpha uses this: `realistic-radio.dll` and `trucker-chat.dll` stay two separate plugins and run side by side; the radio draws its own picture first and then the chat. The interface also tells the other plugin the radio's screen (from the active truck preset) and whether a radio card is showing right now. For developers: `src/PluginApi.cpp` (exports `rr_api_version`, `rr_chat_screen_image` …).

### Changed
- **Typing in the Trucker Chat menu no longer controls the radio** (arrow keys, numpad etc. while the chat menu window is active).
- `Truckers-RealisticRadio.dll` (radio and chat in one DLL) is no longer needed and no longer included. If you use it, replace it with `realistic-radio.dll` and `trucker-chat.dll`.

### Upgrade notes
- Replace `realistic-radio.dll`. With Trucker Chat, also update `trucker-chat.dll` to 0.1.1 Alpha.

## 1.4.2 – No reception in tunnels, truck logos included, radio + Trucker Chat in one DLL, second menu key

### New
- **No reception in tunnels.** Drive into a tunnel and **FM** fades into static, **DAB+** stutters for a moment and goes silent; at the exit the programme comes back (USB / CD keeps playing, the signal bars show 0). Only covered stretches of 200 m and more count. The tunnels come from the databases of **ETS2 Tunnel Radio** by Tal (bohbotoly, MIT license, see NOTICE.md), built from the game's map data: ETS2 1.60 with all map DLCs, and ProMods 2.83 + Middle-East – chosen automatically from `game.log.txt`. Own tunnels for map mods: assign the new key **Mark an own tunnel**, press it at the entry and the exit (saved in `tunnels-own.json`). Settings menu → Reception → **No reception in tunnels**. Setting `"tunnelSignal"`, key `tunnelMark`.
- **Truck logos included.** The start screen now shows real logos right away: the DLL brings logos for **DAF, Iveco, MAN, Mercedes-Benz (8 colours: blue, gold, green, copper, purple, pearl, pink, silver), Scania and Volvo** and puts them into `realistic-radio\truck-logos\` on the first start (in ATS only Volvo). Each logo is written only once – a logo you delete or replace with your own picture stays the way you left it (`truck-logos\delivered-logos.txt`), and own pictures with the same name are never overwritten. For the Mercedes colour: settings menu → Trucks → **Start logo**. Renault and mod trucks still use own pictures or the brand name.
- **Radio + Trucker Chat in one DLL: `Truckers-RealisticRadio.dll`.** For streamers who also use Trucker Chat (Twitch / YouTube live chat on the navigation screen): the new second DLL contains Realistic Radio and Trucker Chat 0.1.1 Alpha. As two separate plugins both hooked Direct3D and did not run reliably together; in one DLL a single hook draws the radio and then the chat. The chat follows the radio's truck presets (drawing surface, area, flip – set up once), waits while the radio shows a card, and typing in the chat menu no longer controls the radio. Install only this DLL (remove `realistic-radio.dll` and `trucker-chat.dll`); the folders `realistic-radio\` and `trucker-chat\` and their settings stay as they are. Chat menu **Ctrl + 9**, radio menu **Ctrl + 0** / **Ctrl + F1**.
- **The settings menu also opens with Ctrl + F1.** For some players **Ctrl + 0** did not open the menu because the game's free camera (developer mode) uses the 0 key. The second key can be changed on the Keys page ("This menu (second key)") or in the file (`keys.menu2`); Ctrl + 0 keeps working.

### Upgrade notes
- Replace `realistic-radio.dll` only. The new keys are added to the settings file automatically.
- With Trucker Chat: use `Truckers-RealisticRadio.dll` instead and remove `realistic-radio.dll` and `trucker-chat.dll`.

## 1.4.1 – Frequency radio and updated truck presets

### New
- **Frequency radio like in the old days.** Settings menu → Radio → **Tuning (FM): Frequency**: every station gets an FM frequency and the radio stays on the frequency you tuned in. **Next / Previous** seek the next frequency with a station, **Shift + → / ←** tune in 0.1 MHz steps, and the screen shows the frequency (e.g. "94,7 MHz"). Driving on, the station fades into static or another station takes the frequency over; at borders two stations can share a frequency and the weaker one breaks through for a few seconds now and then. **Station buttons store the frequency** – in another area a button plays whatever is broadcast there. **RDS AF** (off by default) follows the programme to its other frequency like a modern radio. The frequencies are planned by the plugin (always the same; never twice in one region, neighbouring regions mostly avoid each other's); own ones can be set with `"frequency": 94.7` in the custom stations file. DAB+ keeps the station list. Settings: `"tuning"`, `"rdsAf"`, `"tunedFreq"`, keys `tuneUp` / `tuneDown`.

### Changed
- **Standard presets newly set up in the game:** MAN TGX, Scania and Mercedes-Benz New Actros sit better on the navigation screen (the Mercedes draws into drawing surface 2).
- **Upside down only on drawing surface 2.** All standard presets on surface 1 (MAN, Scania, DAF, Iveco) are no longer flipped; the Mercedes (surface 2) is. Choosing the drawing surface in the settings menu now sets *Flip (up/down)* to match (surface 2 = flipped, surface 1 = not); it can still be changed by hand. Without a preset the display is no longer flipped by default.
- **Scania R and S share one preset** ("Scania R/S", the normal screen), because both have the same cab. For a Scania with the **smart dashboard** there is the new preset **Scania R/S Smart Dashboard**: choose it on the Trucks page (Active preset or Load) or link it to the truck.

### Upgrade notes
- Replace `realistic-radio.dll` only. The new keys (`tuneUp`, `tuneDown`) are added to the settings file automatically. Own presets are kept; a changed copy of the old standard preset "Scania S" stays an own preset and can be deleted on the Trucks page to use the new standard.

## 1.4.0 – Station buttons, FM / DAB+, start screen and staying on your station

### New
- **Station buttons 1–9** on the numpad, like the preset buttons of a car radio: **hold** a button (1.5 s) to save the current station, **tap** it to tune in. A saved station shows "Button 3" on the screen. If the saved station is not in range, the radio takes the same programme from another region, or says that it is out of range. Settings menu → Radio: station buttons on/off and the hold time; the keys can be changed on the Keys page (NumLock must be on). With radio memory per truck every truck has its own station buttons. **Pre-stored:** on the first start the buttons are filled with the nine best received stations where you are (one per programme), like the travel store of a car radio; a truck you drive for the first time with radio memory per truck gets them too. Settings menu → Radio → **Store station buttons again** fills them anew with the stations in range. Settings: `"memoryButtons"`, `"memoryHoldSeconds"`, keys `memory1` … `memory9`.
- **Start screen with the truck logo.** When the ignition is switched on – also with the radio off – the navigation screen shows the manufacturer's logo on black, fading in and out like on modern infotainment systems; then the radio display follows. The pictures go into the new folder `realistic-radio\truck-logos\`, which the plugin creates itself together with a sub folder for every manufacturer of the game and every mod brand you drive: a folder per brand id (`man`, `scania`, `volvo`, `daf`, `mercedes`, `iveco`, `renault`) with any number of pictures, or a folder named after a mod truck's brand id or truck id. Phone wallpapers and photos can be used as they are – a dark background becomes transparent and the picture is cut to the logo automatically. With several pictures for a brand, choose one in the settings menu → Trucks → **Start logo**; the choice is stored in the truck preset. **Preview on the screen** shows it at once, **Logo folder** opens the folder. Without a picture the brand name and model are shown. Settings menu → Navigation screen: start logo on/off and its time. Settings: `"screen": { "startLogo", "startLogoSeconds", "startLogoFile" }`, preset `"logo"`.
- **Band FM / DAB+.** **Shift + Home** (or Settings menu → Radio → Band) switches the band. **FM** works as before: static on weak reception and the tuning sound. **DAB+** reaches further (1.5 × by default, adjustable), has no static and changes stations quickly without the tuning sound; at the edge of coverage the sound drops out briefly, and without reception it is silent – like real digital radio. The screen shows "Radio FM" or "Radio DAB+". Remembered per truck with radio memory per truck. Settings: `"band"`, `"dabRange"`, key `band`.

### Changed
- **The display is drawn only once.** Some trucks render several textures of the screen's size per frame (e.g. navigation and instrument display), and the display – including the start logo – could appear twice. Now it is drawn into the first of them only. If the display is missing in a truck or sits on the wrong screen, choose another one in the settings menu → Navigation screen → **Drawing surface** (1, 2, 3, 4 or all; the hint shows how many the game uses, best with test mode on). The choice is stored per truck in the preset. Setting: `"screen": { "target" }`.
- **Sizes can be adjusted.** Settings menu → Navigation screen: **Size of text and pictures** (60–160 %; the content never gets taller than the display area and is centred if it is lower – previously it sat at the top of tall areas), **Size of station logo / cover** and **Start logo size**. All of them are stored per truck in the preset. Settings: `"screen": { "scale", "logoSize", "startLogoScale" }`.
- **The radio stays on your station.** The radio now changes to a station of another region only when the station you are listening to is out of range – no longer as soon as another region is received a little better. At the edge of the range the signal fades for a few seconds first, so the radio does not switch back and forth when a region comes and goes. The old behaviour can be chosen in the settings menu → Reception → **Change region: on better reception**. Setting: `"switchMode"` (`"lost"` / `"better"`).
- **The same programme from several regions counts as one station**, e.g. SWR1 BW and SWR1 Rheinland-Pfalz, SWR4 BW / Rheinland-Pfalz, NDR 1 Niedersachsen / Welle Nord / Radio MV, MDR Sachsen / Sachsen-Anhalt / Thüringen, ENERGY Stuttgart / München / Berlin / Hamburg / Bremen / NRW / Sachsen, Radio Arabella Bayern / München, or Radio BOB! and Radio Bollerwagen, which are listed in several German regions. Different channels of one region (e.g. RTL 102.5 and RTL 102.5 Disco) stay separate stations. When you leave the area of one, the radio changes to the other without the tuning sound, and Next / Previous lists it only once.

### Upgrade notes
- Replace `realistic-radio.dll` only. Settings, favourites, presets and music are kept; the new keys are added to the settings file automatically.

## 1.3.0 – Truck presets, radio memory per truck, designs and test mode for the navigation screen

### New
- **Designs for the display in the truck.** Settings menu → Navigation screen → **Design**: *Modern* (dark, amber – as before), *Ocean* (navy, light blue), *Sport* (black, red), *Light* (white, blue) and *Retro* (green LCD look with a monospace font). The design applies to station, track, volume and the radio page and changes immediately. Setting: `"screen": { "design": "modern" }`.
- **Test mode for the position of the display.** Settings menu → Navigation screen → **Test mode**: the cab screen shows a coordinate grid and an orange frame where the display will be drawn, with its values in the corners. With the new sliders **Area left (X)**, **Area top (Y)**, **Width** and **Height** (and **Flip** / **Mirror**) the frame moves live until nothing is cut off – useful for trucks where the display was partly cut off. Switch test mode off and the display appears in the new place right away. Test mode is never saved as on.
- **Automatic detection (beta).** Button **Detect automatically** on the same page: the plugin hides its display for a moment, looks once at the picture the game itself draws into the cab screen and sets the area to the part the game uses. Test mode is switched on afterwards so the result can be checked in the truck; the sliders stay available to correct it by hand. Works best in trucks with a modern infotainment screen and visible navigation; if a texture holds several screens (e.g. navigation and digital dashboard), the detected area can be too large.
- **Truck presets.** New page **Trucks** in the settings menu: the settings of the navigation screen (area, flip / mirror, design, display time and the screen's render target) can be saved as presets – as many as you like, e.g. one per truck. The plugin reads the **truck id from the telemetry**: a preset is linked to the truck it was created in, and with **Change automatically per truck** (on by default) it is loaded as soon as you get into that truck. Presets can also be chosen by hand (**Active preset** or **Load**), linked to or unlinked from the current truck and deleted. Changes on the navigation screen page are stored in the active preset right away. If a truck has no preset yet, the current screen settings stay and the preset of the other truck is left unchanged. Settings: `"presets"`, `"activePreset"`, `"presetAuto"`.
- **Standard presets included** for the **MAN TGX**, **Scania S**, **Mercedes-Benz New Actros**, **DAF NGD** and **Iveco S-Way**: in these trucks the display fits the navigation screen right away, without any setting up. A standard preset that you change becomes your own preset with the same name; delete it to get the standard values back.
- **Radio memory per truck.** Settings menu → **Trucks** → **Radio memory per truck** (off by default): every truck keeps its own radio – the last station in every country, its own favourites, volume and mute, the source (radio or which USB / CD) and the track on every USB / CD. Get into another truck and its radio carries on where you left it. Stored in `trucks-ets2.json`; setting `"truckMemory"`.
- **The settings menu can be moved:** drag it by its title bar, e.g. to see the cab screen behind it while using test mode.

### Upgrade notes
- Replace `realistic-radio.dll` only. Settings, favourites, own stations, logos and music are kept; the default design is the look of 1.2. Without presets everything works as before – the current screen settings are used for every truck.

## 1.2.1 – Fix: static in USB / CD mode, "no media detected"

### Fixed
- **Static or the tuning sound could stay audible in USB / CD mode** after switching between radio and USB several times in a row. When a radio station change was still fading in and you switched to USB, the fade-out of the static and the tuning sound was set twice and became extremely slow – the noise then stayed in the music for many seconds, and every further switch made it worse. Fades now always run at the intended speed, and USB / CD mode additionally makes sure static and tuning sound are off.
- The same correction also makes fades on the radio more reliable (e.g. static after a station change).

### Changed
- If there is no music in the `usb` folder, the screen now shows **"Kein Medientyp erkannt"** (no media detected) like a car radio without a medium – with a question-mark picture, where to put the music and how to open the folder from the settings menu. Settings files that still contain the old text "Keine Musik im USB-Ordner" are updated automatically.

### Upgrade notes
- Replace `realistic-radio.dll` only. Settings, favourites, own stations, logos and music are kept.

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

## 1.6.0 – Virtuelles Handy

### Neu
- **Virtuelles Handy** (Einstellungsmenü → neue Seite **Handy**, Einstellung `"phone": { … }`, standardmäßig aus). Nach dem Start des Radios erscheint oben auf dem Navi die Benachrichtigung „Verbunden“ mit dem Namen deines Handys – wie auf dem Smartphone: ein Banner mit App-Symbol, die Navigation (oder die feste Radio-Seite) bleibt darunter sichtbar, dazu ein kurzer Hinweiston. Den Namen legst du selbst fest (Standard „Phone“).
- **Nachrichten vom Kunden.** Läuft die Lieferzeit des aktuellen Auftrags bald ab (Standard: 60 Spielminuten vor Ende der Lieferzeit), schreibt der Kunde dem Handy eine Nachricht: Absender und Text sind frei wählbar, `{city}`, `{company}`, `{cargo}` und `{time}` werden ersetzt. Einmal pro Auftrag; der Knopf **Test** zeigt die Nachricht sofort. Die Zeit kommt aus der Telemetrie (Lieferzeit und Spielzeit).
- **Das Handy selbst (Umschalt + Einfg).** Unten rechts im Bild erscheint ein Smartphone, wie bei GTA, und wird mit Maus und Tastatur bedient, solange es offen ist:
  - **Nachrichten:** Unterhaltungen mit dem Kunden, der Disposition, der Polizei und die Mailbox.
  - **Telefon:** Nimmst du einen neuen Auftrag an und nach jeder Lieferung ruft die Disposition an – das Handy klingelt (auch bei ausgeschaltetem Radio), auf dem Navi steht „Eingehender Anruf“, annehmen mit der Handy-Taste und Enter oder Klick. Eine Windows-Stimme liest den Anruf vor, gefiltert wie am Telefon; der Text läuft als Untertitel mit. Nach 25 s ohne Antwort: verpasster Anruf, der Text landet auf der Mailbox. In der Anrufliste lässt sich ein Anruf nochmal anhören.
  - **Auftrag:** Fracht, von → nach mit Firmen, Restzeit, Entfernung, Ankunftszeit und Lohn, dazu eine kleine Karte mit den Städten ringsum, deinem LKW und dem Ziel.
  - **Musik:** die Musikordner, die auf **Bluetooth** stehen (Einstellungsmenü → USB / CD) – Ordner wählen, Wiedergabe / Pause, nächster / vorheriger Titel.
  - **Bußgelder** kommen als Nachricht von der Polizei mit Grund und Betrag; ein abgebrochener Auftrag als Nachricht von der Disposition mit der Vertragsstrafe.
  - Nachrichten und Anrufe bleiben in `phone-ets2.json` / `phone-ats.json` gespeichert. Einstellungen: Handy-Taste, Anrufe vom Disponenten, Name des Anrufers, Sprachausgabe, Bußgelder als Nachricht und ein **Testanruf** auf der Seite **Handy**.
- **Je LKW.** Die Handy-Einstellungen werden wie die Navi-Einstellungen in den LKW-Presets gespeichert: Jeder LKW kann einen eigenen Handy-Namen, eigene Nachrichten oder gar keine haben.
- **Textfelder im Einstellungsmenü.** Textfeld anklicken (oder Enter), tippen, Enter übernimmt, Esc bricht ab, Strg + V fügt ein – mit dem Tastaturlayout des Spiels (Umlaute, @ mit AltGr).

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Alte Einstellungen funktionieren weiter; das Handy ist aus, bis du es einschaltest.

## 1.5.0 – Neue Designs, eigene Styles, Ton aus der Kabine, Zusammenspiel mit dem Spielradio

### Neu
- **Drei neue Designs für den Navi-Bildschirm** (Einstellungsmenü → Navi-Bildschirm → **Design**, Einstellung `"screen": { "design": … }`):
  - **Klassisches LKW-Radio** (`"classic"`): eine Radioblende mit CD-Schacht, Auswurf- und Play-Taste, zwei Drehknöpfen, roter Quellen-Taste und den Stationstasten 1–6 (die aktive leuchtet). Sender, Land und Songtitel stehen in Punktmatrix-Schrift auf einem beleuchteten grünen LCD, darüber Empfang, Band und „PRESET 3“; lange Songtitel laufen durch. Die Lautstärke erscheint im LCD mit großer Zahl und 20 Segmenten.
  - **Smart-Dashboard** (`"smart"`): oben Quelle, „Jetzt läuft …“ und Empfang, links eine Symbolleiste, dazu ein Glas-Panel mit großem Senderlogo, Sendername, Land, dem Songtitel in der Akzentfarbe und „Sender 3 / 41 • Taste 3“.
  - **Senderliste** (`"presets"`): der Sendername zwischen Pfeilen, darunter der Songtitel und die Stationstasten als Kacheln mit Logo und Namen – der aktuelle Sender ist eingerahmt, Seitenmarken zeigen, wo du in den Tasten 1–9 bist. Die Logos der Tasten werden nacheinander geladen.
- **Eigene Styles.** Jede JSON-Datei im Ordner `realistic-radio\styles\` wird zu einem eigenen Design: Sie wählt einen Aufbau (Standard, Klassisch, Smart, Senderliste), startet mit den Farben eines eingebauten Designs und ändert Farben, Schrift, Leuchten und auf Wunsch ein Hintergrundbild. Eigene Styles erscheinen im Einstellungsmenü neben den eingebauten Designs; ein Knopf öffnet den Ordner, in dem eine kommentierte Vorlage (`_vorlage-template.json`) liegt. Änderungen an einer Style-Datei sind nach ein paar Sekunden zu sehen, Fehler stehen im Log (`Styles: …`). Siehe README → *Eigene Styles*.
- **Ton aus der Kabine (Beta).** Realistic Radio kann jetzt im Soundsystem des Spiels (FMOD) auf dessen Radio-Bus laufen statt direkt über das Windows-Audiogerät. Es klingt dann wie das Radio des Spiels: Kabinen-Akustik, lauter bei offenem Fenster, leiser, wenn du außerhalb des LKWs bist, und die Radio-Lautstärke des Spiels gilt. Einstellungsmenü → Radio → **Ton ausgeben**: *Windows-Gerät* (wie bisher, Standard) oder *Kabine des Spiels (Beta)*; Einstellung `"audioOutput": "cabin"`. Lässt sich das Soundsystem des Spiels nicht nutzen, bleibt der Ton einfach auf dem Windows-Gerät. Log: `Game audio: the radio plays in the cab`.
- **Leiser außerhalb des LKWs.** Gehst du vom LKW weg – zum Beispiel mit einem Lauf-Mod –, klingt das Radio nicht mehr wie Kopfhörer: Draußen dämpft es die geschlossene Kabine, das Stereobild wird schmaler und der Ton wird mit der Entfernung leiser, bis nach 40 m nichts mehr zu hören ist. Bei offenem Fenster kommt mehr durch und es trägt doppelt so weit. Während der Fahrt ändert sich nichts. Das Soundsystem des Spiels meldet, ob die Kamera in der Kabine ist, wo sie ist und wie weit die Fenster offen sind; die Entfernung wird ab dem Fahrersitz gemessen. Funktioniert mit beiden Tonausgaben. Einstellungsmenü → Radio → **Leiser außerhalb des LKWs** (standardmäßig an) und **Hörweite draußen**; Einstellungen `"outsideFade"`, `"outsideDistance"`.
- **Zusammenspiel mit dem eingebauten Radio des Spiels.** Solange Realistic Radio spielt (Radio oder USB / CD), ist das Radio des Spiels stumm – es laufen nie zwei Radios gleichzeitig. Realistic Radio aus, stumm oder pausiert, und das Spielradio ist wieder zu hören. Einstellungsmenü → Radio → **Spielradio stummschalten** (standardmäßig an), Einstellung `"muteGameRadio"`.
- **Die Anzeige dimmt nachts (Beta).** Mit Standlicht oder Abblendlicht folgt die Radioanzeige auf dem Navi der Helligkeit des Armaturenbretts wie ein echter Bildschirm nachts, statt mit voller Helligkeit zu blenden; mit weichem Übergang. Einstellungsmenü → Navi-Bildschirm → **Dimmt mit dem Licht (Beta)** (standardmäßig an), Einstellung `"screen": { "dimWithLights": true }`.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Alte Einstellungen funktionieren weiter; der Ton bleibt auf dem Windows-Gerät, bis du die Kabine wählst.

## 1.4.7 – Läuft mit Snowymoon

### Behoben
- **Menü und Navi-Anzeige mit Snowymoon.** Snowymoon ruft die Bildausgabe von Windows direkt auf, an der Funktionstabelle vorbei, in die sich Realistic Radio einklinkt – das Plugin bekam deshalb kein einziges Bild: Das Einstellungsmenü blieb unsichtbar und die Radioanzeige auf dem Navi fehlte. Wird jetzt ein Grafik-Tool erkannt und kommt 5 s lang kein Bild an, hängt sich Realistic Radio direkt in den Code der Bildausgabe und der Zeichenflächen-Funktionen (MinHook, BSD-Lizenz, siehe NOTICE.md) und zeichnet nach den Effekten von Snowymoon. Ohne Grafik-Tool ändert sich nichts. Log: `code hooks on Present/Present1 are on`, `code hooks on the render-target functions are on`.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen.

## 1.4.6 – Absturz behoben

### Behoben
- **Absturz von 1.4.5 behoben.** Das Spiel konnte direkt nach dem Laden abstürzen. 1.4.5 hat beim Start eine Test-Bildausgabe angelegt, um Grafik-Tools zu finden; Tools wie Snowymoon übernehmen so eine Bildausgabe, und als Realistic Radio sie wieder freigab, stürzte das Spiel ab. Dieser Rückfallweg ist wieder entfernt; gezeichnet wird wie in 1.4.4.
- **Log:** nennt die DLLs, die aus dem Spielordner geladen sind (`Overlay: DLLs from the game folder: …`), damit Grafik-Tools wie Snowymoon erkannt werden – auch wenn sie über `dinput8.dll` laden.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Wer 1.4.5 hat, sollte aktualisieren.

## 1.4.5 – Snowymoon: zweiter Zeichenweg, mehr Angaben im Log

> **Hinweis:** Dieser Fix wurde nicht mit Snowymoon getestet.

### Behoben
- **Snowymoon (zweiter Anlauf).** Snowymoon legt eine eigene `dxgi.dll` neben das Spiel. Seit 1.4.4 zeichnet Realistic Radio an der Bildausgabe von Windows selbst; kommen dort keine Bilder an, zeichnet es jetzt zusätzlich an der Bildausgabe von Snowymoon (`Overlay: the graphics tool's swap chain … is hooked as a fallback`). Menü und Navi-Anzeige sollten dann wieder erscheinen – eventuell mit den Effekten von Snowymoon (z. B. Kantenglättung) darüber.
- **Mehr Angaben im Log**, um die Ursache zu finden, falls es weiter nicht geht: über welche Bildausgabe das erste Bild kam (`Overlay: first frame …`, mit Größe, Format und Swap-Effekt) und nach 20 s, wie viele Bilder bei Windows und beim Grafik-Tool ankamen (`Overlay: status after 20 s …`).

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen.

## 1.4.4 – Songtitel live, moderne Lautstärke-Leiste, läuft mit Snowymoon und anderen Grafik-Tools

> **Hinweis:** Der Fix für Snowymoon wurde nicht mit Snowymoon getestet.

### Neu
- **Songtitel live.** Das Navi zeigt Interpret und Titel des Songs, den der Sender gerade spielt, in einer eigenen Zeile unter dem Sender; lange Titel laufen durch. Die meisten Internetradios schicken den Titel im Stream mit (ICY `StreamTitle`); das Plugin nimmt ihn an seiner genauen Stelle im Ton heraus, so wechselt der Titel in der Sekunde, in der der Song in dem, was du hörst, wechselt – nicht schon, wenn die Daten ankommen. Bei AAC-Streams wird der Titel alle 20 s abgefragt. Auch im Punktmatrix-Display des Overlays. Einstellungsmenü → Radio → **Songtitel live** (standardmäßig an), Einstellung `"songTitle"`.
- **Lautstärke als schmale Leiste oben.** Neuer zweiter Stil für die Lautstärke auf dem Navi, wie bei modernen Infotainment-Systemen: eine Leiste am oberen Rand mit „20%“ und einer dünnen Pegellinie, darunter bleibt die Navigation sichtbar (bei dauerhaft eingeblendeter Radio-Seite liegt sie über ihr). Die Farben folgen dem gewählten Design. Einstellungsmenü → Navi-Bildschirm → **Lautstärke-Anzeige**: *Große Karte* (wie bisher, Standard) oder *Leiste oben (modern)*; Einstellung `"screen": { "volumeStyle": "bar" }`.

### Behoben
- **Läuft mit Grafik-Tools wie Snowymoon oder ReShade.** Diese Tools legen eine eigene `d3d11.dll` / `dxgi.dll` neben das Spiel. Mit Snowymoon blieb das Einstellungsmenü unsichtbar (das Spiel wirkte bis Esc eingefroren) und die Radioanzeige auf dem Navi fehlte. Realistic Radio zeichnet jetzt an der Bildausgabe von Windows selbst, nach den Effekten des Tools. Ein erkanntes Tool steht im Log (`graphics tool detected`), nach 20 s außerdem, wie viele Bilder beim Overlay ankommen.
- **Das Einstellungsmenü blockiert das Spiel nicht mehr, wenn es nicht gezeichnet werden kann.** Kommt 2,5 s nach dem Öffnen kein Bild beim Overlay an, schließt es sich wieder, und im Log steht warum.
- **Lenkräder wie Moza lassen sich belegen.** Bisher wurde nur der erste Controller mit bis zu 32 Knöpfen gelesen (eine alte Windows-Schnittstelle); bei Lenkrädern aus mehreren Geräten (Moza, Fanatec: Basis, Lenkrad, Button-Box) oder höheren Knopfnummern passierte auf der Seite Tasten deshalb nichts. Jetzt wird jeder angeschlossene Controller über DirectInput gelesen: bis zu 128 Knöpfe und der Coolie-Hat (POV) je Controller. Ein Knopf wird mit seinem Controller gespeichert (`"buttons": { "next": "MOZA R9 Base/12" }`); die Seite Tasten zeigt z. B. „MOZA 12“. Ältere Knopfnummern funktionieren weiter (beliebiger Controller). Im Log stehen die gefundenen Controller.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Der Songtitel live ist standardmäßig an (Einstellung `songTitle`).

## 1.4.3 – Läuft zusammen mit Trucker Chat

### Neu
- **Zeichen-Schnittstelle für andere Plugins (Trucker Chat).** Realistic Radio kann jetzt die Bilder eines anderen Plugins im selben Spiel mitzeichnen, damit sich nur ein Plugin an Direct3D hängt. Zwei Plugins an denselben Direct3D-Funktionen konnten das Spiel abstürzen lassen. Trucker Chat 0.1.1 Alpha nutzt das: `realistic-radio.dll` und `trucker-chat.dll` bleiben zwei eigenständige Plugins und laufen nebeneinander; das Radio zeichnet erst sein eigenes Bild, dann den Chat. Die Schnittstelle verrät dem anderen Plugin außerdem den Navi-Bereich des Radios (aus dem aktiven LKW-Preset) und ob gerade eine Radio-Karte zu sehen ist. Für Entwickler: `src/PluginApi.cpp` (Exporte `rr_api_version`, `rr_chat_screen_image` …).

### Geändert
- **Tippen im Trucker-Chat-Menü steuert das Radio nicht mehr** (Pfeiltasten, Nummernblock usw., solange das Chat-Menü aktiv ist).
- `Truckers-RealisticRadio.dll` (Radio und Chat in einer DLL) wird nicht mehr gebraucht und liegt nicht mehr bei. Wer sie nutzt, ersetzt sie durch `realistic-radio.dll` und `trucker-chat.dll`.

### Hinweise zum Update
- `realistic-radio.dll` ersetzen. Mit Trucker Chat auch `trucker-chat.dll` auf 0.1.1 Alpha aktualisieren.

## 1.4.2 – Kein Empfang im Tunnel, Truck-Logos dabei, Radio + Trucker Chat in einer DLL, zweite Menü-Taste

### Neu
- **Kein Empfang im Tunnel.** Fährt man in einen Tunnel, geht **FM** ins Rauschen über, **DAB+** stottert kurz und ist dann still; an der Ausfahrt kommt das Programm zurück (USB / CD spielt weiter, die Empfangsbalken zeigen 0). Es zählen nur überdachte Strecken ab 200 m. Die Tunnel stammen aus den Datenbanken von **ETS2 Tunnel Radio** von Tal (bohbotoly, MIT-Lizenz, siehe NOTICE.md), erstellt aus den Kartendaten des Spiels: ETS2 1.60 mit allen Karten-DLCs sowie ProMods 2.83 + Middle-East – automatisch gewählt anhand von `game.log.txt`. Eigene Tunnel für Map-Mods: die neue Taste **Eigenen Tunnel markieren** belegen, an der Einfahrt und an der Ausfahrt drücken (gespeichert in `tunnels-own.json`). Einstellungsmenü → Empfang → **Kein Empfang im Tunnel**. Einstellung `"tunnelSignal"`, Taste `tunnelMark`.
- **Truck-Logos sind dabei.** Der Startbildschirm zeigt jetzt sofort echte Logos: Die DLL bringt Logos für **DAF, Iveco, MAN, Mercedes-Benz (8 Farben: blau, gold, grün, kupfer, lila, perlmutt, rosa, silber), Scania und Volvo** mit und legt sie beim ersten Start in `realistic-radio\truck-logos\` ab (in ATS nur Volvo). Jedes Logo wird nur einmal geschrieben – ein Logo, das du löschst oder durch ein eigenes Bild ersetzt, bleibt so (`truck-logos\delivered-logos.txt`), und eigene Bilder mit gleichem Namen werden nie überschrieben. Die Mercedes-Farbe wählst du im Einstellungsmenü → LKW → **Start-Logo**. Renault und Mod-LKW nutzen weiter eigene Bilder oder den Herstellernamen.
- **Radio + Trucker Chat in einer DLL: `Truckers-RealisticRadio.dll`.** Für Streamer, die zusätzlich Trucker Chat nutzen (Twitch- / YouTube-Livechat auf dem Navi): Die neue zweite DLL enthält Realistic Radio und Trucker Chat 0.1.1 Alpha. Als zwei getrennte Plugins haben sich beide an Direct3D gehängt und liefen nicht zuverlässig zusammen; in einer DLL zeichnet ein einziger Hook erst das Radio, dann den Chat. Der Chat übernimmt die LKW-Presets des Radios (Zeichenfläche, Bereich, Drehung – nur einmal einstellen), wartet, solange das Radio eine Karte zeigt, und Tippen im Chat-Menü steuert das Radio nicht mehr. Nur diese DLL einsetzen (`realistic-radio.dll` und `trucker-chat.dll` entfernen); die Ordner `realistic-radio\` und `trucker-chat\` und ihre Einstellungen bleiben wie sie sind. Chat-Menü **Strg + 9**, Radio-Menü **Strg + 0** / **Strg + F1**.
- **Das Einstellungsmenü öffnet sich auch mit Strg + F1.** Bei manchen Spielern hat **Strg + 0** das Menü nicht geöffnet, weil die freie Kamera des Spiels (Entwicklermodus) die Taste 0 belegt. Die zweite Taste lässt sich auf der Seite Tasten („Dieses Menü (zweite Taste)“) oder in der Datei (`keys.menu2`) ändern; Strg + 0 funktioniert weiterhin.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Die neuen Tasten werden automatisch in die Einstellungsdatei eingetragen.
- Mit Trucker Chat: stattdessen `Truckers-RealisticRadio.dll` nehmen und `realistic-radio.dll` sowie `trucker-chat.dll` entfernen.

## 1.4.1 – Frequenz-Radio und neue LKW-Presets

### Neu
- **Frequenz-Radio wie früher.** Einstellungsmenü → Radio → **Senderwahl (FM): Frequenz**: Jeder Sender bekommt eine UKW-Frequenz, und das Radio bleibt auf der eingestellten Frequenz. **Weiter / Zurück** suchen die nächste Frequenz mit Sender, **Umschalt + → / ←** stellen in 0,1-MHz-Schritten ein, und das Navi zeigt die Frequenz (z. B. „94,7 MHz“). Beim Weiterfahren verschwindet der Sender im Rauschen oder ein anderer Sender übernimmt die Frequenz; an Grenzen können sich zwei Sender eine Frequenz teilen, und der schwächere kommt ab und zu ein paar Sekunden durch. **Stationstasten speichern die Frequenz** – in einer anderen Gegend spielt eine Taste, was dort gesendet wird. **RDS-AF** (standardmäßig aus) folgt dem Programm auf seine andere Frequenz wie ein modernes Radio. Die Frequenzen plant das Plugin selbst (immer gleich; nie doppelt in einer Region, benachbarte Regionen meiden sich meistens); eigene lassen sich mit `"frequency": 94.7` in der Datei für eigene Sender eintragen. DAB+ behält die Senderliste. Einstellungen: `"tuning"`, `"rdsAf"`, `"tunedFreq"`, Tasten `tuneUp` / `tuneDown`.

### Geändert
- **Standard-Presets im Spiel neu eingestellt:** MAN TGX, Scania und Mercedes-Benz New Actros sitzen besser auf dem Navi (der Mercedes zeichnet in Zeichenfläche 2).
- **Umdrehen nur bei Zeichenfläche 2.** Alle Standard-Presets auf Fläche 1 (MAN, Scania, DAF, Iveco) werden nicht mehr umgedreht, der Mercedes (Fläche 2) schon. Wählt man im Einstellungsmenü die Zeichenfläche, wird *Umdrehen (oben/unten)* jetzt passend gesetzt (Fläche 2 = umgedreht, Fläche 1 = nicht); von Hand ändern geht weiterhin. Ohne Preset wird die Anzeige standardmäßig nicht mehr umgedreht.
- **Scania R und S teilen sich ein Preset** („Scania R/S“, der normale Bildschirm), weil beide dasselbe Cockpit haben. Für einen Scania mit **Smart Dashboard** gibt es das neue Preset **Scania R/S Smart Dashboard**: auf der Seite LKW auswählen (Aktives Preset oder Laden) oder mit dem LKW verknüpfen.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Die neuen Tasten (`tuneUp`, `tuneDown`) werden automatisch in die Einstellungsdatei eingetragen. Eigene Presets bleiben erhalten; eine geänderte Kopie des alten Standard-Presets „Scania S“ bleibt ein eigenes Preset und lässt sich auf der Seite LKW löschen, um den neuen Standard zu nutzen.

## 1.4.0 – Stationstasten, FM / DAB+, Startbildschirm und das Radio bleibt auf deinem Sender

### Neu
- **Stationstasten 1–9** auf dem Nummernblock, wie im Autoradio: Taste **gedrückt halten** (1,5 s) speichert den aktuellen Sender, **kurz drücken** stellt ihn ein. Bei einem gespeicherten Sender zeigt das Navi „Taste 3“. Ist der gespeicherte Sender nicht in Reichweite, nimmt das Radio dasselbe Programm aus einer anderen Region oder meldet „nicht in Reichweite“. Einstellungsmenü → Radio: Stationstasten an/aus und Haltezeit; die Tasten lassen sich auf der Seite Tasten ändern (Num-Lock muss an sein). Mit Radio-Speicher je LKW hat jeder LKW eigene Stationstasten. **Vorbelegt:** Beim ersten Start sind die Tasten mit den neun am besten empfangenen Sendern dort belegt, wo du gerade bist (je Programm einer) – wie die Travelstore-Funktion im Autoradio; mit Radio-Speicher je LKW bekommt auch jeder neue LKW eine Belegung. Einstellungsmenü → Radio → **Stationstasten neu belegen** belegt sie neu mit den Sendern in Reichweite. Einstellungen: `"memoryButtons"`, `"memoryHoldSeconds"`, Tasten `memory1` … `memory9`.
- **Startbildschirm mit Truck-Logo.** Beim Einschalten der Zündung – auch bei ausgeschaltetem Radio – zeigt das Navi das Logo des Herstellers auf Schwarz, mit Ein- und Ausblenden wie bei modernen Infotainment-Systemen; danach folgt die Radio-Anzeige. Die Bilder kommen in den neuen Ordner `realistic-radio\truck-logos\`, den das Plugin selbst anlegt – mit einem Unterordner für jeden Hersteller im Spiel und für jede Mod-Marke, die man fährt: ein Ordner pro Hersteller-ID (`man`, `scania`, `volvo`, `daf`, `mercedes`, `iveco`, `renault`) mit beliebig vielen Bildern oder ein Ordner mit der Hersteller-ID bzw. LKW-ID eines Mod-LKWs. Handy-Hintergrundbilder und Fotos lassen sich direkt verwenden – ein dunkler Hintergrund wird durchsichtig und das Bild automatisch auf das Logo zugeschnitten. Gibt es mehrere Bilder für einen Hersteller, wählt man im Einstellungsmenü → LKW → **Start-Logo** eins aus; die Wahl wird im LKW-Preset gespeichert. **Vorschau auf dem Navi** zeigt es sofort, **Logo-Ordner** öffnet den Ordner. Ohne Bild werden Herstellername und Modell gezeigt. Einstellungsmenü → Navi-Bildschirm: Start-Logo an/aus und Dauer. Einstellungen: `"screen": { "startLogo", "startLogoSeconds", "startLogoFile" }`, Preset `"logo"`.
- **Band FM / DAB+.** **Umschalt + Pos1** (oder Einstellungsmenü → Radio → Band) wechselt das Band. **FM** funktioniert wie bisher: Rauschen bei schwachem Empfang und Umschaltgeräusch. **DAB+** reicht weiter (standardmäßig 1,5-mal so weit, einstellbar), rauscht nicht und wechselt Sender schnell ohne Umschaltgeräusch; am Rand des Empfangsgebiets setzt der Ton kurz aus, ohne Empfang ist es still – wie beim echten Digitalradio. Das Navi zeigt „Radio FM“ bzw. „Radio DAB+“. Mit Radio-Speicher je LKW merkt sich jeder LKW sein Band. Einstellungen: `"band"`, `"dabRange"`, Taste `band`.

### Geändert
- **Die Anzeige wird nur noch einmal gezeichnet.** Manche LKW zeichnen pro Bild mehrere Flächen in der Größe des Bildschirms (z. B. Navi und Kombiinstrument), dann konnte die Anzeige – auch das Start-Logo – doppelt erscheinen. Jetzt wird nur noch in die erste davon gezeichnet. Fehlt die Anzeige in einem LKW oder sitzt sie auf dem falschen Bildschirm, wählt man im Einstellungsmenü → Navi-Bildschirm → **Zeichenfläche** eine andere (1, 2, 3, 4 oder alle; der Hinweis zeigt, wie viele das Spiel benutzt, am besten bei eingeschaltetem Testmodus). Die Wahl wird je LKW im Preset gespeichert. Einstellung: `"screen": { "target" }`.
- **Größen lassen sich anpassen.** Einstellungsmenü → Navi-Bildschirm: **Größe Schrift und Bilder** (60–160 %; der Inhalt wird nie höher als der Anzeigebereich und mittig gesetzt, wenn er niedriger ist – bisher saß er bei hohen Bereichen oben), **Größe Senderlogo / Cover** und **Größe Start-Logo**. Alles wird je LKW im Preset gespeichert. Einstellungen: `"screen": { "scale", "logoSize", "startLogoScale" }`.
- **Das Radio bleibt auf deinem Sender.** Das Radio wechselt jetzt erst dann auf einen Sender einer anderen Region, wenn der gehörte Sender nicht mehr empfangen wird – nicht mehr schon, sobald eine andere Region etwas besser zu empfangen ist. Am Rand des Empfangsgebiets wird das Signal erst ein paar Sekunden schwächer, damit das Radio nicht hin und her springt, wenn eine Region kommt und geht. Das alte Verhalten lässt sich im Einstellungsmenü → Empfang → **Region wechseln: bei besserem Empfang** wählen. Einstellung: `"switchMode"` (`"lost"` / `"better"`).
- **Dasselbe Programm aus mehreren Regionen gilt als ein Sender**, z. B. SWR1 BW und SWR1 Rheinland-Pfalz, SWR4 BW / Rheinland-Pfalz, NDR 1 Niedersachsen / Welle Nord / Radio MV, MDR Sachsen / Sachsen-Anhalt / Thüringen, ENERGY Stuttgart / München / Berlin / Hamburg / Bremen / NRW / Sachsen, Radio Arabella Bayern / München oder Radio BOB! und Radio Bollerwagen, die in mehreren deutschen Regionen gelistet sind. Verschiedene Kanäle einer Region (z. B. RTL 102.5 und RTL 102.5 Disco) bleiben eigene Sender. Verlässt man das Gebiet des einen, wechselt das Radio ohne Umschaltgeräusch auf den anderen, und Weiter / Zurück führt ihn nur einmal.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Einstellungen, Favoriten, Presets und Musik bleiben erhalten; die neuen Tasten werden automatisch in die Einstellungsdatei eingetragen.

## 1.3.0 – LKW-Presets, Radio-Speicher je LKW, Designs und Testmodus für den Navi-Bildschirm

### Neu
- **Designs für die Anzeige im LKW.** Einstellungsmenü → Navi-Bildschirm → **Design**: *Modern* (dunkel, Amber – wie bisher), *Ozean* (Marineblau, Hellblau), *Sport* (Schwarz, Rot), *Hell* (Weiß, Blau) und *Retro* (grüne LCD-Optik mit Monospace-Schrift). Das Design gilt für Sender, Titel, Lautstärke und die Radio-Seite und wechselt sofort. Einstellung: `"screen": { "design": "modern" }`.
- **Testmodus für die Position der Anzeige.** Einstellungsmenü → Navi-Bildschirm → **Testmodus**: Auf dem Bildschirm im Cockpit erscheinen ein Koordinatengitter und ein orangefarbener Rahmen, wohin die Anzeige gezeichnet wird, mit den Werten in den Ecken. Mit den neuen Reglern **Bereich links (X)**, **Bereich oben (Y)**, **Breite** und **Höhe** (sowie **Umdrehen** / **Spiegeln**) verschiebt sich der Rahmen live, bis nichts mehr abgeschnitten ist – praktisch für LKW, in denen die Anzeige teilweise abgeschnitten war. Testmodus aus, und die Anzeige erscheint sofort an der neuen Stelle. Der Testmodus wird nie als „an“ gespeichert.
- **Automatische Erkennung (Beta).** Knopf **Automatisch erkennen** auf derselben Seite: Das Plugin blendet seine Anzeige kurz aus, schaut sich einmal das Bild an, das das Spiel selbst in den Cockpit-Bildschirm zeichnet, und stellt den Bereich auf den Teil, den das Spiel benutzt. Danach ist der Testmodus an, damit man das Ergebnis im LKW prüfen kann; die Regler bleiben zum Nachstellen von Hand. Klappt am besten in LKW mit modernem Infotainment-Bildschirm und sichtbarer Navigation; liegen mehrere Bildschirme in einer Fläche (z. B. Navi und digitales Armaturenbrett), kann der erkannte Bereich zu groß sein.
- **LKW-Presets.** Neue Seite **LKW** im Einstellungsmenü: Die Einstellungen des Navi-Bildschirms (Bereich, Umdrehen / Spiegeln, Design, Anzeigedauer und die Render-Fläche des Bildschirms) lassen sich als Presets speichern – beliebig viele, z. B. eins pro LKW. Das Plugin liest die **LKW-ID aus der Telemetrie**: Ein Preset wird mit dem LKW verknüpft, in dem es angelegt wurde, und mit **Automatisch je LKW wechseln** (standardmäßig an) wird es geladen, sobald man in diesen LKW steigt. Presets lassen sich auch von Hand wählen (**Aktives Preset** oder **Laden**), mit dem aktuellen LKW verknüpfen oder davon lösen und löschen. Änderungen auf der Seite Navi-Bildschirm landen sofort im aktiven Preset. Hat ein LKW noch kein Preset, bleiben die aktuellen Navi-Einstellungen, und das Preset des anderen LKWs wird nicht verändert. Einstellungen: `"presets"`, `"activePreset"`, `"presetAuto"`.
- **Standard-Presets dabei** für **MAN TGX**, **Scania S**, **Mercedes-Benz New Actros**, **DAF NGD** und **Iveco S-Way**: In diesen LKW sitzt die Anzeige sofort passend auf dem Navi, ohne etwas einzustellen. Ändert man ein Standard-Preset, wird daraus ein eigenes Preset mit demselben Namen; löscht man es, sind die Standardwerte wieder da.
- **Radio-Speicher je LKW.** Einstellungsmenü → **LKW** → **Radio-Speicher je LKW** (standardmäßig aus): Jeder LKW behält sein eigenes Radio – den letzten Sender in jedem Land, eigene Favoriten, Lautstärke und Stumm, die Quelle (Radio oder welcher USB / welche CD) und den Titel auf jedem USB / jeder CD. Steigt man in einen anderen LKW, macht dessen Radio dort weiter, wo man es verlassen hat. Gespeichert in `trucks-ets2.json`; Einstellung `"truckMemory"`.
- **Das Einstellungsmenü lässt sich verschieben:** an der Titelleiste ziehen, z. B. um im Testmodus den Bildschirm im Cockpit dahinter zu sehen.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Einstellungen, Favoriten, eigene Sender, Logos und Musik bleiben erhalten; das Standard-Design entspricht dem Aussehen von 1.2. Ohne Presets funktioniert alles wie bisher – die aktuellen Navi-Einstellungen gelten für jeden LKW.

## 1.2.1 – Fehlerbehebung: Rauschen im USB / CD-Modus, „Kein Medientyp erkannt“

### Behoben
- **Im USB / CD-Modus konnte Rauschen oder das Umschaltgeräusch hörbar bleiben**, wenn man mehrmals hintereinander zwischen Radio und USB gewechselt hat. Lief beim Wechsel auf USB gerade noch das Einblenden eines Senderwechsels, wurde das Ausblenden von Rauschen und Umschaltgeräusch doppelt gesetzt und dadurch extrem langsam – das Rauschen lag dann viele Sekunden über der Musik, und jeder weitere Wechsel machte es schlimmer. Ein- und Ausblendungen laufen jetzt immer im vorgesehenen Tempo, und der USB / CD-Modus sorgt zusätzlich dafür, dass Rauschen und Umschaltgeräusch aus sind.
- Dieselbe Korrektur macht auch die Übergänge beim Radio zuverlässiger (z. B. das Rauschen nach einem Senderwechsel).

### Geändert
- Liegt keine Musik im Ordner `usb`, zeigt das Navi jetzt **„Kein Medientyp erkannt“** wie ein Autoradio ohne eingelegtes Medium – mit Fragezeichen-Bild, dem Hinweis, wo die Musik hingehört, und wie man den Ordner über das Einstellungsmenü öffnet. Einstellungsdateien mit dem alten Text „Keine Musik im USB-Ordner“ werden automatisch angepasst.

### Hinweise zum Update
- Nur `realistic-radio.dll` ersetzen. Einstellungen, Favoriten, eigene Sender, Logos und Musik bleiben erhalten.

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
