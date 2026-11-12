// MPL-2.0. Russian strings restored from the community translation shipped in 1.7.0.
#pragma once
#include <string>
#include <initializer_list>
#include <utility>
struct OverlayConfig;
struct MediaConfig;
struct PhoneConfig;
namespace Localization {
void SetLanguage(const std::string& code);
bool Russian();
const char* Select(bool english, const char* en, const char* de);
const wchar_t* Select(bool english, const wchar_t* en, const wchar_t* de);
std::wstring Text(const std::wstring& text);
// Translate only the built-in forms of this particular setting. Custom text
// (even if it happens to match another UI label) is left untouched.
std::wstring Configured(const std::string& value, const std::string& defaultText);
std::wstring OverlayText(const OverlayConfig& config, std::string OverlayConfig::* field);
std::wstring MediaText(const MediaConfig& config, std::string MediaConfig::* field);
std::wstring PhoneText(const PhoneConfig& config, std::string PhoneConfig::* field);
std::wstring Format(const std::wstring& text, std::initializer_list<std::pair<std::wstring,std::wstring>> values);
}
