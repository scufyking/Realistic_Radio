// Uses exactly the same native HLS transport and byte stream as the plugin.
#include "HlsInput.h"
#include "Logger.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <cmath>
#include <cstring>

int main(int argc,char** argv){
    if(argc<2)return 2;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(MFStartup(MF_VERSION,MFSTARTUP_FULL)))return 3;
    Log::Init(L"hls-native-probe.log");
    auto start=std::chrono::steady_clock::now();
    auto input=std::make_shared<HlsInput>(argv[1]);
    std::atomic<bool> finished{false};
    double cancelAfter=argc>2?std::stod(argv[2]):0;
    std::thread watchdog([&]{
        auto deadline=start+std::chrono::milliseconds((int)((cancelAfter>0?cancelAfter:95)*1000));
        while(!finished&&std::chrono::steady_clock::now()<deadline)Sleep(10);
        if(!finished)input->Cancel();
    });
    IMFSourceReader* reader=nullptr;IMFMediaType* type=nullptr;IMFMediaType* actual=nullptr;
    HRESULT hr=E_FAIL;uint64_t frames=0;UINT32 rate=0,ch=0;double energy=0;uint64_t nonzero=0;int gaps=0;LONGLONG previous=-1;double first=-1;
    input->Start();
    if(input->WaitReady())hr=MFCreateSourceReaderFromByteStream(input->Stream(),nullptr,&reader);
    if(SUCCEEDED(hr)){
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS,FALSE);reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
        hr=MFCreateMediaType(&type);
        if(SUCCEEDED(hr)){type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);hr=reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,type);}
        if(SUCCEEDED(hr))hr=reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,&actual);
        if(actual){actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch);}
        while(SUCCEEDED(hr)&&rate&&ch){
            IMFSample* sample=nullptr;DWORD flags=0;LONGLONG timestamp=0;
            hr=reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&timestamp,&sample);
            if(sample){
                IMFMediaBuffer* buffer=nullptr;
                if(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))){
                    BYTE* data=nullptr;DWORD length=0;
                    if(SUCCEEDED(buffer->Lock(&data,nullptr,&length))){
                        if(first<0)first=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                        auto n=length/(4*ch);if(previous>=0&&std::llabs(timestamp-previous)>500000)++gaps;
                        previous=timestamp+(LONGLONG)((uint64_t)n*10000000/rate);frames+=n;
                        for(DWORD i=0;i<length/4;++i){float value;memcpy(&value,data+i*4,4);if(value!=0)++nonzero;energy+=(double)value*value;}
                        buffer->Unlock();
                    }buffer->Release();
                }sample->Release();
            }
            if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM))break;
            if(frames>=rate*65ULL)break;
        }
    }
    auto stopping=std::chrono::steady_clock::now();input->Cancel();input->Join();
    if(reader)reader->Release();if(type)type->Release();if(actual)actual->Release();
    finished=true;watchdog.join();input.reset();
    double stopSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stopping).count();
    bool ok=cancelAfter>0?stopSeconds<2:frames>=rate*65ULL&&rate&&nonzero&&gaps==0;
    std::cout<<"{\"ok\":"<<(ok?"true":"false")<<",\"frames\":"<<frames<<",\"rate\":"<<rate<<",\"channels\":"<<ch<<",\"seconds\":"<<(rate?(double)frames/rate:0)<<",\"first_pcm\":"<<first<<",\"stop_seconds\":"<<stopSeconds<<",\"gaps\":"<<gaps<<",\"nonzero\":"<<nonzero<<",\"energy\":"<<energy<<",\"hr\":\"0x"<<std::hex<<(unsigned long)hr<<"\"}"<<std::endl;
    MFShutdown();CoUninitialize();Log::Shutdown();return ok?0:1;
}
