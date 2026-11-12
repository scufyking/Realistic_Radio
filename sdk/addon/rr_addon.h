/* Realistic Radio - interface for add-ons (apps on the virtual phone), version 2 (Realistic Radio 1.7.0)
 * Copyright (c) 2026 ScufyKing - this header may be used in add-ons of any licence (MIT-style: keep this line).
 *
 * An add-on is its own DLL in the game's plugins folder, i.e. an SCS telemetry plugin (scs_telemetry_init /
 * scs_telemetry_shutdown). It may use the telemetry itself. It talks to Realistic Radio through the functions below,
 * which realistic-radio.dll exports. Call rr_addon_load() from your own thread (never from DllMain or from
 * scs_telemetry_init directly: Realistic Radio may be loaded after your add-on - just try again every second).
 *
 * Texts are UTF-8, data is JSON. All functions are thread-safe. See README.md next to this file (German / English)
 * and the example add-on in sdk/addon/example/.
 */
#ifndef RR_ADDON_H
#define RR_ADDON_H
#include <windows.h>
#include <psapi.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rr_addon_api {
    int (*api_version)(void);   /* 2 or higher */

    /* --- the app -------------------------------------------------------------------------------------------------
     * Register (or update) your app, then call it again about every second: an app not heard from for 5 seconds
     * disappears from the phone. Any other call with your id also counts.
     * {"id": "jobs", "name": "Aufträge+", "name_en": "Jobs+", "mode": "list", "color": "#FF9F0A", "icon": "C:\\...\\icon.png"}
     * mode: "list"   - you describe screens as JSON (app_ui), the phone draws them           (easy)
     *       "canvas" - you draw the screen (app_frame, portrait phone screen)                (free)
     *       "tablet" - like canvas, the phone turns into a large landscape tablet           (e.g. web pages)
     * icon: optional PNG/JPG (square), else your colour with the first letter of the name. Returns 1 when accepted. */
    int (*app_register)(const char* appJson);

    /* list mode: the screen to show. Send it again whenever something changes (only a changed text is redrawn).
     * {"screen": "start", "title": "Aufträge+", "back": false, "hint": "small line at the bottom", "select": 2,
     *  "items": [ {"type": "section", "title": "WARE"},
     *             {"type": "row", "id": "cargo", "title": "Ware", "value": "Möbel", "text": "grey second line", "arrow": true},
     *             {"type": "search", "id": "q", "placeholder": "Stadt suchen …", "text": ""},
     *             {"type": "text", "title": "optional bold line", "text": "a paragraph that wraps"},
     *             {"type": "button", "id": "go", "title": "Auftrag anlegen", "enabled": true} ]}
     * screen: your name for this screen - a new name puts the selection to the top and takes "text" of the search
     *         field; with the same name the phone keeps what the player is typing.
     * back:   true = the back key / back arrow sends {"type": "back"} to you (your own sub screens);
     *         false = back goes to the phone's home screen. */
    void (*app_ui)(const char* id, const char* uiJson);

    /* canvas / tablet mode: the size your picture should have (pixels), returns 1 while your app is open */
    int (*app_area)(const char* id, int* w, int* h);
    /* canvas / tablet mode: your picture, premultiplied BGRA (0xAARRGGBB in memory order B,G,R,A), top-down, w * h values */
    void (*app_frame)(const char* id, const uint32_t* px, int w, int h);

    /* events since the last call as a JSON array, written to buf (UTF-8, 0-terminated). Returns the number of bytes,
     * 0 = no events, a negative number = buf too small (-size needed; the events stay).
     *   {"type": "open"} / {"type": "close"}                       your app was opened / left on the phone
     *   list:   {"type": "click", "id": "go"}                      a row or button was clicked (or Enter)
     *           {"type": "text", "id": "q", "text": "ber"}         the player typed into the search field
     *           {"type": "back"}                                   back (only with "back": true)
     *   canvas: {"type": "down" | "up", "button": 0, "x": 10, "y": 20}   mouse button (0 left, 1 right, 2 middle); x/y in your picture
     *           {"type": "move", "x": 10, "y": 20}  {"type": "wheel", "delta": 120, "x": .., "y": ..}
     *           {"type": "key", "vk": 13, "ctrl": false, "shift": false, "alt": false}   {"type": "char", "text": "a"}   */
    int (*app_events)(const char* id, char* buf, int size);

    /* --- information ----------------------------------------------------------------------------------------------
     * The state of the game as JSON (updated twice a second), returns bytes / 0 / -needed like app_events:
     * {"api": 2, "version": "1.7.0", "game": "ets2", "language": "de", "gameTime": 9123, "x": .., "z": ..,
     *  "country": "germany", "nearestCity": "berlin", "powered": true, "phoneOpen": false,
     *  "job": {"active": true, "cargo": "...", "sourceCity": "...", "sourceCityId": "...", "sourceCompany": "...",
     *          "city": "...", "cityId": "...", "company": "...", "deliveryTime": 9800, "income": 12345}}
     * language: the language of the radio's menu ("de", "en", "ru" or the code of a language file) - use it for your texts. */
    int (*state)(char* buf, int size);
    /* a city by its game name ("berlin"): {"token": "berlin", "name": "Berlin", "country": "germany", "x": .., "z": ..}; 0 = unknown */
    int (*city)(const char* token, char* buf, int size);

    /* --- output ---------------------------------------------------------------------------------------------------
     * A notification on the navigation screen / phone, optionally also kept in the phone's messages:
     * {"app": "Aufträge+", "title": "Auftrag bereit", "text": "Drücke F9", "icon": "message", "message": true, "from": "Aufträge+"}
     * icon: "message" | "phone" | "call" */
    void (*notify)(const char* noticeJson);
    /* a line in realistic-radio.log (UTF-8) - write "<your add-on>: ..." */
    void (*log)(const char* line);

    /* --- music (optional) -----------------------------------------------------------------------------------------
     * An add-on that plays music can tell the radio about it: the title then shows on the navigation screen and the
     * radio fades out while it plays (settings menu -> Phone). status: 1 stopped, 2 paused, 3 playing; times in ms (-1 unknown) */
    void (*media_register)(const wchar_t* addonName);
    void (*media_set)(const wchar_t* app, const wchar_t* title, const wchar_t* artist, const wchar_t* album, int status, long long posMs, long long lenMs, int caps);
    void (*media_cover)(const uint32_t* px, int w, int h);
    void (*media_none)(void);
} rr_addon_api;

