// Tests the actual menu view without installing game input hooks.
#include "Localization.h"
#include "SettingsMenu.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
struct LocalizationTestAccess {
    static MenuView View(SettingsMenu& menu,const std::string& language,int page=0){
        menu.edit_.menuLanguage=language;menu.page_=page;Localization::SetLanguage(language);menu.BuildDefs();return menu.View();
    }
    static void SelectRussian(SettingsMenu& menu){
        menu.edit_.menuLanguage="en";Localization::SetLanguage("en");menu.BuildDefs();
        for(size_t i=0;i<menu.defs_.size();++i)if(std::string(menu.defs_[i].en)=="Menu language"){
            menu.Adjust((int)i,1);
            if(menu.edit_.menuLanguage!="ru")throw std::runtime_error("Russian is not selectable");return;
        }
        throw std::runtime_error("Menu language row missing");
    }
};
void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){
    try {
        Require(Config{}.menuLanguage=="de","Default language must remain German");
        SettingsMenu menu;auto en=LocalizationTestAccess::View(menu,"en");
        Require(en.btnSave==L"Save","English changed");
        auto de=LocalizationTestAccess::View(menu,"de");
        Require(de.btnSave==L"Speichern","German changed");
        LocalizationTestAccess::SelectRussian(menu);
        auto ru=LocalizationTestAccess::View(menu,"ru");
        Require(ru.btnSave==L"Сохранить","Russian Save missing");
        bool volume=false,language=false;
        for(auto& item:ru.items){volume|=item.label==L"Громкость";language|=item.value==L"Русский";}
        Require(volume&&language,"Russian menu labels/choice missing");
        Require(Localization::Text(L"Untranslated UI")==L"Untranslated UI","Unknown text changed");
        auto call=Localization::Format(L"Incoming call • {key} to answer",{{L"key",L"{city}"}});
        Require(call==L"Входящий звонок • {city} – ответить","Placeholder formatting changed user text");
        Require(std::wstring(Localization::Select(true,L"Incoming call …",L"Eingehender Anruf …"))==L"Входящий вызов…","Phone translation missing");
        auto image=RenderMenu(ru,1080);
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+(DWORD)(image->pixels.size()*4);
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=image->width;info.biHeight=-image->height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        std::ofstream bitmap("russian-menu.bmp",std::ios::binary);bitmap.write((char*)&file,sizeof(file));bitmap.write((char*)&info,sizeof(info));bitmap.write((char*)image->pixels.data(),image->pixels.size()*4);bitmap.close();
        for(size_t page=0;page<ru.pages.size();++page){auto view=LocalizationTestAccess::View(menu,"ru",(int)page);Require(!view.section.empty(),"Empty page");}
        LocalizationTestAccess::View(menu,"en");Require(std::string(Localization::Select(true,"Volume","Lautstärke"))=="Volume","Switch back to English failed");
        std::cout<<"Russian selection, menu labels, all pages, phone strings, fallback and DE/EN switching: PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
