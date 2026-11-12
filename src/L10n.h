// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Languages of the menus and messages. The texts are written in the code as a German / English pair. Other languages
// come from a table looked up by the German text: built in (Russian, L10nTexts.inc made by tools/l10n/make_l10n.py
// from tools/l10n/ru.txt) and language files of the players (realistic-radio\languages\*.txt, 1.7.0). Where a
// language has no translation for a text, English is shown.
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace L10n {
struct Language {
    std::string code;        // "de", "en", "ru", "pl", ...
    std::wstring name;       // as shown in the menu
    std::wstring file;       // language file (empty: built in)
    int texts = 0;           // translated texts (of Catalogue())
};

// dir: the realistic-radio folder (ending with \). Reads languages\*.txt and writes the template for a new language.
void Init(const std::wstring& dir);
void SetLanguage(const std::string& code);   // menuLanguage; unknown codes show English
const char* Code();                           // the current code
std::vector<Language> Languages();            // de, en, then the others sorted by name
bool ValidCode(const std::string& code);      // 2-3 letters, optionally -region: "pl", "pt-br"
int Catalogue();                              // number of texts in the plugin

// the text for the current language. `english` is the English flag the code used so far (German menu: false).
// The returned pointer stays valid for the lifetime of the program (literal or kept copy).
const wchar_t* Pick(const wchar_t* de, const wchar_t* en, bool english);
const char* PickA(const char* de, const char* en, bool english);   // UTF-8
// a text from the settings file (German default like "Jetzt läuft:"): translated while it is the default, else as written
std::wstring Ov(const std::string& utf8);
// fills placeholders: Fill(Pick(L"Taste {n}", L"Button {n}", en), {{L"{n}", L"3"}})
std::wstring Fill(std::wstring text, std::initializer_list<std::pair<const wchar_t*, std::wstring>> values);
wchar_t Decimal();              // 94,3 / 94.3
const wchar_t* Thousands();     // 12.500 / 12,500 / 12 500
std::string Voice();            // Windows language id of the voice ("407"), empty: Windows' default voice
}
