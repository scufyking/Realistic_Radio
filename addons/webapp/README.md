# Realistic Radio Web-App 1.1.0 (Spotify, Browser & Co.)

**Deutsch** · [English](#english)

Zusatz-Plugin für **Realistic Radio 1.7.0 oder neuer**: Webseiten als Apps im Handy. Dabei sind der **Spotify Web Player** mit Anmeldung, Playlists und Suche und ein **Webbrowser** mit Adressleiste. Es braucht kein Zusatzprogramm und kein Fenster: Die Seite läuft in **Microsoft Edge WebView2**, das bei Windows 10 und 11 schon dabei ist.

1. `realistic-radio-webapp.dll` neben `realistic-radio.dll` in den `plugins`-Ordner kopieren, Spiel starten.
2. Handy öffnen (Umschalt + Einfg) → App **Spotify**. Das Handy wird zum großen Tablet, die Seite lädt beim ersten Öffnen.
3. Mit Maus, Mausrad und Tastatur bedienen, auch zum Einloggen. Strg + V fügt ein. Esc schließt das Handy, ein Klick auf den Strich unten oder Rechtsklick führt zum Startbildschirm.

**Browser (neu in 1.1.0):** App **Browser** im Handy. Oben ist eine Leiste mit Zurück, Vor, Neu laden und Startseite und die Adresszeile.

- **Adresse oder Suche:** Klick in die Adresszeile (oder Strg + L), Adresse oder Suchbegriff tippen, Enter. Was keine Adresse ist, wird mit Google gesucht.
- **Weitere Tasten:** Alt + ← / → für Zurück / Vor, F5 zum Neu laden.
- **Spotify läuft weiter:** Beide Apps sind getrennte Seiten, die Musik spielt also weiter, während du surfst.

- Die Anmeldung bleibt gespeichert, in `plugins\realistic-radio\webapp\profile`.
- Die Musik läuft weiter, wenn das Handy zu ist. Der Ton kommt direkt aus WebView2, nicht über den Kabinenklang. Die Lautstärke regelt die Seite oder der Windows-Lautstärkemixer („Microsoft Edge WebView2“).
- Die Apps stehen in `plugins\realistic-radio\webapp.json`: Name, Startseite, Zoom, `browser` (mit Adressleiste) und `search` (Suchmaschine). Bis zu 6 Apps sind möglich, z. B. zusätzlich YouTube Music:
  ```json
  {"apps": [
    {"name": "Spotify", "url": "https://open.spotify.com/", "zoom": 1.0},
    {"name": "Browser", "url": "https://www.google.com/", "browser": true, "search": "https://www.google.com/search?q="},
    {"name": "YT Music", "url": "https://music.youtube.com/", "zoom": 0.9}
  ]}
  ```
  Eine alte Datei aus 1.0 (nur `name`, `url`, `zoom`) gilt weiter, der Browser kommt automatisch dazu.
- Fehlt WebView2, zeigt die App einen Hinweis mit dem Download-Link von Microsoft.

---

## English

Add-on for **Realistic Radio 1.7.0 or newer**: web pages as apps on the phone. It comes with the **Spotify web player** (login, playlists and search) and a **web browser** with an address bar. It needs no extra program and no window: the page runs in **Microsoft Edge WebView2**, which comes with Windows 10 / 11.

1. Copy `realistic-radio-webapp.dll` next to `realistic-radio.dll` into the `plugins` folder and start the game.
2. Open the phone (Shift + Insert) → app **Spotify**. The phone turns into a large tablet, and the page loads the first time.
3. Use the mouse, wheel and keyboard, also to log in. Ctrl + V pastes, Esc closes the phone, and a click on the bar at the bottom or a right click goes back to the home screen.

**Browser (new in 1.1.0):** the app **Browser** on the phone. At the top there is a bar with back, forward, reload and home, and the address field.

- **Address or search:** click into the address field (or Ctrl + L), type an address or a search term, and press Enter. What is not an address is searched with Google.
- **More keys:** Alt + ← / → for back / forward, F5 to reload.
- **Spotify keeps playing:** the two apps are separate pages, so the music keeps playing while you browse.

- The login is kept in `plugins\realistic-radio\webapp\profile`.
- The music keeps playing when the phone is closed. The sound comes straight from WebView2 (Windows volume mixer: "Microsoft Edge WebView2").
- The apps are set in `plugins\realistic-radio\webapp.json`: name, start page, zoom, `browser` (with an address bar) and `search` (search engine), up to 6 apps. An old 1.0 file (only `name`, `url`, `zoom`) still works, and the browser is added to it automatically.

## Changelog
- **1.1.0:** New app **Browser** with an address bar (back, forward, reload, home, search). Several apps in `webapp.json`.
- **1.0.1:** The hidden browser window is only created when the app is opened on the phone; the web page can no longer take the focus from the game.
- **1.0.0:** First version.

License: MPL 2.0 · © 2026 ScufyKing · uses the WebView2 SDK headers (Microsoft, BSD) and OpenWebView2Loader (ISC).
