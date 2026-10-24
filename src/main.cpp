#include "core.h"
#include "version.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <thread>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
using namespace app;

// ---------------------------------------------------------------------------
// Identificatori e stato
// ---------------------------------------------------------------------------
enum : int {
    // Coda
    Add = 100, Remove, Clear, AnalyzeAll, Companions, Queue, Output, OutputBrowse, Pattern,
    // Flussi
    JobCombo = 200, AnalyzeOne, Reset, MoveUp, MoveDown, AddExternal, RemoveExternal, ApplyAll, Tracks,
    EditName, EditLang, EditDefault, EditForced, EditDelay, LangFilter, FilterApply, SetTitle, Title,
    // Impostazioni
    Tools = 300, ToolsBrowse, FastCopy, Exclusive, NoChapters, NoAttachments, NoTags,
    // Comuni
    // Rinomina
    RenFolder = 350, RenBrowse, RenUseOutput, RenFind, RenWith, RenPattern, RenIgnoreCase, RenTitle, RenVerify, RenBatch,
    RenPreview, RenApply, RenUndo, RenRefresh,
    LogBox = 400, Start, Cancel, ProgressBar, Status, NavQueue = 500, NavTracks, NavRename, NavSettings, NavLog,
};
constexpr UINT LogMessage = WM_APP + 1, DoneMessage = WM_APP + 2, AnalyzedMessage = WM_APP + 3,
               ProgressMessage = WM_APP + 4, StateMessage = WM_APP + 5, LangCodeMessage = WM_APP + 6,
               RenamedMessage = WM_APP + 7;
constexpr int BaseWidth = 760, BaseHeight = 540;
enum Anchor { MoveX = 1, MoveY = 2, GrowW = 4, GrowH = 8 };
enum Page { PageQueue, PageTracks, PageRename, PageSettings, PageLog, PageCount };

struct Item {
    Job job;
    std::wstring state = L"Da analizzare";
};
struct Analyzed {
    size_t job = 0, source = 0;
    fs::path path;
    std::optional<Source> result;
    std::wstring error;
};
struct StateUpdate {
    size_t job;
    std::wstring text;
};

HWND window;
HINSTANCE instance;
HFONT font;
int dpi = 96;
std::vector<Item> items;
int current = -1;                       // file aperto nella scheda Flussi
std::optional<Template> model;          // layout applicato a tutto il batch
std::thread worker;
std::atomic_bool cancelled = false;
bool busy = false, closeAfter = false, startAfterAnalysis = false, filling = false, batchRunning = false, renameRunning = false;
std::vector<RenameItem> renamePlan;
std::vector<std::pair<fs::path, fs::path>> lastRename;   // per annullare
fs::path settings;
int currentPage = 0, buildingPage = -1;

struct Placement { HWND h; int x, y, w, hgt, anchors, page; };
std::vector<Placement> controls;

const wchar_t* languageChoices[] = {
    L"ita — Italiano", L"eng — Inglese", L"jpn — Giapponese", L"spa — Spagnolo", L"fre — Francese",
    L"ger — Tedesco", L"por — Portoghese", L"rus — Russo", L"chi — Cinese", L"kor — Coreano", L"und — Non definita",
};