/* Finds Realistic Radio in this game process and fills `api`. Returns 1 when Realistic Radio 1.7.0 or newer is there. */
static int rr_addon_load(rr_addon_api* api) {
    HMODULE mods[512];
    DWORD need = 0, i;
    ZeroMemory(api, sizeof(*api));
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return 0;
    for (i = 0; i < need / sizeof(HMODULE) && i < 512; ++i) {
        FARPROC reg = GetProcAddress(mods[i], "rr_app_register");
        if (!reg) continue;
#define RR_GET(field, name) *(FARPROC*)&api->field = GetProcAddress(mods[i], name)
        RR_GET(api_version, "rr_api_version");
        RR_GET(app_register, "rr_app_register");
        RR_GET(app_ui, "rr_app_ui");
        RR_GET(app_area, "rr_app_area");
        RR_GET(app_frame, "rr_app_frame");
        RR_GET(app_events, "rr_app_events");
        RR_GET(state, "rr_state");
        RR_GET(city, "rr_city");
        RR_GET(notify, "rr_notify");
        RR_GET(log, "rr_log");
        RR_GET(media_register, "rr_media_register");
        RR_GET(media_set, "rr_media_set");
        RR_GET(media_cover, "rr_media_cover");
        RR_GET(media_none, "rr_media_none");
#undef RR_GET
        return api->api_version && api->api_version() >= 2 && api->app_register && api->app_ui && api->app_events;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
