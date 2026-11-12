// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "L10n.h"
#include "Common.h"
#include "Logger.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace {
struct Row { const wchar_t* de; const wchar_t* en; const wchar_t* ru; };
const Row kTexts[] = {
#include "L10nTexts.inc"
};
constexpr int kCount = (int)(sizeof(kTexts) / sizeof(kTexts[0]));
const std::wstring kVersion = REALISTIC_RADIO_VERSION_W;

std::wstring Trim(const std::wstring& t, size_t* lead = nullptr, size_t* trail = nullptr) {
    size_t a = t.find_first_not_of(L' '), b = t.find_last_not_of(L' ');
    if (a == std::wstring::npos) { if (lead) *lead = t.size(); if (trail) *trail = 0; return L""; }
    if (lead) *lead = a;
    if (trail) *trail = t.size() - 1 - b;
    return t.substr(a, b - a + 1);
}

struct Lang {
    std::string code;
    std::wstring name, file;
    std::string voice;                                   // "419"
    wchar_t decimal = L'.';
    std::wstring thousands = L",";
    std::unordered_map<std::wstring, std::wstring> byDe; // trimmed German text -> translation (trimmed)
};

std::mutex g_mtx;                             // the language list and the lookup cache
std::vector<std::unique_ptr<Lang>> g_langs;   // never shrinks: pointers stay valid
std::atomic<const Lang*> g_cur{nullptr};
std::string g_wanted = "de";

// catalogue: trimmed English -> trimmed German (code that writes another German wording for the same English text)
const std::unordered_map<std::wstring, std::wstring>& DeByEn() {
    static const auto m = [] { std::unordered_map<std::wstring, std::wstring> r; for (auto& e : kTexts) if (*e.en) r.emplace(Trim(e.en), Trim(e.de)); return r; }();
    return m;
}
const std::unordered_map<std::wstring, const Row*>& RowByDe() {
    static const auto m = [] { std::unordered_map<std::wstring, const Row*> r; for (auto& e : kTexts) r.emplace(Trim(e.de), &e); return r; }();
    return m;
}

Lang* Find(const std::string& code) {
    for (auto& l : g_langs) if (l->code == code) return l.get();
    return nullptr;
}

void AddBuiltIn() {   // under g_mtx
    if (!g_langs.empty()) return;
    auto de = std::make_unique<Lang>(); de->code = "de"; de->name = L"Deutsch"; de->voice = "407"; de->decimal = L','; de->thousands = L".";
    auto en = std::make_unique<Lang>(); en->code = "en"; en->name = L"English"; en->voice = "409";
    auto ru = std::make_unique<Lang>(); ru->code = "ru"; ru->name = L"Русский"; ru->voice = "419";
    ru->decimal = L','; ru->thousands = L" ";
    for (auto& e : kTexts) if (*e.ru) ru->byDe[Trim(e.de)] = e.ru;
    g_langs.push_back(std::move(de)); g_langs.push_back(std::move(en)); g_langs.push_back(std::move(ru));
}

const Lang* Current() {
    const Lang* l = g_cur.load();
    if (l) return l;
    std::lock_guard<std::mutex> lk(g_mtx);
    AddBuiltIn();
    const Lang* f = Find(g_wanted);
    if (!f) f = Find("en");
    g_cur = f;
    return f;
}

// the translation with the spaces of the original around it; kept, so the pointer stays valid
const wchar_t* Lookup(const Lang* l, const wchar_t* de, const wchar_t* en) {
    static std::unordered_map<std::wstring, std::wstring> done;   // under g_mtx
    std::lock_guard<std::mutex> lk(g_mtx);
    std::wstring key = std::wstring(l->code.begin(), l->code.end()) + L'\x01' + (de ? de : L"") + L'\x01' + (en ? en : L"");
    auto d = done.find(key);
    if (d != done.end()) return d->second.empty() ? nullptr : d->second.c_str();
    std::wstring out;
    for (int pass = 0; pass < 2 && out.empty(); ++pass) {
        const wchar_t* src = pass == 0 ? de : en;
        if (!src || !*src) continue;
        size_t lead = 0, trail = 0;
        std::wstring k = Trim(src, &lead, &trail);
        if (k.empty()) continue;
        if (pass == 1) { auto e = DeByEn().find(k); if (e == DeByEn().end()) continue; k = e->second; }
        auto it = l->byDe.find(k);
        if (it != l->byDe.end() && !it->second.empty()) out = std::wstring(lead, L' ') + it->second + std::wstring(trail, L' ');
    }
    auto ins = done.emplace(key, out).first;
    return ins->second.empty() ? nullptr : ins->second.c_str();
}

