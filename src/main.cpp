#include "core.h"
#include "version.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <thread>
#include <memory>
#include <algorithm>
#include <fstream>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace {
using namespace app;
enum { Add=100, Remove, Clear, Inspect, Tools, ToolsBrowse, Output, OutputBrowse, Pattern, SetTitle, Title, Remux, Rules, Start, Cancel, Queue, LogBox, Status, RuleType, RuleName, RuleLang, RuleDefault, RuleForced, RuleAdd, NavQueue=200, NavTracks, NavSettings, NavLog };
constexpr UINT LogMessage=WM_APP+1, DoneMessage=WM_APP+2;
HWND window; HINSTANCE instance; HFONT font;int dpi=96;
std::vector<fs::path> files;std::thread worker;std::atomic_bool cancelled=false;bool busy=false,closeAfter=false;
fs::path settings;
int currentPage=0, buildingPage=-1;
struct Placement {HWND h;int x,y,w,hgt;bool stretch;int page;};std::vector<Placement> controls;
HWND control(int id){return GetDlgItem(window,id);}
std::wstring get(int id){int n=GetWindowTextLengthW(control(id));std::wstring s(n+1,L'\0');GetWindowTextW(control(id),s.data(),n+1);s.resize(n);return s;}
bool checked(int id){return SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED;}
void put(int id,const std::wstring& s){SetWindowTextW(control(id),s.c_str());}
void emit(const std::wstring& s){auto p=new std::wstring(s);if(!PostMessageW(window,LogMessage,0,reinterpret_cast<LPARAM>(p)))delete p;}
void logText(const std::wstring& text){
    HWND h=control(LogBox);if(GetWindowTextLengthW(h)>180000)SetWindowTextW(h,L"[Log precedente abbreviato]\r\n");
    SendMessageW(h,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));auto line=text+L"\r\n";SendMessageW(h,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(line.c_str()));
}
HWND make(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0,bool stretch=false){
    HWND c=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls.push_back({c,x,y,w,h,stretch,buildingPage});return c;
}
void label(const wchar_t* text,int x,int y,int w){make(L"STATIC",text,0,x,y,w,20);}
void showPage(int page){
    currentPage=page;
    for(const auto& c:controls)ShowWindow(c.h,c.page<0||c.page==page?SW_SHOW:SW_HIDE);
    for(int i=0;i<4;++i)SendMessageW(control(NavQueue+i),BM_SETCHECK,i==page?BST_CHECKED:BST_UNCHECKED,0);
}
void layout(){
    RECT r;GetClientRect(window,&r);int dx=MulDiv(r.right,96,dpi)-640,dy=MulDiv(r.bottom,96,dpi)-460;
    for(const auto& c:controls){int x=c.x,y=c.y,h=c.hgt;
        if(c.page<0&&y>=410)y+=dy;
        if(c.page==0){if(GetDlgCtrlID(c.h)==Queue)h+=dy;else if(y>=266)y+=dy;}
        if(c.page==1&&GetDlgCtrlID(c.h)==Rules)h+=dy;
        if(c.page==3&&GetDlgCtrlID(c.h)==LogBox)h+=dy;
        if(!c.stretch&&c.x+c.w>=624)x+=dx;
        MoveWindow(c.h,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(c.w+(c.stretch?dx:0),dpi,96),MulDiv(h,dpi,96),TRUE);
    }
}
void setBusy(bool value){busy=value;for(const auto& c:controls)if(GetDlgCtrlID(c.h)>0)EnableWindow(c.h,!value);EnableWindow(control(LogBox),TRUE);for(int id=NavQueue;id<=NavLog;++id)EnableWindow(control(id),TRUE);EnableWindow(control(Cancel),value);put(Status,value?L"Elaborazione in corso...":L"Pronto");}
void refresh(){SendMessageW(control(Queue),LB_RESETCONTENT,0,0);for(const auto& p:files)SendMessageW(control(Queue),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.c_str()));put(Status,std::to_wstring(files.size())+L" file in coda");}
void addFile(const fs::path& p){
    auto ext=p.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
    std::error_code ec;if(ext!=L".mkv"||!fs::is_regular_file(p,ec))return;
    auto absolute=fs::absolute(p).lexically_normal();
    if(std::none_of(files.begin(),files.end(),[&](auto& f){return _wcsicmp(f.c_str(),absolute.c_str())==0;}))files.push_back(absolute);
}
void addPath(const fs::path& p){std::error_code ec;if(fs::is_directory(p,ec)){for(fs::directory_iterator it(p,ec),end;!ec&&it!=end;it.increment(ec))addFile(it->path());}else addFile(p);}
void chooseFiles(){
    std::vector<wchar_t> buf(65536);OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFilter=L"Matroska (*.mkv)\0*.mkv\0\0";o.lpstrFile=buf.data();o.nMaxFile=static_cast<DWORD>(buf.size());o.Flags=OFN_ALLOWMULTISELECT|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&o)){fs::path first(buf.data());auto p=buf.data()+wcslen(buf.data())+1;if(!*p)addFile(first);else for(;*p;p+=wcslen(p)+1)addFile(first/p);refresh();}
}
std::wstring folder(){
    IFileDialog* dialog=nullptr;std::wstring result;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
        DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){result=path;CoTaskMemFree(path);}item->Release();}}dialog->Release();
    }return result;
}
std::wstring readSetting(const wchar_t* key,const wchar_t* fallback){std::vector<wchar_t> b(32768);GetPrivateProfileStringW(L"Settings",key,fallback,b.data(),static_cast<DWORD>(b.size()),settings.c_str());return b.data();}
void saveSettings(){for(auto pair:{std::pair{Tools,L"Tools"},std::pair{Output,L"Output"},std::pair{Pattern,L"Pattern"}})WritePrivateProfileStringW(L"Settings",pair.second,get(pair.first).c_str(),settings.c_str());}
Options options(){
    Options o;o.tools=get(Tools);o.output=get(Output);o.pattern=get(Pattern);o.title=get(Title);o.setTitle=checked(SetTitle);o.remux=checked(Remux);o.rules=parseRules(get(Rules));
    if(!o.tools.is_absolute()||!fs::is_regular_file(o.tools/L"mkvmerge.exe")||!fs::is_regular_file(o.tools/L"mkvpropedit.exe"))throw std::runtime_error("Selezionare la cartella MKVToolNix con mkvmerge.exe e mkvpropedit.exe");
    if(!o.output.is_absolute())throw std::runtime_error("Scegliere un percorso assoluto per la cartella output");
    if(o.pattern.find(L"{crc}")==o.pattern.npos)throw std::runtime_error("Il modello nome deve contenere {crc}");
    auto validateTokens=[](std::wstring s,bool title){for(auto token:{L"{name}",L"{n}",L"{crc}"}){if(title&&std::wstring(token)==L"{crc}")continue;size_t p;while((p=s.find(token))!=s.npos)s.erase(p,wcslen(token));}if(s.find_first_of(L"{}")!=s.npos)throw std::runtime_error("Token sconosciuto: usare {name}, {n}, {crc}; il titolo non accetta {crc}");};
    validateTokens(o.pattern,false);if(o.setTitle)validateTokens(o.title,true);
    for(size_t i=0;i<files.size();++i)fileName(expand(o.pattern,files[i].stem().wstring(),i+1,L"00000000"));return o;
}
std::wstring errorText(const std::exception& e){std::string s=e.what();int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);if(!w.empty())w.pop_back();return w;}
void launch(bool inspect){
    if(files.empty())throw std::runtime_error("Aggiungere almeno un file MKV");
    Options o;
    fs::path input;
    if(inspect){o.tools=get(Tools);if(!o.tools.is_absolute()||!fs::exists(o.tools/L"mkvmerge.exe"))throw std::runtime_error("Impostare la cartella MKVToolNix");int index=static_cast<int>(SendMessageW(control(Queue),LB_GETCURSEL,0,0));input=files[index==LB_ERR?0:index];}
    else o=options();
    if(inspect)showPage(3);
    saveSettings();auto batch=files;cancelled=false;setBusy(true);SetWindowTextW(control(LogBox),L"");
    try{worker=std::thread([o,batch,input,inspect]{
        size_t ok=0,failed=0;
        try{
            if(inspect){emit(L"Analisi: "+input.wstring());auto rc=run(o.tools/L"mkvmerge.exe",{L"--output-charset",L"UTF-8",L"-J",input.wstring()},cancelled,emit);if(rc>1)throw std::runtime_error("Analisi MKV fallita");}
            else for(size_t i=0;i<batch.size()&&!cancelled;++i){
                emit(L"\r\n["+std::to_wstring(i+1)+L"/"+std::to_wstring(batch.size())+L"] "+batch[i].wstring());
                try{auto target=process(batch[i],i+1,o,cancelled,emit);++ok;emit(L"Salvato: "+target.wstring());}
                catch(const std::exception& e){++failed;emit(L"ERRORE: "+errorText(e));}
            }
        }catch(const std::exception& e){emit(L"ERRORE: "+errorText(e));}
        if(!inspect)emit(L"\r\nRiepilogo: "+std::to_wstring(ok)+L" completati, "+std::to_wstring(failed)+L" falliti; "+(cancelled?L"batch annullato":L"fine batch"));
        PostMessageW(window,DoneMessage,0,0);
    });}catch(...){setBusy(false);throw;}
}
void addRule(){
    auto selector=get(RuleType);auto name=get(RuleName);auto lang=get(RuleLang);
    int d=static_cast<int>(SendMessageW(control(RuleDefault),CB_GETCURSEL,0,0));int f=static_cast<int>(SendMessageW(control(RuleForced),CB_GETCURSEL,0,0));
    if(name.find_first_of(L"|\r\n")!=name.npos||lang.find_first_of(L"|\r\n")!=lang.npos)throw std::runtime_error("I campi non possono contenere | o ritorni a capo");
    std::wstring line=selector+L"|"+name+L"|"+lang+L"|"+(d==0?L"":d==1?L"1":L"0")+L"|"+(f==0?L"":f==1?L"1":L"0");
    parseRules(line);auto current=get(Rules);if(!current.empty())current+=L"\r\n";put(Rules,current+line);
}
void build(){
    dpi=GetDpiForSystem();font=CreateFontW(-MulDiv(14,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    label(L"MKV Flow " MKVFLOW_VERSION_W L"  /  Modifica, rinomina, verifica",12,12,610);
    int nav=NavQueue;for(auto text:{L"Coda",L"Tracce",L"Impostazioni",L"Log"}){make(L"BUTTON",text,nav,12+(nav-NavQueue)*154,40,148,28,WS_TABSTOP|BS_AUTORADIOBUTTON|BS_PUSHLIKE|(nav==NavQueue?WS_GROUP:0));++nav;}
    buildingPage=0;
    make(L"BUTTON",L"Aggiungi MKV...",Add,12,80,120,28,WS_TABSTOP);
    make(L"BUTTON",L"Rimuovi",Remove,140,80,80,28,WS_TABSTOP);
    make(L"BUTTON",L"Svuota",Clear,228,80,72,28,WS_TABSTOP);
    make(L"BUTTON",L"Analizza",Inspect,308,80,90,28,WS_TABSTOP);
    label(L"Trascina qui file MKV o cartelle (non ricorsive).",12,114,610);
    make(L"LISTBOX",L"",Queue,12,134,612,118,WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_BORDER,true);
    label(L"Output",12,270,76);make(L"EDIT",L"",Output,88,266,434,26,WS_TABSTOP|ES_AUTOHSCROLL,true);make(L"BUTTON",L"Sfoglia...",OutputBrowse,530,266,94,26,WS_TABSTOP);
    label(L"Nome",12,304,76);make(L"EDIT",L"{name} [{crc}]",Pattern,88,300,536,26,WS_TABSTOP|ES_AUTOHSCROLL,true);
    make(L"BUTTON",L"Remux (disattiva per copia + modifica)",Remux,12,338,500,24,WS_TABSTOP|BS_AUTOCHECKBOX);SendMessageW(control(Remux),BM_SETCHECK,BST_CHECKED,0);
    label(L"Nome: {name}  |  Indice: {n}  |  CRC32: {crc}  |  .mkv automatico",12,374,612);
    buildingPage=1;
    make(L"BUTTON",L"Imposta titolo",SetTitle,12,84,138,24,WS_TABSTOP|BS_AUTOCHECKBOX);make(L"EDIT",L"{name}",Title,158,82,466,26,WS_TABSTOP|ES_AUTOHSCROLL,true);
    label(L"Regole comuni al batch: a1 = primo audio, s1 = primi sottotitoli, v1 = primo video.",12,120,612);
    label(L"Selettore",12,150,68);label(L"Nome traccia",88,150,180);label(L"Lingua",280,150,68);label(L"Predefinita",356,150,110);label(L"Forzata",480,150,110);
    make(L"EDIT",L"a1",RuleType,12,172,64,26,WS_TABSTOP|ES_AUTOHSCROLL);make(L"EDIT",L"",RuleName,88,172,180,26,WS_TABSTOP|ES_AUTOHSCROLL);make(L"EDIT",L"ita",RuleLang,280,172,64,26,WS_TABSTOP|ES_AUTOHSCROLL);
    for(int id:{RuleDefault,RuleForced}){make(L"COMBOBOX",L"",id,id==RuleDefault?356:480,172,112,150,WS_TABSTOP|CBS_DROPDOWNLIST);for(auto text:{L"Conserva",L"Sì",L"No"})SendMessageW(control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(control(id),CB_SETCURSEL,0,0);}
    make(L"BUTTON",L"Aggiungi regola",RuleAdd,12,210,142,28,WS_TABSTOP);
    label(L"selettore|nome|lingua|default|forced   —   vuoto = conserva; <vuoto> rimuove il nome",12,246,612);
    make(L"EDIT",L"",Rules,12,270,612,124,WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,true);
    buildingPage=2;
    label(L"Cartella MKVToolNix",12,90,610);
    make(L"EDIT",L"",Tools,12,116,502,26,WS_TABSTOP|ES_AUTOHSCROLL,true);make(L"BUTTON",L"Sfoglia...",ToolsBrowse,524,116,100,26,WS_TABSTOP);
    make(L"STATIC",L"Seleziona la cartella con mkvmerge.exe e mkvpropedit.exe.\r\nMKVToolNix resta esterno al programma.",0,12,160,612,50);
    make(L"STATIC",L"Gli originali restano intatti. Il CRC32 viene calcolato dopo tutte le modifiche.\r\nI nomi esistenti non vengono sovrascritti.",0,12,224,612,50);
    buildingPage=3;
    make(L"EDIT",L"",LogBox,12,84,612,310,WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,true);SendMessageW(control(LogBox),EM_SETLIMITTEXT,220000,0);
    buildingPage=-1;
    make(L"BUTTON",L"Avvia batch",Start,12,414,112,32,WS_TABSTOP|BS_DEFPUSHBUTTON);make(L"BUTTON",L"Annulla",Cancel,132,414,90,32,WS_TABSTOP);EnableWindow(control(Cancel),FALSE);
    make(L"STATIC",L"Pronto",Status,236,422,388,22,0,true);
    PWSTR local=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){settings=fs::path(local)/L"MKVFlow";CoTaskMemFree(local);fs::create_directories(settings);settings/=L"settings.ini";
        if(!fs::exists(settings)){std::ofstream ini(settings,std::ios::binary);ini.put(static_cast<char>(0xFF));ini.put(static_cast<char>(0xFE));}}
    put(Tools,readSetting(L"Tools",L"C:\\Program Files\\MKVToolNix"));put(Output,readSetting(L"Output",L""));put(Pattern,readSetting(L"Pattern",L"{name} [{crc}]"));
    DragAcceptFiles(window,TRUE);showPage(0);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    try{switch(msg){
    case WM_CREATE:window=hwnd;build();return 0;
    case WM_SIZE:layout();return 0;
    case WM_GETMINMAXINFO:{RECT bounds{0,0,MulDiv(640,dpi,96),MulDiv(460,dpi,96)};AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0);reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={bounds.right-bounds.left,bounds.bottom-bounds.top};return 0;}
    case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(w);if(!busy){UINT n=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);for(UINT i=0;i<n;++i){std::vector<wchar_t> p(DragQueryFileW(drop,i,nullptr,0)+1);DragQueryFileW(drop,i,p.data(),static_cast<UINT>(p.size()));addPath(p.data());}refresh();}DragFinish(drop);return 0;}
    case WM_COMMAND:if(HIWORD(w)>1)return 0;switch(LOWORD(w)){
        case NavQueue:case NavTracks:case NavSettings:case NavLog:showPage(LOWORD(w)-NavQueue);break;
        case Add:chooseFiles();break;
        case Clear:files.clear();refresh();break;
        case Remove:{int i=static_cast<int>(SendMessageW(control(Queue),LB_GETCURSEL,0,0));if(i!=LB_ERR)files.erase(files.begin()+i);refresh();break;}
        case ToolsBrowse:case OutputBrowse:{auto p=folder();if(!p.empty())put(LOWORD(w)==ToolsBrowse?Tools:Output,p);break;}
        case RuleAdd:addRule();break;case Start:launch(false);break;case Inspect:launch(true);break;case Cancel:cancelled=true;put(Status,L"Annullamento in corso...");break;
    }return 0;
    case LogMessage:{std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(l));logText(*s);return 0;}
    case DoneMessage:if(worker.joinable())worker.join();setBusy(false);put(Status,cancelled?L"Annullato — consulta il log":L"Operazione terminata — consulta il log");if(closeAfter)SendMessageW(hwnd,WM_CLOSE,0,0);return 0;
    case WM_CLOSE:if(busy){closeAfter=true;cancelled=true;put(Status,L"Chiusura dopo annullamento...");return 0;}saveSettings();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:DeleteObject(font);PostQuitMessage(0);return 0;
    }}catch(const std::exception& e){MessageBoxW(hwnd,errorText(e).c_str(),L"MKV Flow",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(hwnd,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int show){
    instance=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=h;wc.lpszClassName=L"MKVFlowWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
    dpi=GetDpiForSystem();RECT bounds{0,0,MulDiv(640,dpi,96),MulDiv(460,dpi,96)};AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0);HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"MKV Flow " MKVFLOW_VERSION_W L" — MKV batch tools",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,h,nullptr);if(!hwnd){CoUninitialize();return 1;}
    ShowWindow(hwnd,show);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    CoUninitialize();return static_cast<int>(m.wParam);
}

