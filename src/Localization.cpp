// MPL-2.0. Translation source: the Russian example file bundled with release 1.7.0.
#include "Localization.h"
#include "Common.h"
#include "Config.h"
#include <unordered_map>
namespace {
enum Language { German, English, Russian };
std::atomic<int> language{German};
struct Entry { const char* de; const char* en; const char* ru; };
const Entry entries[] = {
    {"Speicherplatz", "Preset", "Кнопка"},
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
    std::unordered_map<std::string,std::string> narrow[3];
    std::unordered_map<std::wstring,std::wstring> wide[3];
    Dictionary(){
        for(int lang=0;lang<3;++lang){
            // A string already written in the target language keeps its identity.
            for(auto& e:entries){const char* target=lang==German?e.de:lang==English?e.en:e.ru;
                if(*target){narrow[lang].emplace(target,target);wide[lang].emplace(Utf8ToWide(target),Utf8ToWide(target));}}
            for(auto& e:entries){const char* target=lang==German?e.de:lang==English?e.en:e.ru;
                if(!*target)target=e.en;
                for(auto* key:{e.de,e.en,e.ru})if(*key){narrow[lang].emplace(key,target);wide[lang].emplace(Utf8ToWide(key),Utf8ToWide(target));}}
        }
    }
};
const Dictionary& Get(){static const Dictionary dictionary;return dictionary;}
}
namespace Localization {
void SetLanguage(const std::string& code){language=code=="ru"?::Russian:code=="de"?German:English;}
bool Russian(){return language.load()==::Russian;}
const char* Select(bool english,const char* en,const char* de){
    if(!Russian())return english?en:de;
    const auto& dict=Get().narrow[::Russian];auto found=dict.find(en);if(found==dict.end())found=dict.find(de);
    return found==dict.end()?en:found->second.c_str();
}
const wchar_t* Select(bool english,const wchar_t* en,const wchar_t* de){
    if(!Russian())return english?en:de;
    const auto& dict=Get().wide[::Russian];auto found=dict.find(en);if(found==dict.end())found=dict.find(de);
    return found==dict.end()?en:found->second.c_str();
}
std::wstring Text(const std::wstring& text){
    const auto& dict=Get().wide[language.load()];auto found=dict.find(text);return found==dict.end()?text:found->second;
}
std::wstring Configured(const std::string& value,const std::string& defaultText){
    const Entry* entry=nullptr;
    for(auto& e:entries)if(defaultText==e.de){entry=&e;break;}
    if(!entry)for(auto& e:entries)if(defaultText==e.en||defaultText==e.ru){entry=&e;break;}
    if(!entry || (value!=entry->de&&value!=entry->en&&value!=entry->ru))return Utf8ToWide(value);
    int lang=language.load();const char* result=lang==German?entry->de:lang==English?entry->en:entry->ru;
    return Utf8ToWide(*result?result:entry->en);
}
std::wstring OverlayText(const OverlayConfig& c,std::string OverlayConfig::* f){static const OverlayConfig defaults;return Configured(c.*f,defaults.*f);}
std::wstring MediaText(const MediaConfig& c,std::string MediaConfig::* f){static const MediaConfig defaults;return Configured(c.*f,defaults.*f);}
std::wstring PhoneText(const PhoneConfig& c,std::string PhoneConfig::* f){static const PhoneConfig defaults;return Configured(c.*f,defaults.*f);}
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