// ---- language files
std::wstring Unescape(const std::wstring& s) {
    std::wstring o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'n' || s[i + 1] == L'r')) { o += s[i + 1] == L'n' ? L'\n' : L'\r'; ++i; }
        else o += s[i];
    }
    return o;
}
std::wstring Escape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) { if (c == L'\n') o += L"\\n"; else if (c == L'\r') o += L"\\r"; else o += c; }
    return o;
}
std::string ReadAll(const std::wstring& file) {
    std::ifstream f{std::filesystem::path(file), std::ios::binary};
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
std::wstring ReadUtf8(const std::wstring& file) {
    std::string s = ReadAll(file);
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);
    return Utf8ToWide(s);
}
void WriteUtf8IfChanged(const std::wstring& file, const std::wstring& text) {
    std::string u = WideToUtf8(text);
    if (ReadAll(file) == u) return;
    std::ofstream o{std::filesystem::path(file), std::ios::binary | std::ios::trunc};
    o << u;
}
std::vector<std::wstring> Lines(const std::wstring& t) {
    std::vector<std::wstring> r; std::wstring cur;
    for (wchar_t c : t) { if (c == L'\n') { r.push_back(cur); cur.clear(); } else if (c != L'\r') cur += c; }
    r.push_back(cur);
    return r;
}
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

struct Parsed { std::map<std::string, std::wstring> head; std::unordered_map<std::wstring, std::wstring> tr; std::vector<std::wstring> des; };
// DE: / EN: lines and the translation after TEXT: or after the language's code (RU: ...); header lines before the first [ ]
Parsed Parse(const std::wstring& text) {
    Parsed p; bool entries = false, haveDe = false; std::wstring de;
    for (const std::wstring& raw : Lines(text)) {
        if (raw.empty() || raw[0] == L'#') continue;
        if (raw[0] == L'[') { entries = true; haveDe = false; continue; }
        size_t colon = raw.find(L':');
        if (colon == std::wstring::npos || colon > 40) continue;
        std::wstring key = raw.substr(0, colon), val = raw.substr(colon + 1);
        if (!val.empty() && val[0] == L' ') val.erase(0, 1);
        size_t paren = key.find(L" (");   // "EN (gleiche Zeile / same line)"
        if (paren != std::wstring::npos) key.resize(paren);
        std::string k = WideToUtf8(key);
        if (!entries) {
            if (k == "CODE" || k == "NAME" || k == "VOICE" || k == "DECIMAL" || k == "THOUSANDS") p.head[k] = Trim(val);
            continue;
        }
        if (k == "DE") { if (!haveDe) { de = Unescape(val); haveDe = true; p.des.push_back(Trim(de)); } }
        else if (k == "EN") {}
        else if (haveDe && (k == "TEXT" || (k.size() >= 2 && k.size() <= 3 && std::all_of(k.begin(), k.end(), [](char c) { return c >= 'A' && c <= 'Z'; })))) {
            std::wstring t = Trim(Unescape(val));
            if (!t.empty()) p.tr.emplace(Trim(de), t);
        }
    }
    return p;
}

std::wstring EntryBlock(int n, const Row& r, const std::wstring& text) {
    return L"[" + std::to_wstring(n) + L"]\nDE: " + Escape(r.de) + L"\nEN: " + Escape(r.en) + L"\nTEXT: " + Escape(Trim(text)) + L"\n\n";
}

