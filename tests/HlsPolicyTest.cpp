#include "HlsPolicy.h"
#include <iostream>
#include <stdexcept>
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){
    try{
        uint64_t rate=0;
        for(uint64_t candidate:{33945,65945,129945})if(HlsPolicy::Prefer(true,candidate,true,rate))rate=candidate;
        Require(rate==129945,"Must choose the 128k rendition instead of 32k");
        Require(HlsPolicy::Prefer(true,32000,false,128000),"Audio-only must beat video");
        Require(!HlsPolicy::Prefer(false,128000,true,32000),"Video must not beat audio-only");
        Require(HlsPolicy::Prefer(true,128000,true,0),"Known bitrate must beat unknown");
        Require(HlsPolicy::StartupSeconds(6,32000)>65,"Low bitrate requires sufficient probe history");
        Require(HlsPolicy::StartupSeconds(6,128000)>18&&HlsPolicy::StartupSeconds(6,128000)<24,"128k should use a small initial history");
        Require(HlsPolicy::StartupSeconds(10,256000)==30,"Keep the three-target-duration margin");
        Require(HlsPolicy::StartupSeconds(6,1)==90,"Cap extra probe history");
        Require(HlsPolicy::StartupSeconds(40,1)==120,"Do not violate long target-duration margin");
        std::cout<<"HLS quality selection and startup history: PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