// ---------------------------------------------------------------------------
// Utilità finestra
// ---------------------------------------------------------------------------
HWND control(int id) { return GetDlgItem(window, id); }
std::wstring get(int id) {
    int n = GetWindowTextLengthW(control(id));
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(control(id), s.data(), n + 1);
    s.resize(n);
    return s;
}
bool checked(int id) { return SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(int id, bool value) { SendMessageW(control(id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
void put(int id, const std::wstring& s) { SetWindowTextW(control(id), s.c_str()); }
void status(const std::wstring& s) { put(Status, s); }
int px(int logical) { return MulDiv(logical, dpi, 96); }

void post(UINT msg, WPARAM w, void* payload) { PostMessageW(window, msg, w, reinterpret_cast<LPARAM>(payload)); }
void emit(const std::wstring& s) {
    auto p = new std::wstring(s);
    if (!PostMessageW(window, LogMessage, 0, reinterpret_cast<LPARAM>(p))) delete p;
}
void postState(size_t job, const std::wstring& text) {
    auto p = new StateUpdate{job, text};
    if (!PostMessageW(window, StateMessage, 0, reinterpret_cast<LPARAM>(p))) delete p;
}
void logText(const std::wstring& text) {
    HWND h = control(LogBox);
    if (GetWindowTextLengthW(h) > 400000) SetWindowTextW(h, L"[Log precedente abbreviato]\r\n");
    SendMessageW(h, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    auto line = text + L"\r\n";
    SendMessageW(h, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}
std::wstring errorText(const std::exception& e) { return widen(e.what()); }

HWND make(const wchar_t* cls, const wchar_t* text, int id, int x, int y, int w, int h, DWORD style = 0, int anchors = 0) {
    bool edge = wcscmp(cls, L"EDIT") == 0 || wcscmp(cls, WC_LISTVIEWW) == 0;
    HWND c = CreateWindowExW(edge ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    controls.push_back({c, x, y, w, h, anchors, buildingPage});
    return c;
}
void label(const wchar_t* text, int x, int y, int w, int anchors = 0) { make(L"STATIC", text, 0, x, y, w, 20, SS_NOPREFIX, anchors); }
HWND button(const wchar_t* text, int id, int x, int y, int w, int anchors = 0, DWORD style = 0) {
    return make(L"BUTTON", text, id, x, y, w, 28, WS_TABSTOP | style, anchors);
}
HWND checkbox(const wchar_t* text, int id, int x, int y, int w, int anchors = 0) {
    return make(L"BUTTON", text, id, x, y, w, 24, WS_TABSTOP | BS_AUTOCHECKBOX, anchors);
}
HWND edit(const wchar_t* text, int id, int x, int y, int w, int anchors = 0) {
    return make(L"EDIT", text, id, x, y, w, 26, WS_TABSTOP | ES_AUTOHSCROLL, anchors);
}

HWND listView(int id, int x, int y, int w, int h, DWORD style, DWORD exStyle, std::initializer_list<std::pair<const wchar_t*, int>> columns) {
    HWND list = make(WC_LISTVIEWW, L"", id, x, y, w, h, WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | style, GrowW | GrowH);
    SetWindowTheme(list, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | exStyle);
    int i = 0;
    for (const auto& [title, width] : columns) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<wchar_t*>(title);
        c.cx = px(width);
        ListView_InsertColumn(list, i++, &c);
    }
    return list;
}
// La colonna `stretch` occupa lo spazio libero della lista.
void fitColumn(int id, int stretch) {
    HWND list = control(id);
    RECT r;
    GetClientRect(list, &r);
    HWND header = ListView_GetHeader(list);
    int count = Header_GetItemCount(header), others = 0;
    for (int i = 0; i < count; ++i) if (i != stretch) others += ListView_GetColumnWidth(list, i);
    ListView_SetColumnWidth(list, stretch, (std::max)(px(120), static_cast<int>(r.right) - others));
}
void setRow(HWND list, int row, int col, const std::wstring& text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
}
int selectedRow(int id) { return ListView_GetNextItem(control(id), -1, LVNI_SELECTED); }

void layout() {
    RECT r;
    GetClientRect(window, &r);
    int dx = (std::max)(0, MulDiv(r.right, 96, dpi) - BaseWidth), dy = (std::max)(0, MulDiv(r.bottom, 96, dpi) - BaseHeight);
    HDWP batch = BeginDeferWindowPos(static_cast<int>(controls.size()));
    for (const auto& c : controls) {
        int x = c.x + ((c.anchors & MoveX) ? dx : 0), y = c.y + ((c.anchors & MoveY) ? dy : 0);
        int w = c.w + ((c.anchors & GrowW) ? dx : 0), h = c.hgt + ((c.anchors & GrowH) ? dy : 0);
        if (batch) batch = DeferWindowPos(batch, c.h, nullptr, px(x), px(y), px(w), px(h), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (batch) EndDeferWindowPos(batch);
    fitColumn(Queue, 0);
    fitColumn(Tracks, 4);
    fitColumn(RenPreview, 1);
}

void showPage(int page) {
    currentPage = page;
    for (const auto& c : controls) ShowWindow(c.h, c.page < 0 || c.page == page ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < PageCount; ++i) check(NavQueue + i, i == page);
}

// ---------------------------------------------------------------------------
// Coda
// ---------------------------------------------------------------------------
std::wstring externalsText(const Job& job) {
    size_t n = job.sources.size() - 1;
    return n ? std::to_wstring(n) : std::wstring();
}
void queueRow(size_t i) {
    HWND list = control(Queue);
    const auto& job = items[i].job;
    setRow(list, static_cast<int>(i), 0, job.input().filename().wstring());
    setRow(list, static_cast<int>(i), 1, summary(job) + (job.customized ? L"  ✎" : L""));
    setRow(list, static_cast<int>(i), 2, externalsText(job));
    setRow(list, static_cast<int>(i), 3, items[i].state);
}
void refreshJobCombo() {
    HWND combo = control(JobCombo);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < items.size(); ++i) {
        auto text = std::to_wstring(i + 1) + L". " + items[i].job.input().filename().wstring();
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (current >= static_cast<int>(items.size())) current = static_cast<int>(items.size()) - 1;
    if (current < 0 && !items.empty()) current = 0;
    SendMessageW(combo, CB_SETCURSEL, current, 0);
}
void fillTracks();
void refreshQueue() {
    HWND list = control(Queue);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < items.size(); ++i) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<wchar_t*>(L"");
        ListView_InsertItem(list, &it);
        queueRow(i);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    refreshJobCombo();
    fillTracks();
    status(std::to_wstring(items.size()) + L" file in coda");
}
bool addFile(const fs::path& p) {
    std::error_code ec;
    if (_wcsicmp(p.extension().c_str(), L".mkv") != 0 || !fs::is_regular_file(p, ec)) return false;
    auto absolute = fs::absolute(p).lexically_normal();
    for (const auto& item : items)
        if (_wcsicmp(item.job.input().c_str(), absolute.c_str()) == 0) return false;
    items.push_back({makeJob(absolute)});
    return true;
}
void addPath(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        std::vector<fs::path> found;
        for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) found.push_back(it->path());
        std::sort(found.begin(), found.end(), [](const fs::path& a, const fs::path& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        for (const auto& f : found) addFile(f);
    } else addFile(p);
}
std::vector<fs::path> openFiles(const wchar_t* filter) {
    std::vector<wchar_t> buf(1 << 17);
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = window;
    o.lpstrFilter = filter;
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    std::vector<fs::path> result;
    if (!GetOpenFileNameW(&o)) return result;
    fs::path first(buf.data());
    auto p = buf.data() + wcslen(buf.data()) + 1;
    if (!*p) result.push_back(first);
    else for (; *p; p += wcslen(p) + 1) result.push_back(first / p);
    return result;
}
std::wstring folder() {
    IFileDialog* dialog = nullptr;
    std::wstring result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD flags;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(dialog->Show(window))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
                item->Release();
            }
        }
        dialog->Release();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Editor dei flussi
// ---------------------------------------------------------------------------
Job* currentJob() { return current >= 0 && current < static_cast<int>(items.size()) ? &items[current].job : nullptr; }

bool changed(const Job& job, const TrackEdit& e) {
    const auto& st = job.sourceTrack(e);
    return e.name != st.name || e.language != st.language || e.isDefault != st.isDefault || e.forced != st.forced || e.delayMs;
}
void trackRow(int row) {
    const Job* job = currentJob();
    if (!job || row < 0 || row >= static_cast<int>(job->tracks.size())) return;
    HWND list = control(Tracks);
    const auto& e = job->tracks[row];
    const auto& st = job->sourceTrack(e);
    bool wasFilling = filling;
    filling = true;
    setRow(list, row, 0, std::to_wstring(row + 1) + (changed(*job, e) ? L" ✎" : L""));
    setRow(list, row, 1, kindName(st.kind) + (e.source == 0 ? L" " + std::to_wstring(st.ordinal) : L""));
    setRow(list, row, 2, st.codec);
    setRow(list, row, 3, (e.language.empty() ? L"—" : e.language) + (validLanguage(e.language) ? L"" : L" ⚠"));
    setRow(list, row, 4, e.name);
    setRow(list, row, 5, e.isDefault ? L"Sì" : L"");
    setRow(list, row, 6, e.forced ? L"Sì" : L"");
    setRow(list, row, 7, e.delayMs ? (e.delayMs > 0 ? L"+" : L"") + std::to_wstring(e.delayMs) + L" ms" : L"");
    setRow(list, row, 8, st.info);
    setRow(list, row, 9, e.source == 0 ? L"MKV · ID " + std::to_wstring(st.id) : job->sources[e.source].path.filename().wstring());
    ListView_SetCheckState(list, row, e.keep);
    filling = wasFilling;
}
void fillPanel() {
    const Job* job = currentJob();
    int row = selectedRow(Tracks);
    bool valid = job && row >= 0 && row < static_cast<int>(job->tracks.size()) && !busy;
    filling = true;
    for (int id : {EditName, EditLang, EditDefault, EditForced, EditDelay, MoveUp, MoveDown}) EnableWindow(control(id), valid);
    EnableWindow(control(RemoveExternal), valid && job->tracks[row].source > 0);
    if (valid) {
        const auto& e = job->tracks[row];
        put(EditName, e.name);
        put(EditLang, e.language);
        check(EditDefault, e.isDefault);
        check(EditForced, e.forced);
        put(EditDelay, std::to_wstring(e.delayMs));
    } else {
        put(EditName, L"");
        put(EditLang, L"");
        check(EditDefault, false);
        check(EditForced, false);
        put(EditDelay, L"");
    }
    filling = false;
}
void fillTracks() {
    HWND list = control(Tracks);
    const Job* job = currentJob();
    int keep = selectedRow(Tracks);
    filling = true;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    if (job) {
        for (size_t i = 0; i < job->tracks.size(); ++i) {
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = static_cast<int>(i);
            it.pszText = const_cast<wchar_t*>(L"");
            ListView_InsertItem(list, &it);
            trackRow(static_cast<int>(i));
        }
        if (keep >= 0 && keep < static_cast<int>(job->tracks.size()))
            ListView_SetItemState(list, keep, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    filling = false;
    fillPanel();
    if (!job) status(L"Aggiungere un file MKV nella scheda Coda");
    else if (!job->sources.front().analyzed) status(L"File non ancora analizzato: premere Analizza");
}
void touched() {
    if (Job* job = currentJob()) {
        job->customized = true;
        queueRow(static_cast<size_t>(current));
    }
}
void selectTrack(int row) {
    HWND list = control(Tracks);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, row, FALSE);
}
void moveTrack(int delta) {
    Job* job = currentJob();
    int row = selectedRow(Tracks), to = row + delta;
    if (!job || busy || row < 0 || to < 0 || to >= static_cast<int>(job->tracks.size())) return;
    std::swap(job->tracks[row], job->tracks[to]);
    trackRow(row);
    trackRow(to);
    selectTrack(to);
    touched();
}
void editSelected(const std::function<void(Job&, TrackEdit&, int)>& change) {
    Job* job = currentJob();
    int row = selectedRow(Tracks);
    if (filling || busy || !job || row < 0 || row >= static_cast<int>(job->tracks.size())) return;
    change(*job, job->tracks[row], row);
    trackRow(row);
    touched();
}

// ---------------------------------------------------------------------------
// Impostazioni persistenti
// ---------------------------------------------------------------------------
std::wstring readSetting(const wchar_t* key, const wchar_t* fallback) {
    std::vector<wchar_t> b(32768);
    GetPrivateProfileStringW(L"Settings", key, fallback, b.data(), static_cast<DWORD>(b.size()), settings.c_str());
    return b.data();
}
void saveSettings() {
    if (settings.empty()) return;
    for (auto [id, key] : {std::pair{Tools, L"Tools"}, std::pair{Output, L"Output"}, std::pair{Pattern, L"Pattern"}, std::pair{LangFilter, L"Languages"},
                           std::pair{RenFolder, L"RenameFolder"}, std::pair{RenFind, L"RenameFind"}, std::pair{RenWith, L"RenameWith"},
                           std::pair{RenPattern, L"RenamePattern"}})
        WritePrivateProfileStringW(L"Settings", key, get(id).c_str(), settings.c_str());
    for (auto [id, key] : {std::pair{FastCopy, L"FastCopy"}, std::pair{Exclusive, L"ExclusiveDefault"}, std::pair{NoChapters, L"NoChapters"},
                           std::pair{NoAttachments, L"NoAttachments"}, std::pair{NoTags, L"NoGlobalTags"},
                           std::pair{RenIgnoreCase, L"RenameIgnoreCase"}})
        WritePrivateProfileStringW(L"Settings", key, checked(id) ? L"1" : L"0", settings.c_str());
}
fs::path toolsFolder() {
    fs::path tools = get(Tools);
    if (!tools.is_absolute() || !fs::is_regular_file(tools / L"mkvmerge.exe") || !fs::is_regular_file(tools / L"mkvpropedit.exe"))
        throw std::runtime_error("Selezionare nella scheda Impostazioni la cartella MKVToolNix con mkvmerge.exe e mkvpropedit.exe");
    return tools;
}
Options options() {
    Options o;
    o.tools = toolsFolder();
    o.output = get(Output);
    o.pattern = get(Pattern);
    o.title = get(Title);
    o.setTitle = checked(SetTitle);
    o.fastCopy = checked(FastCopy);
    o.noChapters = checked(NoChapters);
    o.noAttachments = checked(NoAttachments);
    o.noGlobalTags = checked(NoTags);
    if (checked(RenBatch)) {
        o.replaceFind = get(RenFind);
        o.replaceWith = get(RenWith);
        o.replaceIgnoreCase = checked(RenIgnoreCase);
    }
    if (!o.output.is_absolute()) throw std::runtime_error("Scegliere nella scheda Coda un percorso assoluto per la cartella output");
    if (o.pattern.find(L"{crc}") == o.pattern.npos) throw std::runtime_error("Il modello nome deve contenere {crc}");
    auto validateTokens = [](std::wstring s, bool title) {
        for (auto token : {L"{name}", L"{n}", L"{crc}"}) {
            if (title && std::wstring(token) == L"{crc}") continue;
            size_t p;
            while ((p = s.find(token)) != s.npos) s.erase(p, wcslen(token));
        }
        if (s.find_first_of(L"{}") != s.npos) throw std::runtime_error("Token sconosciuto: usare {name}, {n}, {crc}; il titolo non accetta {crc}");
    };
    validateTokens(o.pattern, false);
    if (o.setTitle) validateTokens(o.title, true);
    for (size_t i = 0; i < items.size(); ++i) fileName(expand(o.pattern, outputStem(items[i].job, o), i + 1, L"00000000"));
    return o;
}

// ---------------------------------------------------------------------------
// Lavori in background
// ---------------------------------------------------------------------------
void setBusy(bool value) {
    busy = value;
    for (const auto& c : controls) if (GetDlgCtrlID(c.h) > 0) EnableWindow(c.h, !value);
    for (int id : {LogBox, NavQueue, NavTracks, NavRename, NavSettings, NavLog, Status, ProgressBar}) EnableWindow(control(id), TRUE);
    EnableWindow(control(Cancel), value);
    EnableWindow(control(RenUndo), !value && !lastRename.empty());
    if (!value) fillPanel();
}
void runWorker(std::function<void()> body) {
    cancelled = false;
    setBusy(true);
    try {
        worker = std::thread([body] {
            try { body(); } catch (const std::exception& e) { emit(L"ERRORE: " + errorText(e)); }
            PostMessageW(window, DoneMessage, 0, 0);
        });
    } catch (...) {
        setBusy(false);
        throw;
    }
}

struct Pending { size_t job, source; fs::path path; };
// Analizza le sorgenti indicate (o tutte quelle non ancora analizzate).
void launchAnalysis(std::vector<Pending> work) {
    if (work.empty()) {
        for (size_t j = 0; j < items.size(); ++j)
            for (size_t s = 0; s < items[j].job.sources.size(); ++s)
                if (!items[j].job.sources[s].analyzed) work.push_back({j, s, items[j].job.sources[s].path});
    }
    if (work.empty()) {
        status(L"Tutti i file sono già analizzati");
        if (startAfterAnalysis) PostMessageW(window, DoneMessage, 1, 0);
        return;
    }
    auto tools = toolsFolder();
    for (const auto& w : work)
        if (w.source == 0) { items[w.job].state = L"Analisi..."; queueRow(w.job); }
    SendMessageW(control(ProgressBar), PBM_SETPOS, 0, 0);
    status(L"Analisi di " + std::to_wstring(work.size()) + L" file...");
    runWorker([work, tools] {
        for (size_t i = 0; i < work.size() && !cancelled; ++i) {
            auto* a = new Analyzed{work[i].job, work[i].source, work[i].path};
            try { a->result = analyze(tools, work[i].path, cancelled); }
            catch (const std::exception& e) { a->error = errorText(e); }
            post(AnalyzedMessage, 0, a);
            PostMessageW(window, ProgressMessage, static_cast<WPARAM>((i + 1) * 1000 / work.size()), 0);
        }
    });
}
void analyzed(std::unique_ptr<Analyzed> a) {
    if (a->job >= items.size()) return;
    auto& item = items[a->job];
    auto& job = item.job;
    if (a->source >= job.sources.size() || _wcsicmp(job.sources[a->source].path.c_str(), a->path.c_str()) != 0) return;
    auto name = a->path.filename().wstring();
    if (!a->result) {
        emit(L"ERRORE analisi " + name + L": " + a->error);
        if (a->source > 0) {
            removeSource(job, a->source);
            emit(L"  traccia esterna ignorata.");
        } else item.state = L"Errore analisi";
    } else {
        auto& src = job.sources[a->source];
        bool firstMain = a->source == 0 && !src.analyzed;
        Source fresh = std::move(*a->result);
        fresh.languageHint = src.languageHint;
        fresh.forcedHint = src.forcedHint;
        src = std::move(fresh);
        if (firstMain) {
            resetEdits(job);
            if (model) {
                auto warnings = applyTemplate(job, *model);
                if (!warnings.empty()) emit(L"Modello su " + name + L": " + warnings);
                item.state = warnings.empty() ? L"Pronto (modello)" : L"Pronto — differenze";
            } else item.state = L"Pronto";
        } else syncEdits(job);
        if (a->source == 0 && item.state.rfind(L"Pronto", 0) != 0) item.state = L"Pronto";
        emit(L"Analizzato " + name + L": " + std::to_wstring(src.tracks.size()) + L" tracce");
    }
    queueRow(a->job);
    if (static_cast<int>(a->job) == current) fillTracks();
}

void launchBatch() {
    auto o = options();
    saveSettings();
    std::vector<Job> batch;
    for (const auto& item : items) batch.push_back(item.job);
    SetWindowTextW(control(LogBox), L"");
    SendMessageW(control(ProgressBar), PBM_SETPOS, 0, 0);
    batchRunning = true;
    status(L"Elaborazione in corso...");
    runWorker([o, batch] {
        size_t ok = 0, failed = 0, n = batch.size();
        for (size_t i = 0; i < n && !cancelled; ++i) {
            emit(L"\r\n[" + std::to_wstring(i + 1) + L"/" + std::to_wstring(n) + L"] " + batch[i].input().wstring());
            postState(i, L"In corso...");
            try {
                auto target = process(batch[i], i + 1, o, cancelled, emit, [i, n](int p) {
                    PostMessageW(window, ProgressMessage, static_cast<WPARAM>((i * 100 + p) * 10 / n), 0);
                });
                ++ok;
                emit(L"Salvato: " + target.wstring());
                postState(i, L"✔ " + target.filename().wstring());
            } catch (const std::exception& e) {
                ++failed;
                emit(L"ERRORE: " + errorText(e));
                postState(i, cancelled ? L"Annullato" : L"✖ Errore (vedi Log)");
            }
        }
        emit(L"\r\nRiepilogo: " + std::to_wstring(ok) + L" completati, " + std::to_wstring(failed) + L" falliti; " +
             (cancelled ? L"batch annullato" : L"fine batch"));
    });
}

void start() {
    if (items.empty()) throw std::runtime_error("Aggiungere almeno un file MKV");
    options();  // controlla subito percorsi e nomi
    bool pending = std::any_of(items.begin(), items.end(), [](const Item& i) { return !i.job.analyzed(); });
    if (pending) {
        startAfterAnalysis = true;
        try { launchAnalysis({}); } catch (...) { startAfterAnalysis = false; throw; }
    } else launchBatch();
}

// ---------------------------------------------------------------------------
// Comandi
// ---------------------------------------------------------------------------
void addExternals(const std::vector<fs::path>& files) {
    Job* job = currentJob();
    if (!job) throw std::runtime_error("Selezionare prima un file MKV della coda");
    std::vector<Pending> work;
    for (const auto& f : files) {
        if (!isExternalTrackFile(f)) { emit(L"Ignorato (formato non previsto): " + f.filename().wstring()); continue; }
        size_t before = job->sources.size();
        size_t s = addExternal(*job, f);
        if (s >= before) work.push_back({static_cast<size_t>(current), s, f});
    }
    queueRow(static_cast<size_t>(current));
    if (!work.empty()) launchAnalysis(work);
}
void matchCompanions() {
    std::vector<Pending> work;
    size_t added = 0;
    for (size_t j = 0; j < items.size(); ++j) {
        auto& job = items[j].job;
        for (const auto& f : findCompanions(job.input())) {
            size_t before = job.sources.size();
            size_t s = addExternal(job, f);
            if (s >= before) { work.push_back({j, s, f}); ++added; emit(L"Abbinato: " + f.filename().wstring() + L" → " + job.input().filename().wstring()); }
        }
        queueRow(j);
    }
    if (!added) { status(L"Nessuna traccia esterna con lo stesso nome dei video"); return; }
    status(std::to_wstring(added) + L" tracce esterne abbinate");
    // Analizza anche i file principali ancora in attesa: servono per l'editor.
    for (size_t j = 0; j < items.size(); ++j)
        if (!items[j].job.sources.front().analyzed) work.push_back({j, 0, items[j].job.input()});
    launchAnalysis(work);
}
void applyToAll() {
    Job* job = currentJob();
    if (!job || !job->sources.front().analyzed) throw std::runtime_error("Analizzare prima il file da usare come modello");
    model = makeTemplate(*job);
    size_t applied = 0, differences = 0;
    for (size_t j = 0; j < items.size(); ++j) {
        if (static_cast<int>(j) == current || !items[j].job.sources.front().analyzed) continue;
        auto warnings = applyTemplate(items[j].job, *model);
        ++applied;
        if (!warnings.empty()) {
            ++differences;
            emit(L"Modello su " + items[j].job.input().filename().wstring() + L": " + warnings);
            items[j].state = L"Pronto — differenze";
        } else items[j].state = L"Pronto (modello)";
        queueRow(j);
    }
    status(L"Layout applicato a " + std::to_wstring(applied) + L" file" +
           (differences ? L", " + std::to_wstring(differences) + L" con differenze (vedi Log)" : L"") +
           L"; i file analizzati dopo lo riceveranno automaticamente. Le tracce esterne restano per file.");
}
void removeQueued() {
    std::vector<int> rows;
    for (int i = -1; (i = ListView_GetNextItem(control(Queue), i, LVNI_SELECTED)) >= 0;) rows.push_back(i);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        items.erase(items.begin() + *it);
        if (current > *it || current == static_cast<int>(items.size())) --current;
    }
    refreshQueue();
}
void openJob(int index) {
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    current = index;
    SendMessageW(control(JobCombo), CB_SETCURSEL, current, 0);
    fillTracks();
    showPage(PageTracks);
    if (!items[current].job.sources.front().analyzed && !busy) {
        try { toolsFolder(); launchAnalysis({{static_cast<size_t>(current), 0, items[current].job.input()}}); }
        catch (const std::exception& e) { status(errorText(e)); }
    }
}

// ---------------------------------------------------------------------------
// Rinomina sul posto
// ---------------------------------------------------------------------------
struct Renamed {
    std::vector<RenameItem> plan;
    std::vector<std::pair<fs::path, fs::path>> done;
};
RenameRules renameRules() {
    RenameRules r;
    r.find = get(RenFind);
    r.with = get(RenWith);
    r.pattern = get(RenPattern);
    r.ignoreCase = checked(RenIgnoreCase);
    r.companions = false;  // si rinominano solo gli MKV
    return r;
}
void showRenamePlan(const std::vector<RenameItem>& plan, bool done) {
    HWND list = control(RenPreview);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    size_t ok = 0;
    for (size_t i = 0; i < plan.size(); ++i) {
        const auto& item = plan[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        auto from = (item.companion ? L"   ↳ " : L"") + item.from.filename().wstring();
        it.pszText = const_cast<wchar_t*>(from.c_str());
        ListView_InsertItem(list, &it);
        setRow(list, static_cast<int>(i), 1, item.to.filename().wstring());
        setRow(list, static_cast<int>(i), 2, !item.error.empty() ? L"✖ " + item.error : done ? L"✔ rinominato" : item.pendingCrc ? L"CRC da calcolare" : L"pronto");
        if (item.error.empty()) ++ok;
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    EnableWindow(control(RenApply), !busy && !done && ok > 0);
    if (!done) {
        status(L"Rinomina: " + std::to_wstring(ok) + L" file pronti" +
                    (ok < plan.size() ? L", " + std::to_wstring(plan.size() - ok) + L" con problemi" : L""));
    }
}
void refreshRename() {
    if (busy) return;
    renamePlan.clear();
    fs::path dir = get(RenFolder);
    std::error_code ec;
    if (!dir.empty() && dir.is_absolute() && fs::is_directory(dir, ec)) {
        try { renamePlan = planRename(listMkv(dir), renameRules()); }
        catch (const std::exception& e) { status(errorText(e)); }
    } else if (!dir.empty()) status(L"Rinomina: cartella non trovata");
    showRenamePlan(renamePlan, false);
}
void renameNow() {
    refreshRename();  // la cartella potrebbe essere cambiata dall'ultima anteprima
    auto rules = renameRules();
    {
        auto rest = rules.pattern;
        for (auto token : {L"{name}", L"{crc}", L"{n}"}) {
            size_t p;
            while ((p = rest.find(token)) != rest.npos) rest.erase(p, wcslen(token));
        }
        if (rest.find_first_of(L"{}") != rest.npos) throw std::runtime_error("Nome: usare solo {name}, {crc} e {n}");
        if (rules.pattern.find(L"{name}") == std::wstring::npos && rules.pattern.find(L"{n}") == std::wstring::npos)
            throw std::runtime_error("Nome: inserire {name} o {n}, altrimenti tutti i file avrebbero lo stesso nome");
    }
    bool titles = checked(RenTitle), verify = checked(RenVerify);
    size_t ok = std::count_if(renamePlan.begin(), renamePlan.end(), [](const RenameItem& i) { return i.error.empty(); });
    size_t pending = std::count_if(renamePlan.begin(), renamePlan.end(), [](const RenameItem& i) { return i.error.empty() && i.pendingCrc && !i.companion; });
    if (!ok && !verify) throw std::runtime_error("Nessun file da rinominare: i nomi sono già corretti");
    fs::path tools;
    if (titles) tools = toolsFolder();
    fs::path dir = get(RenFolder);
    auto mkvs = listMkv(dir);
    std::wstring question = ok ? L"Rinominare " + std::to_wstring(ok) + L" file in\n" + dir.wstring() + L"?" : L"Verificare i CRC in\n" + dir.wstring() + L"?";
    if (verify) question += L"\n\nI CRC di tutti gli MKV verranno ricalcolati (rilettura completa dei file).";
    else if (pending) question += L"\n\nPer " + std::to_wstring(pending) + L" file senza CRC nel nome il CRC32 verrà calcolato (rilettura del file).";
    if (titles) question += L"\n\nAnche il titolo interno degli MKV verrà modificato nei file originali.";
    if (MessageBoxW(window, question.c_str(), L"MKV Flow", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    saveSettings();
    auto plan = renamePlan;
    renameRunning = true;
    SendMessageW(control(ProgressBar), PBM_SETPOS, 0, 0);
    status(L"Rinomina in corso...");
    emit(L"\r\nRinomina in " + dir.wstring() + L": \"" + rules.find + L"\" → \"" + rules.with + L"\", nome " + rules.pattern);
    runWorker([plan, rules, titles, verify, tools, mkvs] {
        std::vector<bool> changedContent(mkvs.size(), false), inPlan(mkvs.size(), false);
        for (const auto& item : plan) if (!item.companion && item.error.empty()) inPlan[item.owner] = true;
        // 1) Titolo interno: cambia il contenuto, quindi anche il CRC.
        if (titles)
            for (size_t i = 0; i < mkvs.size() && !cancelled; ++i) {
                if (!inPlan[i]) continue;
                try {
                    auto title = analyze(tools, mkvs[i], cancelled).title;
                    auto fresh = replaceText(title, rules.find, rules.with, rules.ignoreCase);
                    if (fresh == title) continue;
                    DWORD rc = run(tools / L"mkvpropedit.exe", {L"--output-charset", L"UTF-8", mkvs[i].wstring(), L"--edit", L"info", L"--set", L"title=" + fresh},
                                   cancelled, [](const std::wstring& line) { emit(L"  " + line); });
                    if (rc > 1) throw std::runtime_error("mkvpropedit ha segnalato un errore");
                    changedContent[i] = true;
                    emit(L"Titolo: \"" + title + L"\" → \"" + fresh + L"\"");
                } catch (const std::exception& e) {
                    emit(L"ERRORE titolo " + mkvs[i].filename().wstring() + L": " + errorText(e));
                }
            }
        // 2) CRC32: si riusa quello del nome; si legge il file solo se manca, se il contenuto
        //    è cambiato o se è richiesta la verifica.
        std::vector<std::wstring> crcs(mkvs.size());
        std::vector<size_t> toRead;
        for (size_t i = 0; i < mkvs.size(); ++i) {
            bool missing = rules.wantsCrc() && existingCrc(mkvs[i].stem().wstring()).empty();
            if (verify || changedContent[i] || (inPlan[i] && missing)) toRead.push_back(i);
        }
        size_t wrong = 0;
        for (size_t k = 0; k < toRead.size() && !cancelled; ++k) {
            size_t i = toRead[k];
            try {
                crcs[i] = crcFile(mkvs[i], cancelled);
                auto old = existingCrc(mkvs[i].stem().wstring());
                if (!old.empty() && old != crcs[i]) {
                    ++wrong;
                    emit(L"CRC diverso: " + mkvs[i].filename().wstring() + L" contiene " + crcs[i] +
                         (changedContent[i] ? L" (titolo modificato)" : L" — il file NON corrisponde al CRC del nome"));
                } else if (!old.empty()) emit(L"CRC corretto: " + mkvs[i].filename().wstring());
                else emit(L"CRC calcolato: " + mkvs[i].filename().wstring() + L" → " + crcs[i]);
            } catch (const std::exception& e) {
                emit(L"ERRORE CRC " + mkvs[i].filename().wstring() + L": " + errorText(e));
            }
            PostMessageW(window, ProgressMessage, static_cast<WPARAM>((k + 1) * 1000 / toRead.size()), 0);
        }
        if (verify) emit(L"Verifica: " + std::to_wstring(toRead.size()) + L" file controllati, " + std::to_wstring(wrong) + L" con CRC diverso dal nome");
        // 3) Piano definitivo con i CRC reali, poi rinomina (mai sovrascrivendo).
        auto* result = new Renamed{{}, {}};
        if (!cancelled) {
            result->plan = planRename(mkvs, rules, &crcs);
            result->done = applyRename(result->plan);
        }
        for (const auto& [from, to] : result->done) emit(L"Rinominato: " + from.filename().wstring() + L" → " + to.filename().wstring());
        for (const auto& item : result->plan)
            if (!item.error.empty()) emit(L"Saltato: " + item.from.filename().wstring() + L" (" + item.error + L")");
        post(RenamedMessage, 0, result);
    });
}
void renamed(std::unique_ptr<Renamed> r) {
    lastRename = r->done;
    renamePlan = r->plan;
    showRenamePlan(renamePlan, true);
    status(L"Rinominati " + std::to_wstring(r->done.size()) + L" file" + (r->done.size() < renamePlan.size() ? L"; alcuni saltati (vedi Log)" : L"") +
           L" — dettagli nel Log");
}
void undoRename() {
    if (busy || lastRename.empty()) return;
    std::vector<RenameItem> back;
    for (auto it = lastRename.rbegin(); it != lastRename.rend(); ++it) back.push_back({it->second, it->first, false, 0, {}});
    auto restored = applyRename(back);
    emit(L"\r\nAnnullata rinomina: " + std::to_wstring(restored.size()) + L" file ripristinati");
    for (const auto& item : back) if (!item.error.empty()) emit(L"Non ripristinato: " + item.from.filename().wstring() + L" (" + item.error + L")");
    lastRename.clear();
    EnableWindow(control(RenUndo), FALSE);
    refreshRename();
    status(L"Rinomina annullata: " + std::to_wstring(restored.size()) + L" file ripristinati (i titoli interni restano modificati)");
}

void command(int id, int code) {
    switch (id) {
    case NavQueue: case NavTracks: case NavRename: case NavSettings: case NavLog:
        showPage(id - NavQueue);
        if (id == NavRename) refreshRename();
        break;
    case Add: for (const auto& f : openFiles(L"Matroska (*.mkv)\0*.mkv\0\0")) addFile(f); refreshQueue(); break;
    case Remove: removeQueued(); break;
    case Clear: items.clear(); current = -1; refreshQueue(); break;
    case AnalyzeAll: launchAnalysis({}); break;
    case Companions: matchCompanions(); break;
    case ToolsBrowse: case OutputBrowse: { auto p = folder(); if (!p.empty()) put(id == ToolsBrowse ? Tools : Output, p); break; }
    case JobCombo:
        if (code == CBN_SELCHANGE) { current = static_cast<int>(SendMessageW(control(JobCombo), CB_GETCURSEL, 0, 0)); fillTracks(); }
        break;
    case AnalyzeOne:
        if (Job* job = currentJob()) {
            std::vector<Pending> work;
            for (size_t s = 0; s < job->sources.size(); ++s) work.push_back({static_cast<size_t>(current), s, job->sources[s].path});
            launchAnalysis(work);
        }
        break;
    case Reset:
        if (Job* job = currentJob()) { resetEdits(*job); queueRow(static_cast<size_t>(current)); fillTracks(); status(L"Flussi ripristinati come nel file originale"); }
        break;
    case MoveUp: moveTrack(-1); break;
    case MoveDown: moveTrack(1); break;
    case AddExternal:
        addExternals(openFiles(L"Tracce audio, sottotitoli e video\0*.mka;*.mks;*.ac3;*.eac3;*.ec3;*.dts;*.dtshd;*.thd;*.truehd;*.aac;*.m4a;*.flac;*.opus;*.ogg;*.mp3;*.mp2;*.wav;*.srt;*.ass;*.ssa;*.sup;*.vtt;*.idx;*.usf;*.h264;*.264;*.h265;*.265;*.hevc;*.avc;*.mkv;*.mp4;*.m4v;*.webm\0Tutti i file\0*.*\0\0"));
        break;
    case RemoveExternal:
        if (Job* job = currentJob()) {
            int row = selectedRow(Tracks);
            if (row >= 0 && job->tracks[row].source > 0) { removeSource(*job, job->tracks[row].source); touched(); fillTracks(); }
        }
        break;
    case ApplyAll: applyToAll(); break;
    case FilterApply:
        if (Job* job = currentJob()) {
            if (!job->sources.front().analyzed) throw std::runtime_error("Analizzare prima il file");
            keepLanguages(*job, get(LangFilter));
            touched();
            fillTracks();
            status(L"Audio e sottotitoli filtrati per lingua: verificare la lista, poi eventualmente Applica a tutti i file");
        }
        break;
    case EditName:
        if (code == EN_CHANGE) editSelected([](Job&, TrackEdit& e, int) { e.name = get(EditName); });
        break;
    case EditLang:
        if (code == CBN_EDITCHANGE) editSelected([](Job&, TrackEdit& e, int) { e.language = get(EditLang); });
        else if (code == CBN_SELCHANGE) {
            int i = static_cast<int>(SendMessageW(control(EditLang), CB_GETCURSEL, 0, 0));
            if (i >= 0 && i < static_cast<int>(std::size(languageChoices))) {
                std::wstring code3 = std::wstring(languageChoices[i]).substr(0, 3);
                editSelected([&](Job&, TrackEdit& e, int) { e.language = code3; });
                PostMessageW(window, LangCodeMessage, 0, 0);
            }
        }
        break;
    case EditDefault:
        if (code == BN_CLICKED) {
            editSelected([](Job& job, TrackEdit&, int row) { setDefault(job, static_cast<size_t>(row), checked(EditDefault), checked(Exclusive)); });
            if (Job* job = currentJob()) for (int r = 0; r < static_cast<int>(job->tracks.size()); ++r) trackRow(r);
        }
        break;
    case EditForced:
        if (code == BN_CLICKED) editSelected([](Job&, TrackEdit& e, int) { e.forced = checked(EditForced); });
        break;
    case EditDelay:
        if (code == EN_CHANGE) editSelected([](Job&, TrackEdit& e, int) {
            auto text = get(EditDelay);
            wchar_t* end = nullptr;
            long v = wcstol(text.c_str(), &end, 10);
            if (text.empty() || text == L"-") v = 0;
            else if (!end || *end || v < -36000000 || v > 36000000) return;
            e.delayMs = static_cast<int>(v);
        });
        break;
    case RenBrowse: { auto p = folder(); if (!p.empty()) { put(RenFolder, p); refreshRename(); } break; }
    case RenUseOutput: put(RenFolder, get(Output)); refreshRename(); break;
    case RenRefresh: refreshRename(); break;
    case RenFind: case RenWith: case RenPattern: if (code == EN_CHANGE) refreshRename(); break;
    case RenIgnoreCase: if (code == BN_CLICKED) refreshRename(); break;
    case RenApply: renameNow(); break;
    case RenUndo: undoRename(); break;
    case Start: start(); break;
    case Cancel: cancelled = true; startAfterAnalysis = false; status(L"Annullamento in corso..."); break;
    }
}

LRESULT notify(NMHDR* hdr) {
    if (hdr->idFrom == Queue) {
        if (hdr->code == NM_DBLCLK) openJob(reinterpret_cast<NMITEMACTIVATE*>(hdr)->iItem);
        else if (hdr->code == LVN_ITEMCHANGED) {
            auto* nm = reinterpret_cast<NMLISTVIEW*>(hdr);
            if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) && !busy && nm->iItem != current) {
                current = nm->iItem;
                SendMessageW(control(JobCombo), CB_SETCURSEL, current, 0);
                fillTracks();
            }
        } else if (hdr->code == LVN_KEYDOWN && reinterpret_cast<NMLVKEYDOWN*>(hdr)->wVKey == VK_DELETE && !busy) removeQueued();
    } else if (hdr->idFrom == Tracks && hdr->code == LVN_ITEMCHANGED && !filling) {
        auto* nm = reinterpret_cast<NMLISTVIEW*>(hdr);
        if (!(nm->uChanged & LVIF_STATE)) return 0;
        if ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) {
            Job* job = currentJob();
            if (job && nm->iItem >= 0 && nm->iItem < static_cast<int>(job->tracks.size()) && !busy) {
                job->tracks[nm->iItem].keep = ((nm->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
                touched();
            }
        }
        if ((nm->uNewState ^ nm->uOldState) & LVIS_SELECTED) fillPanel();
    } else if (hdr->idFrom == Tracks && hdr->code == NM_DBLCLK) {
        SetFocus(control(EditName));
        SendMessageW(control(EditName), EM_SETSEL, 0, -1);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Costruzione della finestra
// ---------------------------------------------------------------------------
void build() {
    dpi = GetDpiForSystem();
    font = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    label(L"MKV Flow " MKVFLOW_VERSION_W L"  /  Flussi, rinomina, verifica", 12, 12, 736, GrowW);
    int nav = NavQueue;
    for (auto text : {L"Coda", L"Flussi", L"Rinomina", L"Impostazioni", L"Log"}) {
        make(L"BUTTON", text, nav, 12 + (nav - NavQueue) * 120, 40, 114, 28,
             WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | (nav == NavQueue ? WS_GROUP : 0));
        ++nav;
    }

    buildingPage = PageQueue;
    button(L"Aggiungi MKV...", Add, 12, 80, 124);
    button(L"Rimuovi", Remove, 140, 80, 80);
    button(L"Svuota", Clear, 224, 80, 72);
    button(L"Analizza tutto", AnalyzeAll, 300, 80, 112);
    button(L"Abbina tracce esterne", Companions, 416, 80, 168);
    label(L"Trascina qui file MKV o cartelle. Doppio clic su un file per modificarne i flussi.", 12, 114, 736, GrowW);
    listView(Queue, 12, 134, 736, 232, 0, 0, {{L"File", 380}, {L"Flussi", 150}, {L"Esterni", 60}, {L"Stato", 140}});
    label(L"Output", 12, 382, 70, MoveY);
    edit(L"", Output, 86, 378, 556, MoveY | GrowW);
    button(L"Sfoglia...", OutputBrowse, 650, 377, 98, MoveX | MoveY);
    label(L"Nome", 12, 416, 70, MoveY);
    edit(L"{name} [{crc}]", Pattern, 86, 412, 662, MoveY | GrowW);
    label(L"{name} nome originale  ·  {n} posizione in coda  ·  {crc} CRC32 finale  ·  .mkv aggiunto da solo", 12, 446, 736, MoveY | GrowW);

    buildingPage = PageTracks;
    label(L"File", 12, 84, 36);
    make(L"COMBOBOX", L"", JobCombo, 52, 80, 446, 300, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, GrowW);
    button(L"Analizza", AnalyzeOne, 506, 79, 100, MoveX);
    button(L"Ripristina", Reset, 610, 79, 138, MoveX);
    button(L"▲ Su", MoveUp, 12, 112, 64);
    button(L"▼ Giù", MoveDown, 80, 112, 64);
    button(L"Aggiungi traccia esterna...", AddExternal, 152, 112, 196);
    button(L"Rimuovi esterna", RemoveExternal, 352, 112, 128);
    button(L"Applica a tutti i file", ApplyAll, 588, 112, 160, MoveX);
    listView(Tracks, 12, 146, 736, 194, LVS_SINGLESEL, LVS_EX_CHECKBOXES,
             {{L"#", 44}, {L"Tipo", 100}, {L"Codec", 100}, {L"Lingua", 64}, {L"Nome", 150}, {L"Pred.", 46}, {L"Forz.", 46},
              {L"Ritardo", 64}, {L"Dettagli", 96}, {L"Origine", 120}});
    label(L"Nome", 12, 352, 44, MoveY);
    edit(L"", EditName, 60, 348, 300, MoveY | GrowW);
    label(L"Lingua", 372, 352, 48, MoveX | MoveY);
    make(L"COMBOBOX", L"", EditLang, 422, 348, 126, 300, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, MoveX | MoveY);
    for (auto text : languageChoices) SendMessageW(control(EditLang), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessageW(control(EditLang), CB_SETDROPPEDWIDTH, px(190), 0);
    checkbox(L"Predefinita", EditDefault, 560, 349, 96, MoveX | MoveY);
    checkbox(L"Forzata", EditForced, 660, 349, 88, MoveX | MoveY);
    label(L"Ritardo ms", 12, 386, 72, MoveY);
    edit(L"0", EditDelay, 88, 382, 80, MoveY);
    label(L"Spunta = includi  ·  Spazio = includi/escludi  ·  Ctrl+↑/↓ = sposta  ·  trascina qui audio/sottotitoli", 180, 386, 568, MoveY | GrowW);
    label(L"Mantieni lingue", 12, 420, 104, MoveY);
    edit(L"ita, jpn", LangFilter, 118, 416, 120, MoveY);
    button(L"Filtra audio e sottotitoli", FilterApply, 244, 415, 184, MoveY);
    checkbox(L"Imposta titolo", SetTitle, 12, 451, 128, MoveY);
    edit(L"{name}", Title, 144, 450, 604, MoveY | GrowW);

    buildingPage = PageRename;
    label(L"Cartella", 12, 84, 60);
    edit(L"", RenFolder, 76, 80, 446, GrowW);
    button(L"Sfoglia...", RenBrowse, 530, 79, 98, MoveX);
    button(L"Usa output", RenUseOutput, 632, 79, 116, MoveX);
    label(L"Trova", 12, 118, 60);
    edit(L"", RenFind, 76, 114, 250);
    label(L"Sostituisci con", 338, 118, 104);
    edit(L"", RenWith, 446, 114, 302, GrowW);
    label(L"Nome", 12, 152, 60);
    edit(L"{name} [{crc}]", RenPattern, 76, 148, 250);
    label(L"{name} nome senza vecchio CRC  ·  {crc} CRC32  ·  {n} numero", 338, 152, 410, GrowW);
    checkbox(L"Ignora maiuscole/minuscole", RenIgnoreCase, 12, 180, 210);
    checkbox(L"Verifica i CRC già presenti nel nome (rilegge i file)", RenVerify, 12, 204, 736, GrowW);
    checkbox(L"Aggiorna anche il titolo interno degli MKV (mkvpropedit, modifica i file e il CRC)", RenTitle, 12, 228, 736, GrowW);
    checkbox(L"Usa la stessa sostituzione per i nomi e i titoli creati dal batch", RenBatch, 12, 252, 736, GrowW);
    listView(RenPreview, 12, 280, 736, 150, 0, 0, {{L"Nome attuale", 300}, {L"Nuovo nome", 300}, {L"Stato", 120}});
    button(L"Rinomina", RenApply, 12, 440, 120, MoveY);
    button(L"Annulla ultima rinomina", RenUndo, 136, 440, 184, MoveY);
    button(L"Aggiorna anteprima", RenRefresh, 324, 440, 150, MoveY);

    buildingPage = PageSettings;
    label(L"Cartella MKVToolNix", 12, 84, 400);
    edit(L"", Tools, 12, 106, 630, GrowW);
    button(L"Sfoglia...", ToolsBrowse, 650, 105, 98, MoveX);
    make(L"STATIC", L"Serve la cartella con mkvmerge.exe e mkvpropedit.exe. MKVToolNix resta esterno al programma.", 0, 12, 140, 736, 22, SS_NOPREFIX, GrowW);
    checkbox(L"Copia veloce quando la struttura non cambia (solo intestazioni, niente remux)", FastCopy, 12, 178, 736, GrowW);
    checkbox(L"Una sola traccia predefinita per tipo (attivarne una disattiva le altre)", Exclusive, 12, 206, 736, GrowW);
    checkbox(L"Rimuovi capitoli", NoChapters, 12, 234, 736, GrowW);
    checkbox(L"Rimuovi allegati (font e immagini)", NoAttachments, 12, 262, 736, GrowW);
    checkbox(L"Rimuovi tag globali", NoTags, 12, 290, 736, GrowW);
    make(L"STATIC",
         L"Rimozioni, riordino, tracce esterne e ritardi richiedono un remux con mkvmerge, senza ricodifica.\r\n"
         L"Gli originali restano intatti, il CRC32 è calcolato sul file finale e i nomi esistenti non vengono sovrascritti.",
         0, 12, 328, 736, 44, SS_NOPREFIX, GrowW);

    buildingPage = PageLog;
    make(L"EDIT", L"", LogBox, 12, 80, 736, 396, WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, GrowW | GrowH);
    SendMessageW(control(LogBox), EM_SETLIMITTEXT, 1000000, 0);

    buildingPage = -1;  // barra inferiore
    make(L"BUTTON", L"Avvia batch", Start, 12, 494, 112, 32, WS_TABSTOP, MoveY);
    make(L"BUTTON", L"Annulla", Cancel, 130, 494, 90, 32, WS_TABSTOP, MoveY);
    EnableWindow(control(Cancel), FALSE);
    make(PROGRESS_CLASSW, L"", ProgressBar, 230, 501, 160, 18, PBS_SMOOTH, MoveY);
    SendMessageW(control(ProgressBar), PBM_SETRANGE32, 0, 1000);
    make(L"STATIC", L"Pronto", Status, 400, 501, 348, 22, SS_NOPREFIX | SS_ENDELLIPSIS, MoveY | GrowW);

    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        settings = fs::path(local) / L"MKVFlow";
        CoTaskMemFree(local);
        std::error_code ec;
        fs::create_directories(settings, ec);
        settings /= L"settings.ini";
        if (!fs::exists(settings, ec)) {
            std::ofstream ini(settings, std::ios::binary);
            ini.put(static_cast<char>(0xFF));
            ini.put(static_cast<char>(0xFE));
        }
    }
    put(Tools, readSetting(L"Tools", L"C:\\Program Files\\MKVToolNix"));
    put(Output, readSetting(L"Output", L""));
    put(Pattern, readSetting(L"Pattern", L"{name} [{crc}]"));
    put(LangFilter, readSetting(L"Languages", L"ita, jpn"));
    check(FastCopy, readSetting(L"FastCopy", L"1") == L"1");
    check(Exclusive, readSetting(L"ExclusiveDefault", L"1") == L"1");
    check(NoChapters, readSetting(L"NoChapters", L"0") == L"1");
    check(NoAttachments, readSetting(L"NoAttachments", L"0") == L"1");
    check(NoTags, readSetting(L"NoGlobalTags", L"0") == L"1");
    put(RenFolder, readSetting(L"RenameFolder", L""));
    put(RenFind, readSetting(L"RenameFind", L""));
    put(RenWith, readSetting(L"RenameWith", L""));
    put(RenPattern, readSetting(L"RenamePattern", L"{name} [{crc}]"));
    check(RenIgnoreCase, readSetting(L"RenameIgnoreCase", L"1") == L"1");
    EnableWindow(control(RenUndo), FALSE);
    DragAcceptFiles(window, TRUE);
    showPage(PageQueue);
    fillTracks();
}

void dropFiles(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<fs::path> paths;
    for (UINT i = 0; i < n; ++i) {
        std::vector<wchar_t> p(DragQueryFileW(drop, i, nullptr, 0) + 1);
        DragQueryFileW(drop, i, p.data(), static_cast<UINT>(p.size()));
        paths.push_back(p.data());
    }
    DragFinish(drop);
    if (busy) return;
    if (currentPage == PageTracks && currentJob()) {
        // Nella scheda Flussi i file trascinati diventano tracce del file aperto.
        std::vector<fs::path> files;
        std::error_code ec;
        for (const auto& p : paths) if (fs::is_regular_file(p, ec)) files.push_back(p);
        addExternals(files);
        return;
    }
    for (const auto& p : paths) addPath(p);
    refreshQueue();
}

LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
        case WM_CREATE: window = hwnd; build(); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            RECT bounds{0, 0, MulDiv(BaseWidth, dpi, 96), MulDiv(BaseHeight, dpi, 96)};
            AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
            reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
            return 0;
        }
        case WM_DROPFILES: dropFiles(reinterpret_cast<HDROP>(w)); return 0;
        case WM_COMMAND: command(LOWORD(w), HIWORD(w)); return 0;
        case WM_NOTIFY: return notify(reinterpret_cast<NMHDR*>(l));
        case LogMessage: { std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(l)); logText(*s); return 0; }
        case RenamedMessage: renamed(std::unique_ptr<Renamed>(reinterpret_cast<Renamed*>(l))); return 0;
        case AnalyzedMessage: analyzed(std::unique_ptr<Analyzed>(reinterpret_cast<Analyzed*>(l))); return 0;
        case StateMessage: {
            std::unique_ptr<StateUpdate> s(reinterpret_cast<StateUpdate*>(l));
            if (s->job < items.size()) { items[s->job].state = s->text; queueRow(s->job); }
            return 0;
        }
        case ProgressMessage: SendMessageW(control(ProgressBar), PBM_SETPOS, w, 0); return 0;
        case LangCodeMessage:
            if (Job* job = currentJob()) {
                int row = selectedRow(Tracks);
                if (row >= 0) { filling = true; put(EditLang, job->tracks[row].language); filling = false; }
            }
            return 0;
        case DoneMessage: {
            if (worker.joinable()) worker.join();
            bool wasBatch = batchRunning, wasRename = renameRunning;
            batchRunning = renameRunning = false;
            setBusy(false);
            if (closeAfter) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
            if (cancelled) { startAfterAnalysis = false; status(L"Annullato — consulta il log"); return 0; }
            if (wasRename) { EnableWindow(control(RenUndo), !lastRename.empty()); return 0; }
            if (wasBatch) { status(L"Batch terminato — consulta il log"); showPage(PageLog); return 0; }
            if (startAfterAnalysis) {
                startAfterAnalysis = false;
                bool ready = std::all_of(items.begin(), items.end(), [](const Item& i) { return i.job.analyzed(); });
                if (ready) launchBatch();
                else { status(L"Alcuni file non sono stati analizzati: vedere il Log"); showPage(PageLog); }
                return 0;
            }
            status(L"Analisi completata");
            return 0;
        }
        case WM_CLOSE:
            if (busy) { closeAfter = true; cancelled = true; status(L"Chiusura dopo annullamento..."); return 0; }
            saveSettings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: DeleteObject(font); PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) {
        MessageBoxW(hwnd, errorText(e).c_str(), L"MKV Flow", MB_OK | MB_ICONERROR);
        if (msg == WM_COMMAND || msg == WM_NOTIFY) return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int show) {
    instance = h;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&cc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = procedure;
    wc.hInstance = h;
    wc.lpszClassName = L"MKVFlowWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(h, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(BaseWidth, dpi, 96), MulDiv(BaseHeight, dpi, 96)};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MKV Flow " MKVFLOW_VERSION_W, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, h, nullptr);
    if (!hwnd) { CoUninitialize(); return 1; }
    ShowWindow(hwnd, show);
    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        // Ctrl+↑/↓ sposta la traccia selezionata.
        if (m.message == WM_KEYDOWN && (m.wParam == VK_UP || m.wParam == VK_DOWN) && (GetKeyState(VK_CONTROL) & 0x8000) &&
            m.hwnd == GetDlgItem(hwnd, Tracks)) {
            moveTrack(m.wParam == VK_UP ? -1 : 1);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    CoUninitialize();
    return static_cast<int>(m.wParam);
}
