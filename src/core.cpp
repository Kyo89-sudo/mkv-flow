#include "core.h"
#include <array>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <stdexcept>
#include <algorithm>
namespace app {
namespace {
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):h(value){}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
void checkCancel(std::atomic_bool& c){if(c) throw std::runtime_error("Operazione annullata");}
void replace(std::wstring& s,const std::wstring& from,const std::wstring& to){
    size_t p=0; while((p=s.find(from,p))!=s.npos){s.replace(p,from.size(),to);p+=to.size();}
}
std::wstring utf8(const std::string& s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring w(n,L' '); MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
}
std::wstring quote(const std::wstring& s){
    std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:s){
        if(c==L'\\'){++slashes;continue;}
        out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;
        if(c==L'"')out+=L'\\';out+=c;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
std::vector<Rule> parseRules(const std::wstring& text){
    std::vector<Rule> rules;std::wistringstream lines(text);std::wstring line;
    while(std::getline(lines,line)){
        if(!line.empty()&&line.back()==L'\r')line.pop_back();
        if(line.empty())continue;
        std::vector<std::wstring> fields;size_t start=0,p;
        while((p=line.find(L'|',start))!=line.npos){fields.push_back(line.substr(start,p-start));start=p+1;}
        fields.push_back(line.substr(start));
        if(fields.size()!=5 || !std::regex_match(fields[0],std::wregex(L"[avs]?[1-9][0-9]*")))
            throw std::runtime_error("Regole: usare selettore|nome|lingua|default|forced (es. a1|Italiano|ita|1|0)");
        for(int i:{3,4})if(!fields[i].empty()&&fields[i]!=L"0"&&fields[i]!=L"1")throw std::runtime_error("Flag: usare 0, 1 o campo vuoto");
        if(!fields[2].empty()&&!std::regex_match(fields[2],std::wregex(L"[A-Za-z0-9-]+")))throw std::runtime_error("Codice lingua non valido");
        rules.push_back({fields[0],fields[1],fields[2],fields[3],fields[4]});
    }return rules;
}
std::wstring expand(std::wstring text,const std::wstring& name,size_t index,const std::wstring& crc){
    // Replace the original tokens in a single pass: input names may themselves contain braces.
    std::wstring out;
    for(size_t p=0;p<text.size();){
        if(text.compare(p,6,L"{name}")==0){out+=name;p+=6;}
        else if(text.compare(p,3,L"{n}")==0){out+=std::to_wstring(index);p+=3;}
        else if(text.compare(p,5,L"{crc}")==0){out+=crc;p+=5;}
        else out+=text[p++];
    }return out;
}
std::wstring fileName(const std::wstring& text){
    if(text.empty()||text.size()>220||text==L"."||text==L".."||text.back()==L'.'||text.back()==L' ')
        throw std::runtime_error("Nome finale vuoto, troppo lungo o con punto/spazio finale");
    for(wchar_t c:text)if(c<32||std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos)throw std::runtime_error("Carattere non valido nel nome finale");
    auto base=text.substr(0,text.find(L'.'));std::transform(base.begin(),base.end(),base.begin(),towupper);
    if(std::regex_match(base,std::wregex(L"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]")))throw std::runtime_error("Nome riservato da Windows");
    return text+L".mkv";
}
uint32_t crcUpdate(uint32_t crc,const unsigned char* data,size_t size){
    static const auto table=[](){std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;++i){uint32_t v=i;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xEDB88320u:0u);t[i]=v;}return t;}();
    for(size_t i=0;i<size;++i)crc=table[(crc^data[i])&255]^(crc>>8);return crc;
}
std::wstring crcFile(const fs::path& path,std::atomic_bool& cancel){
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(f.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Impossibile aprire file per CRC32");
    std::vector<unsigned char> buffer(1024*1024);DWORD n;uint32_t crc=0xFFFFFFFFu;
    for(;;){checkCancel(cancel);if(!ReadFile(f.h,buffer.data(),static_cast<DWORD>(buffer.size()),&n,nullptr))throw std::runtime_error("Errore lettura CRC32");if(!n)break;crc=crcUpdate(crc,buffer.data(),n);}
    std::wostringstream s;s<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill(L'0')<<(crc^0xFFFFFFFFu);return s.str();
}
DWORD run(const fs::path& exe,const std::vector<std::wstring>& args,std::atomic_bool& cancel,Log log){
    checkCancel(cancel);std::wstring cmd=quote(exe.wstring());for(const auto& a:args)cmd+=L" "+quote(a);
    if(cmd.size()>32000)throw std::runtime_error("Comando troppo lungo: ridurre le regole");
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd=nullptr,wr=nullptr;
    if(!CreatePipe(&rd,&wr,&sa,0))throw std::runtime_error("CreatePipe fallita");
    Handle read(rd),write(wr);SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
    Handle nullIn(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    // Restrict inheritance to the three standard handles, even if the embedding process has others.
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> attributes(bytes);
    auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw std::runtime_error("Attributi processo non disponibili");
    struct AttrGuard{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttrGuard(){DeleteProcThreadAttributeList(p);}} guard{list};
    HANDLE inherited[]={wr,nullIn.h};
    if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error("Handle processo non disponibili");
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput=si.StartupInfo.hStdError=wr;si.StartupInfo.hStdInput=nullIn.h;si.lpAttributeList=list;
    // Kill any child process too if the batch is cancelled or the parent terminates.
    Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job.h||!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Job Windows non disponibile");
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi))
        throw std::runtime_error("Avvio MKVToolNix fallito (errore Windows "+std::to_string(GetLastError())+"): controllare percorso e dipendenze");
    Handle process(pi.hProcess),thread(pi.hThread);
    if(!AssignProcessToJobObject(job.h,pi.hProcess)){TerminateProcess(pi.hProcess,2);WaitForSingleObject(pi.hProcess,INFINITE);throw std::runtime_error("Impossibile associare processo al job");}
    ResumeThread(pi.hThread);CloseHandle(write.h);write.h=INVALID_HANDLE_VALUE;
    std::string pending;
    for(;;){
        if(cancel){TerminateJobObject(job.h,2);WaitForSingleObject(pi.hProcess,INFINITE);throw std::runtime_error("Operazione annullata");}
        DWORD available=0;
        if(PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr)&&available){
            char buf[4096];DWORD n=0;if(ReadFile(rd,buf,(std::min)(available,DWORD(sizeof(buf))),&n,nullptr)&&n){
                pending.append(buf,n);size_t p;
                while((p=pending.find_first_of("\r\n"))!=pending.npos){if(p)log(utf8(pending.substr(0,p)));pending.erase(0,p+1);}
                if(pending.size()>16384){log(utf8(pending));pending.clear();}
            }continue;
        }
        if(WaitForSingleObject(pi.hProcess,40)==WAIT_OBJECT_0){
            // A final write may have arrived between the first peek and process exit.
            DWORD remaining=0;if(PeekNamedPipe(rd,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;break;
        }
    }
    if(!pending.empty())log(utf8(pending));DWORD code=2;GetExitCodeProcess(pi.hProcess,&code);return code;
}
fs::path process(const fs::path& input,size_t index,const Options& o,std::atomic_bool& cancel,Log log){
    checkCancel(cancel);fs::create_directories(o.output);
    wchar_t temp[MAX_PATH];if(!GetTempFileNameW(o.output.c_str(),L"mkf",0,temp))throw std::runtime_error("Impossibile creare temporaneo nella cartella output (percorso troppo lungo o permessi)");
    fs::path stage(temp);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove(p,e);}} cleanup{stage};
    bool warning=false;
    auto execute=[&](const wchar_t* tool,std::vector<std::wstring> args){
        DWORD rc=run(o.tools/tool,args,cancel,log);if(rc>1)throw std::runtime_error("MKVToolNix ha segnalato un errore");if(rc==1)warning=true;
    };
    if(o.remux){
        // The unique temporary is reserved by this process; mkvmerge may replace only this file.
        log(L"Ripacchettamento...");execute(L"mkvmerge.exe",{L"--output-charset",L"UTF-8",L"-o",stage.wstring(),input.wstring()});
    }else{
        log(L"Copia di lavoro...");
        auto callback=[](LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID data)->DWORD{return static_cast<std::atomic_bool*>(data)->load()?PROGRESS_CANCEL:PROGRESS_CONTINUE;};
        if(!CopyFileExW(input.c_str(),stage.c_str(),callback,&cancel,nullptr,0)){checkCancel(cancel);throw std::runtime_error("Copia fallita");}
    }
    checkCancel(cancel);
    std::vector<std::wstring> edits{L"--output-charset",L"UTF-8",stage.wstring()};
    if(o.setTitle){edits.insert(edits.end(),{L"--edit",L"info",L"--set",L"title="+expand(o.title,input.stem().wstring(),index,L"")});}
    for(const auto& r:o.rules){
        edits.insert(edits.end(),{L"--edit",L"track:"+r.selector});
        auto set=[&](const std::wstring& key,const std::wstring& value){if(!value.empty())edits.insert(edits.end(),{L"--set",key+L"="+value});};
        if(r.name==L"<vuoto>")edits.insert(edits.end(),{L"--delete",L"name"});else set(L"name",r.name);
        set(L"language",r.language);set(L"flag-default",r.def);set(L"flag-forced",r.forced);
    }
    if(o.setTitle||!o.rules.empty()){log(L"Modifica intestazioni...");execute(L"mkvpropedit.exe",edits);}
    checkCancel(cancel);log(L"Calcolo CRC32 sul file finale...");auto crc=crcFile(stage,cancel);
    auto target=o.output/fileName(expand(o.pattern,input.stem().wstring(),index,crc));
    checkCancel(cancel);
    // Same-directory rename, with NO replace flag: collision and original files are protected atomically.
    if(!MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Rinomina fallita: nome esistente, percorso troppo lungo o permessi");
    log(L"CRC32: "+crc+(warning?L" — COMPLETATO CON AVVISI: verificare il log":L" — completato"));return target;
}
}
