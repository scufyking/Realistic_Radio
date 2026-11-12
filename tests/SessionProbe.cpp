#include "StreamSource.h"
#include "Logger.h"
#include <objbase.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <cmath>
struct StreamSessionTestAccess {static bool Play(StreamSession& s){return s.PlayOnce(s.url_,0);}};
int main(int argc,char** argv){
    if(argc<2)return 2;
    Log::Init(L"session-native-probe.log");
    bool success=true;
    for(int test=0;test<5;++test){
        auto s=std::make_shared<StreamSession>(argv[1],10);
        std::atomic<bool> done{false};
        auto start=std::chrono::steady_clock::now();
        std::thread worker([&]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);StreamSessionTestAccess::Play(*s);CoUninitialize();done=true;});
        uint64_t frames=0,nonzero=0;float pcm[2048*2];
        double cancelAt=test==0?.05:test==1?12:0;
        while(!done){
            double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            if(cancelAt>0&&elapsed>=cancelAt)break;
            if(test>=2){auto got=s->Read(pcm,2048);frames+=got;for(unsigned i=0;i<got*2;++i)if(pcm[i]!=0)++nonzero;if(frames>=kRate*8ULL)break;}
            if(elapsed>45)break;
            Sleep(5);
        }
        auto stopping=std::chrono::steady_clock::now();s->Stop();
        for(int i=0;i<400&&!done;++i)Sleep(5);
        double stopSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stopping).count();
        bool ok=done&&stopSeconds<2&&(test<2||(frames>=kRate*8ULL&&nonzero));
        std::cout<<"{\"test\":"<<test<<",\"ok\":"<<(ok?"true":"false")<<",\"frames\":"<<frames<<",\"nonzero\":"<<nonzero<<",\"stop_seconds\":"<<stopSeconds<<"}"<<std::endl;
        if(!done)ExitProcess(3);worker.join();success&=ok;
    }
    Log::Shutdown();return success?0:1;
}