const wchar_t* kHowTo =
    L"# DE: So kommt eine neue Sprache ins Radio:\n"
    L"#   1. Diese Vorlage kopieren und umbenennen, z. B. in \"pl.txt\" (ohne _ am Anfang).\n"
    L"#   2. Unten bei CODE das Kürzel (z. B. pl) und bei NAME den Namen für das Menü (z. B. Polski) eintragen.\n"
    L"#   3. Hinter jedes \"TEXT:\" die Übersetzung schreiben. Leer gelassene Texte erscheinen auf Englisch.\n"
    L"#      Platzhalter wie {city}, {n}, {station}, {time} und Zeichen wie • → – unverändert übernehmen.\n"
    L"#   4. Spiel neu starten, dann Einstellungsmenü (Strg + 0) → Radio → Sprache des Menüs.\n"
    L"#   Diese Vorlage wird bei jedem Start neu geschrieben – nicht hier übersetzen. Kommen mit einem Update neue Texte\n"
    L"#   dazu, hängt das Plugin sie unten an deine Datei an. Fertige Übersetzung gern an mich schicken (SCS-Forum oder\n"
    L"#   GitHub), dann kommt sie für alle ins Plugin.\n"
    L"# EN: How to add a language to the radio:\n"
    L"#   1. Copy this template and rename it, e.g. to \"pl.txt\" (without the _ at the start).\n"
    L"#   2. Enter the short code at CODE (e.g. pl) and the name for the menu at NAME (e.g. Polski).\n"
    L"#   3. Write the translation after every \"TEXT:\". Texts left empty are shown in English.\n"
    L"#      Keep placeholders like {city}, {n}, {station}, {time} and characters like • → – as they are.\n"
    L"#   4. Restart the game, then settings menu (Ctrl + 0) → Radio → Menu language.\n"
    L"#   This template is written again at every start - do not translate here. When an update brings new texts, the plugin\n"
    L"#   adds them at the end of your file. Please send me your finished translation (SCS forum or GitHub) and it goes into\n"
    L"#   the plugin for everyone.\n"
    L"#\n"
    L"# VOICE: Windows-Sprach-ID der Stimme für die Anrufe / Windows language id of the voice for the calls, e.g.\n"
    L"#        415 Polski, 40C Français, 410 Italiano, 413 Nederlands, 405 Čeština, 40A Español, 416 Português (Brasil),\n"
    L"#        41F Türkçe, 422 Українська. Leer / empty = Windows-Standardstimme / Windows' default voice.\n"
    L"# DECIMAL: Dezimalzeichen / decimal separator (94,3 MHz)   THOUSANDS: Tausendertrennzeichen / thousands separator\n"
    L"#          (12.500 €), \"space\" = Leerzeichen / a space, \"none\" = keins / none\n\n";

void WriteTemplates(const std::wstring& folder) {
    std::wstring t = L"# Realistic Radio " + kVersion + L" – Vorlage für eine neue Sprache / template for a new language\n#\n";
    t += kHowTo;
    t += L"CODE: xx\nNAME: \nVOICE: \nDECIMAL: ,\nTHOUSANDS: .\n\n";
    for (int i = 0; i < kCount; ++i) t += EntryBlock(i + 1, kTexts[i], L"");
    WriteUtf8IfChanged(folder + L"_Vorlage - template.txt", t);
    // the built-in Russian as a filled example; copied to ru.txt it corrects single texts
    std::wstring r = L"# Realistic Radio " + kVersion + L" – eingebaute Übersetzung als Beispiel / built-in translation as an example\n"
                     L"# DE: Als ru.txt kopiert, ersetzen die Texte darin die eingebauten (zum Verbessern).\n"
                     L"# EN: Copied to ru.txt, its texts replace the built-in ones (to improve them).\n\n"
                     L"CODE: ru\nNAME: Русский\nVOICE: 419\nDECIMAL: ,\nTHOUSANDS: space\n\n";
    for (int i = 0; i < kCount; ++i) r += EntryBlock(i + 1, kTexts[i], kTexts[i].ru);
    WriteUtf8IfChanged(folder + L"_Beispiel Russisch - example Russian.txt", r);
}

