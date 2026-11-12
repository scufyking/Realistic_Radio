// Tests the actual menu view without installing game input hooks.
#include "Localization.h"
#include "SettingsMenu.h"
#include "RadioManager.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
struct LocalizationTestAccess {
    static PhoneView PhonePage(Phone& phone,PhoneConfig& config,bool english,PhoneScreen screen=PhoneScreen::Messages){
        phone.host_.settings=[&config]() -> const PhoneConfig& { return config; };
        phone.host_.english=[english]{return english;};phone.host_.gameTime=[]{return 1500u;};
        phone.host_.design=[]{return std::string("modern");};phone.screen_=screen;
        return phone.View();
    }
    static OverlayInfo RadioView(RadioManager& radio,const std::string& language,bool playing=true){
        radio.cfg_.menuLanguage=language;radio.cfg_.tuning="list";Localization::SetLanguage(language);
        radio.db_.stations["test"]={Station{"Volume","https://example.invalid",""}};
        radio.currentCountry_="test";radio.currentIndex_=0;radio.playing_=playing;
        radio.list_={{"test",0,0}};radio.song_=L"Now playing:";
        return radio.CurrentInfo();
    }
    static void CustomHeadline(RadioManager& radio,const std::string& text){radio.cfg_.overlay.textNowPlaying=text;}
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
void SaveBitmap(const std::string& name,const OverlayImage& image){
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+(DWORD)(image.pixels.size()*4);
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=image.width;info.biHeight=-image.height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream bitmap(name,std::ios::binary);bitmap.write((char*)&file,sizeof(file));bitmap.write((char*)&info,sizeof(info));bitmap.write((char*)image.pixels.data(),image.pixels.size()*4);
}
int main(){
    try {
        Require(Config{}.menuLanguage=="de","Default language must remain German");
        RadioManager radio;
        const char* codes[]={"ru","de","en","ru","en","de"};
        for(const char* code:codes){
            auto panel=LocalizationTestAccess::RadioView(radio,code);
            std::string lang=code;
            Require(panel.headline==(lang=="ru"?L"Сейчас в эфире:":lang=="en"?L"Now playing:":L"Jetzt läuft:"),"Popup headline did not switch language");
            Require(panel.title==L"Volume"&&panel.song==L"Now playing:","Station/song names must not be translated");
            SaveBitmap(std::string("popup-")+code+".bmp",*RenderOverlay(panel,1080,1,nullptr));
            SaveBitmap(std::string("navigation-")+code+".bmp",*RenderScreenCard(panel,nullptr,800,450));
            auto classic=panel;classic.button=1;
            SaveBitmap(std::string("classic-")+code+".bmp",*RenderScreenCard(classic,nullptr,800,450,"classic"));
            Require(panel.detail.find(lang=="ru"?L"Станции":lang=="en"?L"Stations":L"Sender")==0,"Station count label did not switch");
            auto stopped=LocalizationTestAccess::RadioView(radio,code,false);
            Require(stopped.lcd==(lang=="ru"?L"Радио выключено":lang=="en"?L"Radio off":L"Radio aus"),"Stopped state did not switch");
            OverlayConfig overlay;MediaConfig media;PhoneConfig phone;
            auto volume=Localization::OverlayText(overlay,&OverlayConfig::textVolume);
            Require(volume==(lang=="ru"?L"Громкость":lang=="en"?L"Volume":L"Lautstärke"),"Volume label did not switch");
            Require(Localization::MediaText(media,&MediaConfig::textTrack)==(lang=="ru"?L"Трек":lang=="en"?L"Track":L"Titel"),"USB label did not switch");
            Require(Localization::PhoneText(phone,&PhoneConfig::sender)==(lang=="ru"?L"Клиент":lang=="en"?L"Customer":L"Kunde"),"Default phone sender did not switch");
            Phone device;auto view=LocalizationTestAccess::PhonePage(device,phone,lang!="de");
            Require(view.title==(lang=="ru"?L"Сообщения":lang=="en"?L"Messages":L"Nachrichten"),"Phone page did not switch language");
            Require(view.day==(lang=="ru"?L"Вторник":lang=="en"?L"Tuesday":L"Dienstag"),"Phone weekday did not switch");
            auto home=LocalizationTestAccess::PhonePage(device,phone,lang!="de",PhoneScreen::Home);
            SaveBitmap(std::string("phone-")+code+".bmp",*RenderPhone(home,1080,nullptr));
            std::string OverlayConfig::* labels[]={&OverlayConfig::textNowPlaying,&OverlayConfig::textVolume,&OverlayConfig::textStopped,&OverlayConfig::textMuted,&OverlayConfig::textFavourite,&OverlayConfig::textConnecting,&OverlayConfig::textUnavailable,&OverlayConfig::textPowerOff,&OverlayConfig::textRadio,&OverlayConfig::textStations};
            const wchar_t* english[]={L"Now playing:",L"Volume",L"Radio off",L"Mute",L"Saved as favourite",L"connecting…",L"Station not available",L"Ignition off",L"Radio",L"Stations"};
            for(size_t n=0;n<10;++n){
                if(lang=="en")Require(Localization::OverlayText(overlay,labels[n])==english[n],"Configured English status missing");
                if(lang=="de")Require(Localization::OverlayText(overlay,labels[n])==Utf8ToWide(overlay.*labels[n]),"Configured German status missing");
                if(lang=="ru")Require(Localization::OverlayText(overlay,labels[n])!=Utf8ToWide(overlay.*labels[n]),"Configured Russian status missing");
                auto saved=overlay.*labels[n];overlay.*labels[n]="Volume (my caption)";
                Require(Localization::OverlayText(overlay,labels[n])==L"Volume (my caption)","Custom status changed");overlay.*labels[n]=saved;
            }
            phone.sender="Volume";Require(Localization::PhoneText(phone,&PhoneConfig::sender)==L"Volume","Custom sender was translated");
            Require(overlay.textNowPlaying=="Jetzt läuft:","Translation must not rewrite configuration");
        }
        LocalizationTestAccess::CustomHeadline(radio,"Volume");
        Require(LocalizationTestAccess::RadioView(radio,"ru").headline==L"Volume","Custom popup headline was translated");
        LocalizationTestAccess::CustomHeadline(radio,"Сейчас в эфире:");
        Require(LocalizationTestAccess::RadioView(radio,"en").headline==L"Now playing:","Stored built-in Russian text must switch back to English");
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
        std::cout<<"DE/EN/RU switching: menu, actual popup/navigation view, configured statuses, USB, phone, weekdays and custom text: PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
