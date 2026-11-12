// Example add-on for Realistic Radio: a small app on the phone ("list" mode) - a counter, a search field and a button
// that sends a notification. Build it as a DLL (64 bit) named e.g. realistic-radio-hello.dll and put it into the game's
// plugins folder next to realistic-radio.dll. Needs the SCS SDK headers (scssdk_telemetry.h) and sdk/addon/rr_addon.h.
//   MinGW / clang:  x86_64-w64-mingw32-g++ -shared -O2 -std=c++17 HelloApp.cpp -I<scs sdk>/include -I.. -o realistic-radio-hello.dll -static
//   MSVC:           cl /LD /O2 /std:c++17 HelloApp.cpp /I<scs sdk>\include /I.. /Fe:realistic-radio-hello.dll
#include "rr_addon.h"
#include <scssdk_telemetry.h>
#include <atomic>
#include <string>
#include <vector>

static rr_addon_api rr;
static std::atomic<bool> quit{false};
static HANDLE thread = nullptr;
static const char* kId = "hello";

// very small JSON helpers (use a real JSON library in bigger add-ons, e.g. nlohmann/json)
static std::string Esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
static std::string Field(const std::string& ev, const std::string& key) {   // "key":"value" from one event
    size_t p = ev.find("\"" + key + "\":\"");
    if (p == std::string::npos) return "";
    p += key.size() + 4;
    std::string v;
    for (; p < ev.size() && ev[p] != '"'; ++p) { if (ev[p] == '\\' && p + 1 < ev.size()) ++p; v += ev[p]; }
    return v;
}

static DWORD WINAPI Worker(LPVOID) {
    while (!quit && !rr_addon_load(&rr)) Sleep(1000);   // Realistic Radio may come later
    if (quit) return 0;
    rr.log("Hello add-on: started");
    int count = 0;
    std::string typed;
    DWORD lastReg = 0;
    bool dirty = true;
    while (!quit) {
        if (GetTickCount() - lastReg > 1000) {   // keep the app on the phone
            lastReg = GetTickCount();
            rr.app_register(R"({"id":"hello","name":"Hallo","name_en":"Hello","mode":"list","color":"#34C759"})");
        }
        char buf[8192];
        int n = rr.app_events(kId, buf, sizeof buf);
        if (n > 0) {   // a JSON array: look at each event (here simply by searching the text)
            std::string evs(buf, n);
            if (evs.find("\"id\":\"plus\"") != std::string::npos) { ++count; dirty = true; }
            if (evs.find("\"id\":\"send\"") != std::string::npos)
                rr.notify((R"({"app":"Hallo","title":"Hallo vom Add-on","text":"Zähler: )" + std::to_string(count) + R"(","message":true})").c_str());
            if (evs.find("\"type\":\"text\"") != std::string::npos) { typed = Field(evs.substr(evs.rfind("\"type\":\"text\"")), "text"); dirty = true; }
            if (evs.find("\"type\":\"open\"") != std::string::npos) dirty = true;
        }
        if (dirty) {
            dirty = false;
            std::string ui = R"({"screen":"main","title":"Hallo","hint":"Beispiel-Add-on","items":[)"
                             R"({"type":"section","title":"ZÄHLER"},)"
                             R"({"type":"row","id":"plus","title":"Plus eins","value":")" + std::to_string(count) + R"(","arrow":true},)"
                             R"({"type":"search","id":"q","placeholder":"Etwas eintippen …"},)"
                             R"({"type":"text","title":"Du hast getippt:","text":")" + Esc(typed.empty() ? "(noch nichts)" : typed) + R"("},)"
                             R"({"type":"button","id":"send","title":"Nachricht schicken"}]})";
            rr.app_ui(kId, ui.c_str());
        }
        Sleep(50);
    }
    return 0;
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    quit = false;
    thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown(void) {
    quit = true;
    if (thread) { WaitForSingleObject(thread, 3000); CloseHandle(thread); thread = nullptr; }
}
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