// a player's language file: read it and add the texts it does not have yet at the end (under g_mtx)
void LoadFile(const std::wstring& path, const std::wstring& stem) {
    Parsed p = Parse(ReadUtf8(path));
    const std::string fileName = WideToUtf8(std::filesystem::path(path).filename().wstring());
    std::string code = Lower(p.head.count("CODE") && !p.head["CODE"].empty() ? WideToUtf8(p.head["CODE"]) : WideToUtf8(stem));
    if (!L10n::ValidCode(code)) { Log::Write("Languages: %s - CODE '%s' is not a language code (e.g. pl, fr, pt-br), file skipped", fileName.c_str(), code.c_str()); return; }
    if (code == "de" || code == "en") { Log::Write("Languages: %s - German and English are the source texts, file skipped", fileName.c_str()); return; }
    Lang* l = Find(code);
    const bool builtIn = l != nullptr;
    if (!l) { g_langs.push_back(std::make_unique<Lang>()); l = g_langs.back().get(); l->code = code; l->name = Utf8ToWide(code); }
    l->file = path;
    if (p.head.count("NAME") && !p.head["NAME"].empty()) l->name = p.head["NAME"];
    if (p.head.count("VOICE")) {
        std::string v = WideToUtf8(p.head["VOICE"]);
        if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0) v = v.substr(2);
        if (v.size() <= 4 && std::all_of(v.begin(), v.end(), [](char c) { return isxdigit((unsigned char)c) != 0; })) l->voice = v;
    }
    if (p.head.count("DECIMAL") && !p.head["DECIMAL"].empty()) l->decimal = p.head["DECIMAL"][0];
    if (p.head.count("THOUSANDS")) {
        const std::wstring& t = p.head["THOUSANDS"];
        l->thousands = t == L"space" ? L" " : t == L"none" ? L"" : t;
    }
    for (auto& e : p.tr) l->byDe[e.first] = e.second;   // a file for a built-in language replaces single texts
    // texts of this version that the file does not have yet: append them, so the translator finds them
    // (not for a file that only corrects a built-in language)
    std::unordered_map<std::wstring, bool> has;
    for (auto& d : p.des) has[d] = true;
    std::wstring add;
    int added = 0;
    for (int i = 0; i < kCount; ++i)
        if (!builtIn && !has.count(Trim(kTexts[i].de))) { add += EntryBlock(i + 1, kTexts[i], L""); ++added; }
    if (added) {
        std::ofstream o{std::filesystem::path(path), std::ios::binary | std::ios::app};
        o << WideToUtf8(L"\n# ---- " + std::to_wstring(added) + L" neue Texte in " + kVersion + L" / new texts in " + kVersion + L" ----\n\n" + add);
    }
    int done = 0;
    for (int i = 0; i < kCount; ++i) if (l->byDe.count(Trim(kTexts[i].de))) ++done;
    Log::Write("Languages: %s '%s' from %s - %d of %d texts translated%s", code.c_str(), WideToUtf8(l->name).c_str(), fileName.c_str(), done, kCount,
               builtIn ? " (replaces built-in texts)" : "");
    if (added) Log::Write("Languages: %d new texts added at the end of %s", added, fileName.c_str());
}
}

