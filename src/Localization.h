// MPL-2.0. Russian strings restored from the community translation shipped in 1.7.0.
#pragma once
#include <string>
#include <initializer_list>
#include <utility>
namespace Localization {
void SetLanguage(const std::string& code);
bool Russian();
const char* Select(bool english, const char* en, const char* de);
const wchar_t* Select(bool english, const wchar_t* en, const wchar_t* de);
std::wstring Text(const std::wstring& text);
std::wstring Format(const std::wstring& text, std::initializer_list<std::pair<std::wstring,std::wstring>> values);
}
