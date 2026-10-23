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
enum { Add=100, Remove, Clear, Inspect, Tools, ToolsBrowse, Output, OutputBrowse, Pattern, SetTitle, Title, Remux, Rules, Start, Cancel, Queue, LogBox, Status, RuleType, RuleName, RuleLang, RuleDefault, RuleForced, RuleAdd };
constexpr UINT LogMessage=WM_APP+1, DoneMessage=WM_APP+2;
HWND window; HINSTANCE instance; HFONT font;int dpi=96;
std::vector<fs::path> files;std::thread worker;std::atomic_bool cancelled=false;bool busy=false,closeAfter=false;
fs::path settings;
struct Placement {HWND h;int x,y,w,hgt;bool stretch;};std::vector<Placement> controls;
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
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls.push_back({c,x,y,w,h,stretch});return c;
}
void label(const wchar_t* text,int x,int y,int w){make(L"STATIC",text,0,x,y,w,20);}
void layout(){
    RECT r;GetClientRect(window,&r);int logicalHeight=MulDiv(r.bottom,96,dpi),dy=logicalHeight-780;
    for(const auto& c:controls){int y=c.y,h=c.hgt;if(c.y>=560)y+=dy;else if(c.y==448)h+=dy;
        MoveWindow(c.h,MulDiv(c.x,r.right,930),MulDiv(y,dpi,96),MulDiv(c.w,r.right,930),MulDiv(h,dpi,96),TRUE);}
}
void setBusy(bool value){busy=value;for(const auto& c:controls)if(GetDlgCtrlID(c.h)>0)EnableWindow(c.h,!value);EnableWindow(control(LogBox),TRUE);EnableWindow(control(Cancel),value);put(Status,value?L"Elaborazione in corso...":L"Pronto");}
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
    dpi=GetDpiForSystem();font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    label(L"MKV Flow " MKVFLOW_VERSION_W L"  /  Modifica, rinomina, verifica",16,12,650);
    make(L"BUTTON",L"Aggiungi MKV...",Add,16,42,145,28,WS_TABSTOP);make(L"BUTTON",L"Rimuovi",Remove,170,42,100,28,WS_TABSTOP);make(L"BUTTON",L"Svuota",Clear,278,42,85,28,WS_TABSTOP);make(L"BUTTON",L"Analizza selezionato",Inspect,372,42,180,28,WS_TABSTOP);
    label(L"Trascina MKV o cartelle (non ricorsive).",565,46,350);
    make(L"LISTBOX",L"",Queue,16,78,898,112,WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_BORDER,true);
    label(L"MKVToolNix",16,203,100);make(L"EDIT",L"",Tools,120,198,666,26,WS_TABSTOP|ES_AUTOHSCROLL,true);make(L"BUTTON",L"Sfoglia...",ToolsBrowse,800,198,114,26,WS_TABSTOP);
    label(L"Output",16,237,100);make(L"EDIT",L"",Output,120,232,666,26,WS_TABSTOP|ES_AUTOHSCROLL,true);make(L"BUTTON",L"Sfoglia...",OutputBrowse,800,232,114,26,WS_TABSTOP);
    label(L"Nome finale",16,271,100);make(L"EDIT",L"{name} [{crc}]",Pattern,120,266,470,26,WS_TABSTOP|ES_AUTOHSCROLL,true);label(L"Token: {name}  {n}  {crc}  /  .mkv automatico",604,271,310);
    make(L"BUTTON",L"Imposta titolo",SetTitle,16,302,140,24,WS_TABSTOP|BS_AUTOCHECKBOX);make(L"EDIT",L"{name}",Title,160,300,430,26,WS_TABSTOP|ES_AUTOHSCROLL,true);
    make(L"BUTTON",L"Remux (disattiva per copia + modifica)",Remux,604,302,310,24,WS_TABSTOP|BS_AUTOCHECKBOX);SendMessageW(control(Remux),BM_SETCHECK,BST_CHECKED,0);
    label(L"Regole comuni a tutti i file: selettori a1 = primo audio, s1 = primi sottotitoli, v1 = primo video, 1 = prima traccia",16,336,898);
    label(L"Selettore",16,362,90);label(L"Nome traccia (vuoto = conserva)",120,362,275);label(L"Lingua",410,362,90);label(L"Predefinita",514,362,110);label(L"Forzata",638,362,100);
    make(L"EDIT",L"a1",RuleType,16,385,90,26,WS_TABSTOP|ES_AUTOHSCROLL);make(L"EDIT",L"",RuleName,120,385,275,26,WS_TABSTOP|ES_AUTOHSCROLL);make(L"EDIT",L"ita",RuleLang,410,385,90,26,WS_TABSTOP|ES_AUTOHSCROLL);
    for(int id:{RuleDefault,RuleForced}){make(L"COMBOBOX",L"",id,id==RuleDefault?514:638,385,110,150,WS_TABSTOP|CBS_DROPDOWNLIST);for(auto s:{L"Conserva",L"Sì",L"No"})SendMessageW(control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));SendMessageW(control(id),CB_SETCURSEL,0,0);}
    make(L"BUTTON",L"Aggiungi regola",RuleAdd,762,385,152,26,WS_TABSTOP);
    label(L"Regole modificabili: selettore|nome|lingua|default|forced   •   campo vuoto = conserva; nome <vuoto> = rimuovi",16,421,898);
    make(L"EDIT",L"",Rules,16,448,898,98,WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,true);
    make(L"BUTTON",L"Avvia batch",Start,16,560,145,32,WS_TABSTOP|BS_DEFPUSHBUTTON);make(L"BUTTON",L"Annulla",Cancel,170,560,105,32,WS_TABSTOP);EnableWindow(control(Cancel),FALSE);
    make(L"STATIC",L"Pronto",Status,290,567,624,22,0,true);
    make(L"EDIT",L"",LogBox,16,605,898,159,WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,true);SendMessageW(control(LogBox),EM_SETLIMITTEXT,220000,0);
    PWSTR local=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){settings=fs::path(local)/L"MKVFlow";CoTaskMemFree(local);fs::create_directories(settings);settings/=L"settings.ini";
        if(!fs::exists(settings)){std::ofstream ini(settings,std::ios::binary);ini.put(static_cast<char>(0xFF));ini.put(static_cast<char>(0xFE));}}
    put(Tools,readSetting(L"Tools",L"C:\\Program Files\\MKVToolNix"));put(Output,readSetting(L"Output",L""));put(Pattern,readSetting(L"Pattern",L"{name} [{crc}]"));
    DragAcceptFiles(window,TRUE);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    try{switch(msg){
    case WM_CREATE:window=hwnd;build();return 0;
    case WM_SIZE:layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={MulDiv(950,dpi,96),MulDiv(825,dpi,96)};return 0;
    case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(w);if(!busy){UINT n=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);for(UINT i=0;i<n;++i){std::vector<wchar_t> p(DragQueryFileW(drop,i,nullptr,0)+1);DragQueryFileW(drop,i,p.data(),static_cast<UINT>(p.size()));addPath(p.data());}refresh();}DragFinish(drop);return 0;}
    case WM_COMMAND:if(HIWORD(w)>1)return 0;switch(LOWORD(w)){
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
    dpi=GetDpiForSystem();HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"MKV Flow " MKVFLOW_VERSION_W L" — MKV batch tools",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(950,dpi,96),MulDiv(825,dpi,96),nullptr,nullptr,h,nullptr);if(!hwnd){CoUninitialize();return 1;}
    ShowWindow(hwnd,show);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    CoUninitialize();return static_cast<int>(m.wParam);
}
