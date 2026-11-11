// HLS test implementation. MPL-2.0, like the surrounding project.
#include "HlsInput.h"
#include "Logger.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cstring>
#include <chrono>

namespace {
constexpr size_t kLimit=4*1024*1024, kHistory=1024*1024;
constexpr size_t kHttpLimit=4*1024*1024;
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

// IMFAttributes shares this object's COM identity. No native adapter owns the I/O.
class LiveStream final : public IMFByteStream, public IMFAttributes {
    std::atomic<ULONG> refs_{1};
    IMFAttributes* attrs_=nullptr;
    DWORD queue_=0;
    bool queueOk_=false;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<BYTE> bytes_;
    QWORD base_=0,pos_=0,end_=0;
    bool finished_=false,closed_=false;
    HRESULT error_=S_OK;
    std::mutex readMtx_;
    struct Operation final : IUnknown {
        std::atomic<ULONG> refs{1};
        ULONG got=0; HRESULT status=E_PENDING;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
            if(!out)return E_POINTER; *out=nullptr;
            if(id==IID_IUnknown){*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    };
    class ReadWork final : public IMFAsyncCallback {
        std::atomic<ULONG> refs_{1};
        LiveStream* stream_; IMFAsyncResult* result_; Operation* op_; BYTE* data_; ULONG count_;
    public:
        ReadWork(LiveStream* s,IMFAsyncResult* r,Operation* op,BYTE* p,ULONG n):stream_(s),result_(r),op_(op),data_(p),count_(n){s->AddRef();r->AddRef();op->AddRef();}
        ~ReadWork(){op_->Release();result_->Release();stream_->Release();}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {
            if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IMFAsyncCallback){*p=static_cast<IMFAsyncCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
        ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
        HRESULT STDMETHODCALLTYPE GetParameters(DWORD*,DWORD*) override{return E_NOTIMPL;}
        HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult*) override {
            op_->status=stream_->Read(data_,count_,&op_->got);
            result_->SetStatus(op_->status);
            return MFInvokeCallback(result_);
        }
    };
public:
    LiveStream(){
        HRESULT hr=MFCreateAttributes(&attrs_,4);
        if(FAILED(hr))throw Failure("MFCreateAttributes failed");
        hr=MFAllocateWorkQueueEx(MF_STANDARD_WORKQUEUE,&queue_);
        if(FAILED(hr)){attrs_->Release();attrs_=nullptr;throw Failure("MFAllocateWorkQueueEx failed");}
        queueOk_=true;
    }
    ~LiveStream(){if(queueOk_)MFUnlockWorkQueue(queue_);if(attrs_)attrs_->Release();}
    void Format(const wchar_t* ext,const wchar_t* mime){
        attrs_->SetString(MF_BYTESTREAM_ORIGIN_NAME,ext);
        attrs_->SetString(MF_BYTESTREAM_CONTENT_TYPE,mime);
    }
    bool Append(const BYTE* p,size_t n){
        size_t done=0;
        while(done<n){
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l,[&]{return closed_||bytes_.size()<kLimit;});
            if(closed_)return false;
            size_t count=std::min(n-done,kLimit-bytes_.size());
            bytes_.insert(bytes_.end(),p+done,p+done+count);end_+=count;done+=count;
            cv_.notify_all();
        }
        return true;
    }
    void Finish(HRESULT hr=S_OK){std::lock_guard<std::mutex> l(m_);finished_=true;error_=hr;cv_.notify_all();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==IID_IMFByteStream||id==IID_IAgileObject)*out=static_cast<IMFByteStream*>(this);
        else if(id==IID_IMFAttributes)*out=static_cast<IMFAttributes*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetCapabilities(DWORD* p) override {if(!p)return E_POINTER;*p=MFBYTESTREAM_IS_READABLE|MFBYTESTREAM_IS_SEEKABLE|MFBYTESTREAM_IS_REMOTE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetLength(QWORD* p) override {if(!p)return E_POINTER;std::lock_guard<std::mutex> l(m_);*p=finished_?end_:std::numeric_limits<QWORD>::max();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetLength(QWORD) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(QWORD* p) override{if(!p)return E_POINTER;std::lock_guard<std::mutex> l(m_);*p=pos_;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(QWORD p) override{if(p>LLONG_MAX)return E_INVALIDARG;return Seek(msoBegin,(LONGLONG)p,0,nullptr);}
    HRESULT STDMETHODCALLTYPE IsEndOfStream(BOOL* p) override{if(!p)return E_POINTER;std::lock_guard<std::mutex> l(m_);*p=finished_&&pos_>=end_;return S_OK;}
    HRESULT STDMETHODCALLTYPE Read(BYTE* p,ULONG n,ULONG* got) override {
        if(!got||(!p&&n))return E_POINTER;*got=0;
        std::lock_guard<std::mutex> serial(readMtx_);
        std::unique_lock<std::mutex> l(m_);
        while(*got<n){
            cv_.wait(l,[&]{return closed_||finished_||pos_<end_;});
            if(closed_)return E_ABORT;
            if(pos_<base_)return E_INVALIDARG;
            size_t count=(size_t)std::min<QWORD>(n-*got,pos_<end_?end_-pos_:0);
            for(size_t i=0;i<count;++i)p[*got+i]=bytes_[(size_t)(pos_-base_)+i];
            pos_+=count;*got+=(ULONG)count;
            while(pos_>base_+kHistory&&!bytes_.empty()){bytes_.pop_front();++base_;}
            cv_.notify_all();
            if(finished_&&pos_>=end_)return *got?S_OK:error_;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BeginRead(BYTE* p,ULONG n,IMFAsyncCallback* callback,IUnknown* state) override {
        if(!callback||(!p&&n))return E_POINTER;
        {std::lock_guard<std::mutex> l(m_);if(closed_)return E_ABORT;}
        auto* op=new Operation;IMFAsyncResult* result=nullptr;
        HRESULT hr=MFCreateAsyncResult(op,callback,state,&result);
        if(SUCCEEDED(hr)){
            auto* work=new ReadWork(this,result,op,p,n);
            hr=MFPutWorkItem(queue_,work,nullptr);work->Release();result->Release();
        }
        op->Release();return hr;
    }
    HRESULT STDMETHODCALLTYPE EndRead(IMFAsyncResult* r,ULONG* got) override {
        if(!r||!got)return E_POINTER;IUnknown* object=nullptr;HRESULT hr=r->GetObject(&object);
        if(FAILED(hr))return hr;
        auto* op=static_cast<Operation*>(object);*got=op->got;hr=op->status;object->Release();return hr;
    }
    HRESULT STDMETHODCALLTYPE Write(const BYTE*,ULONG,ULONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE BeginWrite(const BYTE*,ULONG,IMFAsyncCallback*,IUnknown*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EndWrite(IMFAsyncResult*,ULONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Seek(MFBYTESTREAM_SEEK_ORIGIN origin,LONGLONG offset,DWORD flags,QWORD* result) override {
        if(flags&MFBYTESTREAM_SEEK_FLAG_CANCEL_PENDING_IO)return E_NOTIMPL;
        std::lock_guard<std::mutex> l(m_);if(closed_)return E_ABORT;
        if(origin!=msoBegin&&origin!=msoCurrent)return E_INVALIDARG;
        QWORD start=origin==msoBegin?0:pos_;
        QWORD target;
        if(offset<0){QWORD magnitude=(QWORD)(-(offset+1))+1;if(magnitude>start)return E_INVALIDARG;target=start-magnitude;}
        else {if(start>std::numeric_limits<QWORD>::max()-(QWORD)offset)return E_INVALIDARG;target=start+(QWORD)offset;}
        if(target<base_||(!finished_&&target>end_))return E_INVALIDARG;
        pos_=target;if(result)*result=pos_;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Flush() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Close() override{std::lock_guard<std::mutex> l(m_);closed_=true;cv_.notify_all();return S_OK;}

    // Forward attribute storage only, preserving a single IUnknown identity.
#define ATTR(method,args,call) HRESULT STDMETHODCALLTYPE method args override{return attrs_->method call;}
    ATTR(GetItem,(REFGUID k,PROPVARIANT* v),(k,v))
    ATTR(GetItemType,(REFGUID k,MF_ATTRIBUTE_TYPE* v),(k,v))
    ATTR(CompareItem,(REFGUID k,REFPROPVARIANT v,BOOL* b),(k,v,b))
    ATTR(Compare,(IMFAttributes* a,MF_ATTRIBUTES_MATCH_TYPE t,BOOL* b),(a,t,b))
    ATTR(GetUINT32,(REFGUID k,UINT32* v),(k,v))
    ATTR(GetUINT64,(REFGUID k,UINT64* v),(k,v))
    ATTR(GetDouble,(REFGUID k,double* v),(k,v))
    ATTR(GetGUID,(REFGUID k,GUID* v),(k,v))
    ATTR(GetStringLength,(REFGUID k,UINT32* v),(k,v))
    ATTR(GetString,(REFGUID k,LPWSTR v,UINT32 n,UINT32* got),(k,v,n,got))
    ATTR(GetAllocatedString,(REFGUID k,LPWSTR* v,UINT32* n),(k,v,n))
    ATTR(GetBlobSize,(REFGUID k,UINT32* n),(k,n))
    ATTR(GetBlob,(REFGUID k,UINT8* v,UINT32 n,UINT32* got),(k,v,n,got))
    ATTR(GetAllocatedBlob,(REFGUID k,UINT8** v,UINT32* n),(k,v,n))
    ATTR(GetUnknown,(REFGUID k,REFIID id,LPVOID* v),(k,id,v))
    ATTR(SetItem,(REFGUID k,REFPROPVARIANT v),(k,v))
    ATTR(DeleteItem,(REFGUID k),(k))
    ATTR(DeleteAllItems,(),())
    ATTR(SetUINT32,(REFGUID k,UINT32 v),(k,v))
    ATTR(SetUINT64,(REFGUID k,UINT64 v),(k,v))
    ATTR(SetDouble,(REFGUID k,double v),(k,v))
    ATTR(SetGUID,(REFGUID k,REFGUID v),(k,v))
    ATTR(SetString,(REFGUID k,LPCWSTR v),(k,v))
    ATTR(SetBlob,(REFGUID k,const UINT8* v,UINT32 n),(k,v,n))
    ATTR(SetUnknown,(REFGUID k,IUnknown* v),(k,v))
    ATTR(LockStore,(),())
    ATTR(UnlockStore,(),())
    ATTR(GetCount,(UINT32* v),(v))
    ATTR(GetItemByIndex,(UINT32 n,GUID* k,PROPVARIANT* v),(n,k,v))
    ATTR(CopyAllItems,(IMFAttributes* a),(a))
#undef ATTR
};

std::string Trim(std::string s){auto a=s.find_first_not_of(" \r\n\t");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \r\n\t")-a+1);}
std::string Resolve(const std::string& base,const std::string& relative){
    std::vector<wchar_t> out(32768);DWORD n=(DWORD)out.size();
    HRESULT hr=UrlCombineW(Utf8ToWide(base).c_str(),Utf8ToWide(relative).c_str(),out.data(),&n,URL_DONT_ESCAPE_EXTRA_INFO);
    if(FAILED(hr))throw Failure("Cannot resolve HLS URI");
    std::string url=WideToUtf8(out.data());
    if(url.rfind("http://",0)!=0&&url.rfind("https://",0)!=0)throw Failure("Non-HTTP HLS URI");return url;
}
using Attrs=std::map<std::string,std::string>;
Attrs Attributes(const std::string& text){
    Attrs out;size_t p=0;
    while(p<text.size()){
        auto eq=text.find('=',p);if(eq==text.npos)break;std::string key=Trim(text.substr(p,eq-p));p=eq+1;std::string value;
        if(p<text.size()&&text[p]=='"'){auto e=text.find('"',++p);if(e==text.npos)throw Failure("Unclosed HLS attribute");value=text.substr(p,e-p);p=e+1;}
        else{auto e=text.find(',',p);value=Trim(text.substr(p,e==text.npos?e:e-p));p=e==text.npos?text.size():e;}
        out[key]=value;if(p<text.size()&&text[p]==',')++p;
    }return out;
}
struct Segment {uint64_t seq=0,disc=0;std::string url;double duration=0;bool discontinuity=false;int64_t offset=-1,length=0;};
struct Playlist {std::string url;double target=6;bool end=false;std::vector<Segment> segments;std::vector<Attrs> variants,audio;};
Playlist Parse(const std::string& text,const std::string& url){
    Playlist out;out.url=url;std::istringstream lines(text);std::string line;bool header=false,pendingVariant=false,discontinuity=false;
    Attrs variant;uint64_t seq=0,disc=0;double duration=-1;std::string range;std::map<std::string,int64_t> rangeEnds;
    while(std::getline(lines,line)){
        line=Trim(line);if(line.empty())continue;
        if(!header){if(line.rfind("\xef\xbb\xbf",0)==0)line.erase(0,3);if(line!="#EXTM3U")throw Failure("Not an HLS playlist");header=true;continue;}
        auto value=[&]{return line.substr(line.find(':')+1);};
        if(line.rfind("#EXT-X-STREAM-INF:",0)==0){variant=Attributes(value());pendingVariant=true;}
        else if(line.rfind("#EXT-X-MEDIA:",0)==0){auto a=Attributes(value());if(a["TYPE"]=="AUDIO"&&a.count("URI")){a["url"]=Resolve(url,a["URI"]);out.audio.push_back(a);}}
        else if(line.rfind("#EXT-X-MEDIA-SEQUENCE:",0)==0)seq=std::stoull(value());
        else if(line.rfind("#EXT-X-DISCONTINUITY-SEQUENCE:",0)==0)disc=std::stoull(value());
        else if(line.rfind("#EXT-X-TARGETDURATION:",0)==0){out.target=std::stod(value());if(!(out.target>0&&out.target<=120))throw Failure("Invalid HLS target duration");}
        else if(line.rfind("#EXTINF:",0)==0){duration=std::stod(value());if(!(duration>0&&duration<=120))throw Failure("Invalid HLS segment duration");}
        else if(line.rfind("#EXT-X-KEY:",0)==0){if(Attributes(value())["METHOD"]!="NONE")throw Failure("Encrypted HLS unsupported in HLS-test1");}
        else if(line.rfind("#EXT-X-MAP:",0)==0)throw Failure("fMP4 HLS unsupported in HLS-test1");
        else if(line.rfind("#EXT-X-BYTERANGE:",0)==0)range=value();
        else if(line=="#EXT-X-ENDLIST")out.end=true;
        else if(line=="#EXT-X-DISCONTINUITY"){discontinuity=true;++disc;}
        else if(line[0]!='#'){
            std::string absolute=Resolve(url,line);
            if(pendingVariant){variant["url"]=absolute;out.variants.push_back(variant);pendingVariant=false;continue;}
            if(duration<0)throw Failure("HLS segment without EXTINF");
            Segment s;s.seq=seq+out.segments.size();s.disc=disc;s.duration=duration;s.url=absolute;s.discontinuity=discontinuity;
            if(!range.empty()){
                auto at=range.find('@');s.length=std::stoll(range.substr(0,at));
                if(at!=range.npos)s.offset=std::stoll(range.substr(at+1));
                else {if(!rangeEnds.count(absolute))throw Failure("HLS range without previous offset");s.offset=rangeEnds[absolute];}
                if(s.offset<0||s.length<=0||s.length>(int64_t)kHttpLimit||s.offset>LLONG_MAX-s.length)throw Failure("Invalid HLS byte range");
                rangeEnds[absolute]=s.offset+s.length;
            }
            out.segments.push_back(s);duration=-1;range.clear();discontinuity=false;
        }
    }
    if(out.segments.empty()&&out.variants.empty())throw Failure("Empty HLS playlist");return out;
}
std::string Choose(const Playlist& p){
    const Attrs* best=nullptr;uint64_t bestRate=UINT64_MAX;bool bestAudio=false;
    for(auto& v:p.variants){auto c=v.find("CODECS");std::string codecs=c==v.end()?"":c->second;bool audio=true;
        for(auto* video:{"avc1","hvc1","hev1","av01","vp09"})if(codecs.find(video)!=codecs.npos)audio=false;
        auto b=v.find("BANDWIDTH");uint64_t rate=b==v.end()?0:std::stoull(b->second);
        if(!best||(audio&&!bestAudio)||(audio==bestAudio&&rate<bestRate)){best=&v;bestRate=rate;bestAudio=audio;}}
    if(!best)throw Failure("No HLS rendition");
    auto group=best->find("AUDIO");const Attrs* alternative=nullptr;
    if(group!=best->end())for(auto& a:p.audio){auto g=a.find("GROUP-ID");if(g!=a.end()&&g->second==group->second){if(!alternative)alternative=&a;auto d=a.find("DEFAULT");if(d!=a.end()&&d->second=="YES"){alternative=&a;break;}}}
    return alternative?alternative->at("url"):best->at("url");
}
size_t StripId3(const std::vector<BYTE>& data){
    size_t p=0;
    while(p+3<=data.size()&&memcmp(data.data()+p,"ID3",3)==0){
        if(data.size()-p<10)throw Failure("Truncated ID3 header");auto* d=data.data()+p;
        for(int i=6;i<10;++i)if(d[i]&128)throw Failure("Invalid ID3 size");
        size_t size=10+((size_t)d[6]<<21)+((size_t)d[7]<<14)+((size_t)d[8]<<7)+d[9]+((d[5]&16)?10:0);
        if(size>data.size()-p)throw Failure("Truncated ID3 data");p+=size;
    }return p;
}
int Format(const BYTE* p,size_t n){
    if(n>=376&&p[0]==0x47&&p[188]==0x47)return 1;
    if(n>=2&&p[0]==255&&(p[1]&0xf6)==0xf0)return 2;
    if(n>=2&&p[0]==255&&(p[1]&0xe0)==0xe0)return 3;
    throw Failure("Unsupported HLS fragment container");
}
}

struct HlsInput::Impl {
    std::string url;
    LiveStream* stream;
    std::atomic<bool> stop{false};
    std::thread loader;
    std::mutex mutex;
    std::condition_variable cv;
    bool ready=false,done=false;
    HINTERNET request=nullptr;
    explicit Impl(std::string u):url(std::move(u)),stream(new LiveStream){}
    ~Impl(){Cancel();if(loader.joinable())loader.join();stream->Release();}
    void Cancel(){
        stop=true;stream->Close();
        {std::lock_guard<std::mutex> l(mutex);if(request){WinHttpCloseHandle(request);request=nullptr;}}
        cv.notify_all();
    }
    bool Wait(double seconds){std::unique_lock<std::mutex> l(mutex);return cv.wait_for(l,std::chrono::duration<double>(seconds),[&]{return stop.load();});}
    struct HttpReply {std::vector<BYTE> data;std::string url;};
    HttpReply GetOnce(const std::string& source,const Segment* segment){
        std::wstring w=Utf8ToWide(source);URL_COMPONENTS uc{};uc.dwStructSize=sizeof(uc);
        uc.dwHostNameLength=uc.dwUrlPathLength=uc.dwExtraInfoLength=(DWORD)-1;
        if(!WinHttpCrackUrl(w.c_str(),0,0,&uc)||(uc.nScheme!=INTERNET_SCHEME_HTTP&&uc.nScheme!=INTERNET_SCHEME_HTTPS))throw Failure("Invalid HLS HTTP URL");
        HINTERNET session=WinHttpOpen(L"RealisticRadio/HLS-test1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(!session)throw Failure("HLS WinHttpOpen failed");
        HINTERNET connection=nullptr,req=nullptr;
        auto cleanup=[&]{
            {std::lock_guard<std::mutex> l(mutex);if(req&&request==req){WinHttpCloseHandle(req);request=nullptr;}}
            if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
        };
        try{
            WinHttpSetTimeouts(session,5000,5000,5000,8000);
            DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
            WinHttpSetOption(session,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
            connection=WinHttpConnect(session,std::wstring(uc.lpszHostName,uc.dwHostNameLength).c_str(),uc.nPort,0);
            if(!connection)throw Failure("HLS connect failed");
            std::wstring path(uc.lpszUrlPath,uc.dwUrlPathLength);if(uc.dwExtraInfoLength)path.append(uc.lpszExtraInfo,uc.dwExtraInfoLength);if(path.empty())path=L"/";
            {std::lock_guard<std::mutex> l(mutex);if(stop)throw Failure("HLS cancelled");
                req=WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,uc.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
                request=req;}
            if(!req)throw Failure("HLS request creation failed");
            std::wstring headers=L"Accept: */*\r\n";
            if(segment&&segment->offset>=0)headers+=L"Range: bytes="+std::to_wstring(segment->offset)+L"-"+std::to_wstring(segment->offset+segment->length-1)+L"\r\n";
            if(!WinHttpSendRequest(req,headers.c_str(),(DWORD)-1,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(req,nullptr))throw Failure("HLS HTTP request failed: "+std::to_string(GetLastError()));
            DWORD status=0,n=sizeof(status);
            if(!WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX))throw Failure("HLS HTTP status missing");
            if(status!=200&&status!=206)throw Failure("HLS HTTP "+std::to_string(status));
            if(segment&&segment->offset>=0&&status!=206)throw Failure("HLS server ignored Range");
            HttpReply reply;reply.url=source;n=0;
            WinHttpQueryOption(req,WINHTTP_OPTION_URL,nullptr,&n);
            if(n&&n<65536){std::vector<wchar_t> finalUrl(n/sizeof(wchar_t)+1);if(WinHttpQueryOption(req,WINHTTP_OPTION_URL,finalUrl.data(),&n))reply.url=WideToUtf8(finalUrl.data());}
            BYTE block[16384];auto started=std::chrono::steady_clock::now();
            for(;;){
                if(stop)throw Failure("HLS cancelled");
                if(std::chrono::steady_clock::now()-started>std::chrono::seconds(25))throw Failure("HLS body download deadline");
                DWORD got=0;if(!WinHttpReadData(req,block,sizeof(block),&got))throw Failure("HLS body read failed: "+std::to_string(GetLastError()));
                if(!got)break;if(reply.data.size()+got>kHttpLimit)throw Failure("HLS response exceeded 4 MB limit");
                reply.data.insert(reply.data.end(),block,block+got);
            }
            if(segment&&segment->offset>=0&&reply.data.size()!=(size_t)segment->length)throw Failure("HLS byte range length mismatch");
            cleanup();return reply;
        }catch(...){cleanup();throw;}
    }
    HttpReply Get(const std::string& source,const Segment* segment=nullptr){
        for(int attempt=0;;++attempt){
            if(stop)throw Failure("HLS cancelled");
            try{return GetOnce(source,segment);}catch(const std::exception& e){if(stop||attempt==2)throw;Log::Write("HLS retry %d: %s",attempt+1,e.what());if(Wait(.25*(attempt+1)))throw Failure("HLS cancelled");}
        }
    }
    Playlist Manifest(const std::string& source){auto r=Get(source);return Parse(std::string(r.data.begin(),r.data.end()),r.url);}
    void Run(){
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{
            auto p=Manifest(url);std::map<std::string,bool> visited;
            for(int depth=0;!p.variants.empty();++depth){if(depth>=4||visited[p.url])throw Failure("HLS master cycle/depth limit");visited[p.url]=true;p=Manifest(Choose(p));}
            bool initial=true,haveLast=false;uint64_t last=0,lastDisc=0;int format=0;uint64_t count=0;
            std::map<uint64_t,std::string> seen;
            while(!stop){
                auto started=std::chrono::steady_clock::now();size_t start=0;bool added=false;
                if(initial&&!p.end){double behind=0;start=p.segments.size();while(start&&behind<p.target*3)behind+=p.segments[--start].duration;}
                for(size_t i=start;i<p.segments.size()&&!stop;++i){
                    const auto& s=p.segments[i];std::string identity=s.url+"|"+std::to_string(s.offset)+"|"+std::to_string(s.length);
                    auto old=seen.find(s.seq);if(old!=seen.end()){if(old->second!=identity)throw Failure("HLS sequence URI changed");continue;}
                    if(haveLast){
                        if(s.seq<=last)continue;
                        if(s.seq!=last+1)throw Failure("HLS live window skipped a segment; reconnecting");
                        // End this reader and let StreamSession reconnect with a fresh decoder.
                        if(s.discontinuity||s.disc!=lastDisc)throw Failure("HLS discontinuity; restarting decoder");
                    }
                    auto data=Get(s.url,&s);size_t skip=StripId3(data.data);int fmt=Format(data.data.data()+skip,data.data.size()-skip);
                    if(format&&fmt!=format)throw Failure("HLS container changed; restarting decoder");
                    if(!format){format=fmt;stream->Format(fmt==1?L"hls-live.ts":fmt==2?L"hls-live.aac":L"hls-live.mp3",fmt==1?L"video/mp2t":fmt==2?L"audio/aac":L"audio/mpeg");}
                    if(!stream->Append(data.data.data()+skip,data.data.size()-skip))break;
                    {std::lock_guard<std::mutex> l(mutex);ready=true;}cv.notify_all();
                    last=s.seq;lastDisc=s.disc;haveLast=true;added=true;seen[s.seq]=identity;
                    while(seen.size()>100)seen.erase(seen.begin());
                    ++count;if(count==1||count%10==0)Log::Write("HLS segment %llu, sequence %llu, container %s, bytes %zu",(unsigned long long)count,(unsigned long long)s.seq,format==1?"TS":format==2?"AAC":"MP3",data.data.size()-skip);
                }
                initial=false;if(p.end){stream->Finish();break;}
                double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                if(Wait(std::max(.1,(added?p.target:p.target/2)-elapsed)))break;
                p=Manifest(p.url);
            }
        }catch(const std::exception& e){if(!stop)Log::Write("HLS stopped: %s (%s)",e.what(),url.c_str());stream->Finish(E_FAIL);}
        {std::lock_guard<std::mutex> l(mutex);done=true;}cv.notify_all();
        CoUninitialize();
    }
};
HlsInput::HlsInput(std::string u):p_(new Impl(std::move(u))){}
HlsInput::~HlsInput()=default;
void HlsInput::Start(){p_->loader=std::thread([this]{p_->Run();});}
bool HlsInput::WaitReady(){std::unique_lock<std::mutex> l(p_->mutex);p_->cv.wait(l,[&]{return p_->ready||p_->done||p_->stop.load();});return p_->ready&&!p_->stop;}
IMFByteStream* HlsInput::Stream() const{return p_->stream;}
void HlsInput::Cancel(){p_->Cancel();}
void HlsInput::Join(){if(p_->loader.joinable())p_->loader.join();}