namespace L10n {
bool ValidCode(const std::string& c) {
    size_t i = 0;
    while (i < c.size() && c[i] >= 'a' && c[i] <= 'z') ++i;
    if (i < 2 || i > 3) return false;
    if (i == c.size()) return true;
    if (c[i] != '-' || c.size() - i - 1 < 2 || c.size() - i - 1 > 8) return false;
    for (size_t k = i + 1; k < c.size(); ++k) if (!((c[k] >= 'a' && c[k] <= 'z') || (c[k] >= '0' && c[k] <= '9'))) return false;
    return true;
}

void Init(const std::wstring& dir) {
    static bool once = false;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (once) return;
    once = true;
    AddBuiltIn();
    const std::wstring folder = dir + L"languages\\";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(folder), ec);
    try { WriteTemplates(folder); } catch (...) { Log::Write("Languages: could not write the template into %s", WideToUtf8(folder).c_str()); }
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(std::filesystem::path(folder), ec)) {
        const auto& f = e.path();
        if (e.is_regular_file(ec) && Lower(WideToUtf8(f.extension().wstring())) == ".txt" && !f.filename().wstring().empty() && f.filename().wstring()[0] != L'_')
            files.push_back(f);
    }
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        try { LoadFile(f.wstring(), f.stem().wstring()); }
        catch (...) { Log::Write("Languages: %s could not be read", WideToUtf8(f.filename().wstring()).c_str()); }
    }
    Log::Write("Languages: %d texts, built in: Deutsch, English, Russian; %d language file(s) in languages\\", kCount, (int)files.size());
    const Lang* f = Find(g_wanted);   // the wanted language may come from a file
    g_cur = f ? f : Find("en");
}

void SetLanguage(const std::string& code) {
    const Lang* cur = g_cur.load();
    if (cur && cur->code == code) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    AddBuiltIn();
    g_wanted = code;
    const Lang* f = Find(code);
    g_cur = f ? f : Find("en");
}

const char* Code() { return Current()->code.c_str(); }

std::vector<Language> Languages() {
    std::lock_guard<std::mutex> lk(g_mtx);
    AddBuiltIn();
    std::vector<Language> r;
    for (auto& l : g_langs) {
        Language x; x.code = l->code; x.name = l->name; x.file = l->file;
        for (auto& e : kTexts) if (l->code == "de" || l->code == "en" || l->byDe.count(Trim(e.de))) ++x.texts;
        r.push_back(x);
    }
    std::sort(r.begin() + 2, r.end(), [](const Language& a, const Language& b) { return a.name < b.name; });
    return r;
}

int Catalogue() { return kCount; }

const wchar_t* Pick(const wchar_t* de, const wchar_t* en, bool english) {
    const Lang* l = Current();
    if (l->code == "de" || l->code == "en") return english ? en : de;
    if (const wchar_t* r = Lookup(l, de, en)) return r;
    return en;
}

const char* PickA(const char* de, const char* en, bool english) {
    const Lang* l = Current();
    if (l->code == "de" || l->code == "en") return english ? en : de;
    static std::unordered_map<std::string, std::string> cache;   // UTF-8 copies; entries are never removed
    static std::mutex mtx;
    std::string key = l->code + '\x01' + (de ? de : "") + '\x01' + (en ? en : "");
    {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.c_str();
    }
    const wchar_t* r = Lookup(l, Utf8ToWide(de ? de : "").c_str(), Utf8ToWide(en ? en : "").c_str());
    std::string v = r ? WideToUtf8(r) : std::string(en ? en : "");
    std::lock_guard<std::mutex> lk(mtx);
    return cache.emplace(key, v).first->second.c_str();
}

std::wstring Ov(const std::string& utf8) {
    std::wstring w = Utf8ToWide(utf8);
    const Lang* l = Current();
    if (l->code == "de") return w;
    size_t lead = 0, trail = 0;
    std::wstring k = Trim(w, &lead, &trail);
    auto it = RowByDe().find(k);
    if (it == RowByDe().end()) return w;   // the player's own text
    std::wstring t = it->second->en;
    if (l->code != "en") if (const wchar_t* r = Lookup(l, it->second->de, it->second->en)) t = r;
    return std::wstring(lead, L' ') + Trim(t) + std::wstring(trail, L' ');
}

std::wstring Fill(std::wstring text, std::initializer_list<std::pair<const wchar_t*, std::wstring>> values) {
    for (auto& v : values) {
        const std::wstring k = v.first;
        for (size_t p = text.find(k); p != std::wstring::npos; p = text.find(k, p + v.second.size())) text.replace(p, k.size(), v.second);
    }
    return text;
}

wchar_t Decimal() { return Current()->decimal; }
const wchar_t* Thousands() { return Current()->thousands.c_str(); }
std::string Voice() { return Current()->voice; }
}
