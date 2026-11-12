# Language switching across display surfaces

German remains the default. Selecting DE/EN/RU changes known UI text at display time, rather than rewriting translated values into the user's configuration.

Previously `RadioManager` copied `overlay.textNowPlaying` and related settings straight into the popup/navigation view. Existing configurations normally contain German defaults such as `Jetzt läuft:` even when `menuLanguage` is `ru`. Those fields bypassed the Russian dictionary. Some renderers also contained direct labels such as `Stumm`, `Volume`, `PRESET`, and the phone home-screen app names.

`Localization::Configured` resolves the built-in language forms of a particular setting. It uses that setting's default as context: `Sender` as an overlay station-count label is distinct from `Sender` as the English name of a phone setting. If a configured value matches DE/EN/RU built-in forms of this setting, it is displayed in the selected language. Otherwise it remains verbatim, including when a custom caption matches a different UI label. The configuration is not modified.

Covered paths:

- Popup radio headline, volume/mute, power, connecting/unavailable and favourites.
- Navigation radio cards, station-count labels, LCD source and preset captions, volume fallback text.
- Separate display window mute status.
- USB track, pause, empty-media labels and built-in media type names.
- Phone app labels, weekdays, built-in device/sender/dispatcher names and default customer-message template.
- Known cached menu messages, controller button labels and refreshing visible radio/phone UI after a language change.

Names, song metadata, folders, custom captions, custom message templates and device names are not globally translated at drawing time. Existing stored phone messages remain historical content in the language in which they were generated. Standard literal values are recognized even when an older configuration already stores their translated form.

The native `localization-probe` verifies repeated RU → DE → EN switches using actual `RadioManager::CurrentInfo` and phone/menu views; it checks all ten configurable overlay captions, USB/phone defaults, weekday text, custom-text preservation, restored German defaults and switching previously stored Russian standard text back to English. It renders popup, navigation, classic-radio and phone fixtures for all three languages. The default menu test and user-text/placeholder tests remain included.

HLS transport, byte stream, decoder and audio mixer code are unchanged by this update. In-game visual verification is still required after loading the new DLL.
