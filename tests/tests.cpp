#include "../src/core.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>
using namespace app;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F> void rejects(F f){bool failed=false;try{f();}catch(...){failed=true;}require(failed,"expected rejection");}
int wmain(int argc,wchar_t** argv){try{
    if(argc>=2&&std::wstring(argv[1])==L"--child"){
        if(argc<4)return 9;
        if(std::wstring(argv[2])==L"sleep"){Sleep(10000);return 0;}
        if(std::wstring(argv[3])!=L"spazi è \"quote\" e slash\\")return 8;
        DWORD written;const char line[]="UTF-8: \xC3\xA8\nlast line";
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),line,sizeof(line)-1,&written,nullptr);return 1;
    }
    auto bytes=reinterpret_cast<const unsigned char*>("123456789");
    require((crcUpdate(0xFFFFFFFFu,bytes,9)^0xFFFFFFFFu)==0xCBF43926u,"CRC vector");
    uint32_t crc=crcUpdate(0xFFFFFFFFu,bytes,4);crc=crcUpdate(crc,bytes+4,5);require((crc^0xFFFFFFFFu)==0xCBF43926u,"CRC streaming");
    require(quote(L"a b\\")==L"\"a b\\\\\"","quote trailing slash");
    require(quote(L"a\"b")==L"\"a\\\"b\"","quote embedded quote");
    require(expand(L"{name}-{n}-{crc}",L"{crc}",2,L"ABC")==L"{crc}-2-ABC","token injection");
    require(fileName(L"Film [ABCDEF12]")==L"Film [ABCDEF12].mkv","name");
    rejects([]{fileName(L"CON");});rejects([]{fileName(L"../film");});rejects([]{fileName(L"film.");});
    auto rules=parseRules(L"a1|Italiano|ita|1|0\r\ns2|<vuoto>||0|\r\n");require(rules.size()==2&&rules[0].language==L"ita","rules");
    rejects([]{parseRules(L"a0|x|ita|1|0");});rejects([]{parseRules(L"a1|x|ita|3|0");});
    std::atomic_bool cancel=false;
    std::wstring captured;
    auto self=fs::absolute(argv[0]);
    require(run(self,{L"--child",L"echo",L"spazi è \"quote\" e slash\\"},cancel,[&](const auto& s){captured+=s;})==1,"process exit code");
    require(captured==L"UTF-8: èlast line","process UTF8 and final unterminated output");
    std::thread stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(200));cancel=true;});
    bool aborted=false;try{run(self,{L"--child",L"sleep",L"x"},cancel,[](auto){});}catch(...){aborted=true;}stop.join();require(aborted,"active process cancellation");cancel=false;
    if(argc>=3){
        fs::path root=fs::absolute(argv[2]);fs::create_directories(root);
        fs::path fixture=root/L"sorgente è [test].mkv";
        // Tiny valid PCM WAV, then wrap it with real mkvmerge. No downloaded media.
        fs::path wav=root/L"fixture.wav";std::ofstream f(wav,std::ios::binary);
        auto u16=[&](uint16_t v){f.put(static_cast<char>(v));f.put(static_cast<char>(v>>8));};
        auto u32=[&](uint32_t v){u16(static_cast<uint16_t>(v));u16(static_cast<uint16_t>(v>>16));};
        f.write("RIFF",4);u32(16036);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);f.write("data",4);u32(16000);for(int i=0;i<8000;++i)u16(0);f.close();
        auto log=[](auto s){std::wcout<<s<<L'\n';};
        require(run(fs::path(argv[1])/L"mkvmerge.exe",{L"-o",fixture.wstring(),wav.wstring()},cancel,log)<=1,"fixture remux");
        auto original=crcFile(fixture,cancel);
        Options o;o.tools=argv[1];o.output=root/L"remux";o.setTitle=true;o.title=L"Titolo è {n}";o.rules=parseRules(L"a1|Italiano|ita|1|0");
        auto target=process(fixture,1,o,cancel,log);require(target.stem().wstring().find(crcFile(target,cancel))!=std::wstring::npos,"final CRC remux");
        require(crcFile(fixture,cancel)==original,"original unchanged");
        auto existing=crcFile(target,cancel);o.remux=false;o.output=root/L"copy";auto copy=process(fixture,2,o,cancel,log);require(copy.stem().wstring().find(crcFile(copy,cancel))!=std::wstring::npos,"final CRC copy");
        std::wstring json;
        auto identify=run(o.tools/L"mkvmerge.exe",{L"--output-charset",L"UTF-8",L"-J",copy.wstring()},cancel,[&](auto s){json+=s;});require(identify<=1,"identify output");
        require(json.find(L"Titolo è 2")!=json.npos,"edited title persisted");
        require(json.find(L"Italiano")!=json.npos&&json.find(L"\"language\": \"ita\"")!=json.npos,"track properties persisted");
        // Fixed title gives deterministic bytes in copy mode, thus the exact same target collision.
        o.title=L"Fixed";o.output=root/L"collision";auto first=process(fixture,3,o,cancel,log);auto before=crcFile(first,cancel);
        rejects([&]{process(fixture,3,o,cancel,log);});require(crcFile(first,cancel)==before,"collision preserves existing");
        require(crcFile(target,cancel)==existing,"previous output unchanged");
        o.rules=parseRules(L"a9|Missing|ita|1|0");o.output=root/L"bad";rejects([&]{process(fixture,4,o,cancel,log);});require(fs::is_empty(o.output),"failure cleans temporary");
        cancel=true;rejects([&]{process(fixture,1,o,cancel,log);});rejects([&]{crcFile(target,cancel);});
    }
    std::wcout<<L"\nPASS: tutti i test superati\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
