# Realistic Radio Web-App 1.0.0 (Spotify & Co.)

**Deutsch** · [English](#english)

Zusatz-Plugin für **Realistic Radio 1.7.0 oder neuer**: Eine Webseite als App im Handy. Standard ist der **Spotify Web Player** mit Anmeldung, Playlists und Suche. Es braucht kein Zusatzprogramm und kein Fenster: Die Seite läuft in **Microsoft Edge WebView2**, das bei Windows 10 und 11 schon dabei ist.

1. `realistic-radio-webapp.dll` neben `realistic-radio.dll` in den `plugins`-Ordner kopieren, Spiel starten.
2. Handy öffnen (Umschalt + Einfg) → App **Spotify**. Das Handy wird zum großen Tablet, die Seite lädt beim ersten Öffnen.
3. Mit Maus, Mausrad und Tastatur bedienen, auch zum Einloggen. Strg + V fügt ein. Esc schließt das Handy, ein Klick auf den Strich unten oder Rechtsklick führt zum Startbildschirm.

- Die Anmeldung bleibt gespeichert, in `plugins\realistic-radio\webapp\profile`.
- Die Musik läuft weiter, wenn das Handy zu ist. Der Ton kommt direkt aus WebView2, nicht über den Kabinenklang. Die Lautstärke regelt die Seite oder der Windows-Lautstärkemixer („Microsoft Edge WebView2“).
- Name, Startseite und Zoom stehen in `plugins\realistic-radio\webapp.json`, z. B. für YouTube Music:
  ```json
  {"name": "YT Music", "url": "https://music.youtube.com/", "zoom": 0.9}
  ```
- Fehlt WebView2, zeigt die App einen Hinweis mit dem Download-Link von Microsoft.

---

## English

Add-on for **Realistic Radio 1.7.0 or newer**: a web page as an app on the phone. By default it is the **Spotify web player**, with login, playlists and search. It needs no extra program and no window: the page runs in **Microsoft Edge WebView2**, which comes with Windows 10 / 11.

1. Copy `realistic-radio-webapp.dll` next to `realistic-radio.dll` into the `plugins` folder and start the game.
2. Open the phone (Shift + Insert) → app **Spotify**. The phone turns into a large tablet, and the page loads the first time.
3. Use the mouse, wheel and keyboard, also to log in. Ctrl + V pastes, Esc closes the phone, and a click on the bar at the bottom or a right click goes back to the home screen.

- The login is kept in `plugins\realistic-radio\webapp\profile`.
- The music keeps playing when the phone is closed. The sound comes straight from WebView2 (Windows volume mixer: "Microsoft Edge WebView2").
- Name, start page and zoom are set in `plugins\realistic-radio\webapp.json`.

License: MPL 2.0 · © 2026 ScufyKing · uses the WebView2 SDK headers (Microsoft, BSD) and OpenWebView2Loader (ISC).
