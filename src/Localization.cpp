// MPL-2.0. Translation source: the Russian example file bundled with release 1.7.0.
#include "Localization.h"
#include "Common.h"
#include <unordered_map>
namespace {
std::atomic<bool> russian{false};
struct Entry { const char* de; const char* en; const char* ru; };
const Entry entries[] = {
#include "RussianTranslations.inc"
    {"", " - nothing received", " — нет приёма"},
    {"", " - out of range", " — вне зоны приёма"},
    {"", " is empty - hold it to save", " пуста — удерживайте для сохранения"},
    {"", " stored with the stations received here", " сохранены с доступными здесь станциями"},
    {"", " with this size", " с таким размером"},
    {"", "Add music to realistic-radio\\", "Добавьте музыку в realistic-radio\\"},
    {"", "Found: ", "Найдено: "},
    {"", "Saved on button ", "Сохранено на кнопке "},
    {"", "Station buttons 1-", "Кнопки станций 1–"},
    {"", "late ", "опоздание "},
    {"", "now", "сейчас"},
};
struct Dictionary {
    std::unordered_map<std::string,std::string> narrow;
    std::unordered_map<std::wstring,std::wstring> wide;
    Dictionary(){for(auto& e:entries){
        if(!*e.ru)continue;
        for(auto* key:{e.de,e.en})if(*key){narrow.emplace(key,e.ru);wide.emplace(Utf8ToWide(key),Utf8ToWide(e.ru));}
    }}
};
const Dictionary& Get(){static const Dictionary dictionary;return dictionary;}
}
namespace Localization {
void SetLanguage(const std::string& code){russian=code=="ru";}
bool Russian(){return russian.load();}
const char* Select(bool english,const char* en,const char* de){
    if(!Russian())return english?en:de;
    const auto& dict=Get().narrow;auto found=dict.find(en);if(found==dict.end())found=dict.find(de);
    return found==dict.end()?en:found->second.c_str();
}
const wchar_t* Select(bool english,const wchar_t* en,const wchar_t* de){
    if(!Russian())return english?en:de;
    const auto& dict=Get().wide;auto found=dict.find(en);if(found==dict.end())found=dict.find(de);
    return found==dict.end()?en:found->second.c_str();
}
std::wstring Text(const std::wstring& text){
    if(!Russian())return text;
    const auto& dict=Get().wide;auto found=dict.find(text);return found==dict.end()?text:found->second;
}
std::wstring Format(const std::wstring& text,std::initializer_list<std::pair<std::wstring,std::wstring>> values){
    std::wstring pattern=Text(text),out;
    for(size_t p=0;p<pattern.size();){
        if(pattern[p]=='{'){
            auto end=pattern.find('}',p+1);
            if(end!=pattern.npos){auto key=pattern.substr(p+1,end-p-1);bool found=false;
                for(auto& value:values)if(value.first==key){out+=value.second;found=true;break;}
                if(found){p=end+1;continue;}
            }
        }
        out+=pattern[p++];
    }return out;
}
}
