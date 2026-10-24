#include "core.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace app {
namespace {

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

void checkCancel(std::atomic_bool& c) { if (c) throw std::runtime_error("Operazione annullata"); }

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

[[noreturn]] void fail(const std::wstring& message) { throw std::runtime_error(narrow(message)); }

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n"), b = s.find_last_not_of(L" \t\r\n");
    return a == s.npos ? std::wstring() : s.substr(a, b - a + 1);
}

std::wstring join(const std::vector<std::wstring>& parts, const wchar_t* sep) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) { if (i) out += sep; out += parts[i]; }
    return out;
}

// ---------------------------------------------------------------------------
// JSON minimale, sufficiente per l'output di `mkvmerge -J`.
// ---------------------------------------------------------------------------
namespace json {
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> items;               // Array, oppure valori dell'Object
    std::vector<std::string> keys;          // chiavi dell'Object, parallele a items

    const Value& operator[](const char* key) const {
        static const Value none;
        if (type == Object)
            for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == key) return items[i];
        return none;
    }
    std::string str() const { return type == String ? s : std::string(); }
    long long integer(long long fallback = 0) const { return type == Number ? static_cast<long long>(n) : fallback; }
    bool boolean(bool fallback = false) const { return type == Bool ? b : fallback; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : t(text) {}
    Value parse() {
        Value v = value(0);
        ws();
        if (p != t.size()) bad();
        return v;
    }
private:
    const std::string& t;
    size_t p = 0;
    [[noreturn]] void bad() { throw std::runtime_error("Risposta JSON di mkvmerge non valida"); }
    void ws() { while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\r' || t[p] == '\n')) ++p; }
    bool literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (t.compare(p, n, word) != 0) return false;
        p += n;
        return true;
    }
    static void utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    uint32_t hex4() {
        if (p + 4 > t.size()) bad();
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else bad();
        }
        return v;
    }
    std::string string() {
        if (t[p] != '"') bad();
        ++p;
        std::string out;
        for (;;) {
            if (p >= t.size()) bad();
            char c = t[p++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (p >= t.size()) bad();
            switch (char e = t[p++]) {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                    p += 2;
                    uint32_t low = hex4();
                    if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    else { utf8(out, 0xFFFD); cp = low; }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                utf8(out, cp);
                break;
            }
            default: bad();
            }
        }
    }
    Value value(int depth) {
        if (depth > 64) bad();
        ws();
        if (p >= t.size()) bad();
        Value v;
        char c = t[p];
        if (c == '{') {
            v.type = Value::Object;
            ++p; ws();
            if (p < t.size() && t[p] == '}') { ++p; return v; }
            for (;;) {
                ws();
                v.keys.push_back(string());
                ws();
                if (p >= t.size() || t[p++] != ':') bad();
                v.items.push_back(value(depth + 1));
                ws();
                if (p >= t.size()) bad();
                if (t[p] == ',') { ++p; continue; }
                if (t[p] == '}') { ++p; return v; }
                bad();
            }
        }
        if (c == '[') {
            v.type = Value::Array;
            ++p; ws();
            if (p < t.size() && t[p] == ']') { ++p; return v; }
            for (;;) {
                v.items.push_back(value(depth + 1));
                ws();
                if (p >= t.size()) bad();
                if (t[p] == ',') { ++p; continue; }
                if (t[p] == ']') { ++p; return v; }
                bad();
            }
        }
        if (c == '"') { v.type = Value::String; v.s = string(); return v; }
        if (literal("true")) { v.type = Value::Bool; v.b = true; return v; }
        if (literal("false")) { v.type = Value::Bool; return v; }
        if (literal("null")) return v;
        size_t start = p;
        if (t[p] == '-') ++p;
        while (p < t.size() && t[p] != '\0' && std::strchr("0123456789.eE+-", t[p])) ++p;
        if (start == p) bad();
        std::string number = t.substr(start, p - start);
        char* end = nullptr;
        v.type = Value::Number;
        v.n = std::strtod(number.c_str(), &end);
        if (!end || *end) bad();
        return v;
    }
};
Value parse(const std::string& text) { return Parser(text).parse(); }
}  // namespace json

Kind kindOf(const std::string& type) {
    if (type == "video") return Kind::Video;
    if (type == "audio") return Kind::Audio;
    if (type == "subtitles") return Kind::Subtitles;
    return Kind::Other;
}

bool sameFile(const fs::path& a, const fs::path& b) { return _wcsicmp(a.wstring().c_str(), b.wstring().c_str()) == 0; }

const std::vector<std::pair<const wchar_t*, const wchar_t*>>& languageWords() {
    static const std::vector<std::pair<const wchar_t*, const wchar_t*>> words = {
        {L"ita", L"ita"}, {L"it", L"ita"}, {L"italian", L"ita"}, {L"italiano", L"ita"},
        {L"eng", L"eng"}, {L"en", L"eng"}, {L"english", L"eng"}, {L"inglese", L"eng"},
        {L"jpn", L"jpn"}, {L"ja", L"jpn"}, {L"jap", L"jpn"}, {L"japanese", L"jpn"}, {L"giapponese", L"jpn"},
        {L"spa", L"spa"}, {L"es", L"spa"}, {L"spanish", L"spa"}, {L"spagnolo", L"spa"},
        {L"fre", L"fre"}, {L"fra", L"fre"}, {L"fr", L"fre"}, {L"french", L"fre"}, {L"francese", L"fre"},
        {L"ger", L"ger"}, {L"deu", L"ger"}, {L"de", L"ger"}, {L"german", L"ger"}, {L"tedesco", L"ger"},
        {L"por", L"por"}, {L"pt", L"por"}, {L"portuguese", L"por"}, {L"portoghese", L"por"},
        {L"rus", L"rus"}, {L"ru", L"rus"}, {L"russian", L"rus"}, {L"russo", L"rus"},
        {L"chi", L"chi"}, {L"zho", L"chi"}, {L"zh", L"chi"}, {L"chinese", L"chi"}, {L"cinese", L"chi"},
        {L"kor", L"kor"}, {L"ko", L"kor"}, {L"korean", L"kor"}, {L"coreano", L"kor"},
    };
    return words;
}

bool isContainer(const std::wstring& ext) { return ext == L".mkv" || ext == L".mp4" || ext == L".m4v" || ext == L".webm"; }

}  // namespace

// ---------------------------------------------------------------------------
// Utilità generali
// ---------------------------------------------------------------------------
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring kindName(Kind kind) {
    switch (kind) {
    case Kind::Video: return L"Video";
    case Kind::Audio: return L"Audio";
    case Kind::Subtitles: return L"Sottotitoli";
    default: return L"Altro";
    }
}

wchar_t kindLetter(Kind kind) {
    switch (kind) {
    case Kind::Video: return L'v';
    case Kind::Audio: return L'a';
    case Kind::Subtitles: return L's';
    default: return L'o';
    }
}

std::wstring quote(const std::wstring& s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

std::wstring expand(std::wstring text, const std::wstring& name, size_t index, const std::wstring& crc) {
    // Sostituzione in un solo passaggio: anche i nomi in ingresso possono contenere graffe.
    std::wstring out;
    for (size_t p = 0; p < text.size();) {
        if (text.compare(p, 6, L"{name}") == 0) { out += name; p += 6; }
        else if (text.compare(p, 3, L"{n}") == 0) { out += std::to_wstring(index); p += 3; }
        else if (text.compare(p, 5, L"{crc}") == 0) { out += crc; p += 5; }
        else out += text[p++];
    }
    return out;
}

std::wstring fileName(const std::wstring& text) {
    if (text.empty() || text.size() > 220 || text == L"." || text == L".." || text.back() == L'.' || text.back() == L' ')
        throw std::runtime_error("Nome finale vuoto, troppo lungo o con punto/spazio finale");
    for (wchar_t c : text)
        if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos)
            throw std::runtime_error("Carattere non valido nel nome finale");
    auto base = text.substr(0, text.find(L'.'));
    std::transform(base.begin(), base.end(), base.begin(), towupper);
    if (std::regex_match(base, std::wregex(L"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]")))
        throw std::runtime_error("Nome riservato da Windows");
    return text + L".mkv";
}

std::wstring outputStem(const Job& job, const Options& o) {
    return replaceText(job.input().stem().wstring(), o.replaceFind, o.replaceWith, o.replaceIgnoreCase);
}

bool validLanguage(const std::wstring& code) {
    return code.empty() || std::regex_match(code, std::wregex(L"[A-Za-z]{2,3}(-[A-Za-z0-9]{1,8})*"));
}

std::wstring guessLanguage(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t c : lower(text) + L" ") {
        if (std::iswalnum(c)) current += c;
        else if (!current.empty()) { tokens.push_back(current); current.clear(); }
    }
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
        for (const auto& [word, code] : languageWords())
            if (*it == word) return code;
    return {};
}

bool isExternalTrackFile(const fs::path& file) {
    static const std::set<std::wstring> extensions = {
        L".mka", L".ac3", L".eac3", L".ec3", L".dts", L".dtshd", L".thd", L".truehd", L".aac", L".m4a", L".flac",
        L".opus", L".ogg", L".mp3", L".wav", L".mp2", L".srt", L".ass", L".ssa", L".sup", L".vtt", L".idx",
        L".mks", L".usf", L".h264", L".264", L".h265", L".265", L".hevc", L".avc", L".mkv", L".mp4", L".m4v", L".webm"};
    return extensions.count(lower(file.extension().wstring())) > 0;
}

std::vector<fs::path> findCompanions(const fs::path& mkv) {
    std::vector<fs::path> found;
    auto stem = lower(mkv.stem().wstring());
    std::error_code ec;
    for (fs::directory_iterator it(mkv.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        std::error_code e2;
        if (!it->is_regular_file(e2) || sameFile(p, mkv) || !isExternalTrackFile(p)) continue;
        if (isContainer(lower(p.extension().wstring()))) continue;
        auto name = lower(p.filename().wstring());
        // "Episodio 1.ita.ac3" sì, "Episodio 10.srt" no: dopo il nome serve un separatore.
        if (name.size() > stem.size() && name.compare(0, stem.size(), stem) == 0 &&
            std::wstring(L". _-[(").find(name[stem.size()]) != std::wstring::npos)
            found.push_back(p);
    }
    std::sort(found.begin(), found.end(), [](const fs::path& a, const fs::path& b) { return _wcsicmp(a.wstring().c_str(), b.wstring().c_str()) < 0; });
    return found;
}

// ---------------------------------------------------------------------------
// Modello dei flussi
// ---------------------------------------------------------------------------
bool Job::analyzed() const {
    return std::all_of(sources.begin(), sources.end(), [](const Source& s) { return s.analyzed; });
}

Job makeJob(const fs::path& mkv) {
    Job job;
    Source main;
    main.path = mkv;
    job.sources.push_back(main);
    return job;
}

Source parseIdentify(const std::string& text, const fs::path& path) {
    auto root = json::parse(text);
    const auto& container = root["container"];
    if (!container["recognized"].boolean() || !container["supported"].boolean()) {
        std::vector<std::wstring> errors;
        for (const auto& e : root["errors"].items) errors.push_back(widen(e.str()));
        fail(L"Formato non riconosciuto da mkvmerge: " + path.filename().wstring() +
             (errors.empty() ? L"" : L" — " + join(errors, L" ")));
    }
    Source s;
    s.path = path;
    s.analyzed = true;
    s.title = widen(container["properties"]["title"].str());
    int counts[4] = {};
    for (const auto& t : root["tracks"].items) {
        SourceTrack x;
        const auto& p = t["properties"];
        x.id = static_cast<int>(t["id"].integer(-1));
        x.kind = kindOf(t["type"].str());
        x.ordinal = ++counts[static_cast<int>(x.kind)];
        x.number = static_cast<int>(p["number"].integer(0));
        x.codec = widen(t["codec"].str());
        x.language = widen(p["language"].str());
        if (x.language.empty()) x.language = widen(p["language_ietf"].str());
        x.name = widen(p["track_name"].str());
        x.isDefault = p["default_track"].boolean(false);
        x.forced = p["forced_track"].boolean(false);
        if (x.kind == Kind::Video) {
            x.info = widen(p["pixel_dimensions"].str());
        } else if (x.kind == Kind::Audio) {
            auto ch = p["audio_channels"].integer(0), hz = p["audio_sampling_frequency"].integer(0);
            if (ch) x.info = std::to_wstring(ch) + L" ch";
            if (hz) {
                if (!x.info.empty()) x.info += L", ";
                x.info += hz % 1000 ? std::to_wstring(hz) + L" Hz" : std::to_wstring(hz / 1000) + L" kHz";
            }
        }
        if (x.id < 0) fail(L"Traccia senza ID in " + path.filename().wstring());
        s.tracks.push_back(x);
    }
    return s;
}

TrackEdit defaultEdit(const Job& job, size_t source, size_t track) {
    const auto& src = job.sources[source];
    const auto& st = src.tracks[track];
    TrackEdit e;
    e.source = source;
    e.track = track;
    e.name = st.name;
    e.language = st.language;
    e.isDefault = st.isDefault;
    e.forced = st.forced;
    if (source > 0) {
        // Le tracce esterne non diventano predefinite da sole: si sceglie nell'editor.
        e.isDefault = false;
        if ((e.language.empty() || e.language == L"und") && !src.languageHint.empty()) e.language = src.languageHint;
        if (src.forcedHint && st.kind == Kind::Subtitles) e.forced = true;
    }
    return e;
}

void resetEdits(Job& job) {
    job.tracks.clear();
    job.customized = false;
    syncEdits(job);
}

void syncEdits(Job& job) {
    std::set<std::pair<size_t, size_t>> seen;
    std::vector<TrackEdit> kept;
    for (const auto& e : job.tracks) {
        if (e.source >= job.sources.size() || !job.sources[e.source].analyzed) continue;
        if (e.track >= job.sources[e.source].tracks.size()) continue;
        if (!seen.insert({e.source, e.track}).second) continue;
        kept.push_back(e);
    }
    job.tracks = std::move(kept);
    for (size_t s = 0; s < job.sources.size(); ++s) {
        if (!job.sources[s].analyzed) continue;
        for (size_t t = 0; t < job.sources[s].tracks.size(); ++t)
            if (!seen.count({s, t})) job.tracks.push_back(defaultEdit(job, s, t));
    }
}

size_t addExternal(Job& job, const fs::path& file) {
    for (size_t i = 0; i < job.sources.size(); ++i)
        if (sameFile(job.sources[i].path, file)) {
            if (i == 0) throw std::runtime_error("Il file principale non può essere aggiunto come traccia esterna");
            return i;
        }
    Source s;
    s.path = file;
    // Si guarda solo la parte di nome dopo quella del video: "Film.ita.ac3" → "ita".
    auto mainStem = lower(job.input().stem().wstring());
    auto stem = file.stem().wstring();
    auto rest = lower(stem).compare(0, mainStem.size(), mainStem) == 0 ? stem.substr(mainStem.size()) : stem;
    s.languageHint = guessLanguage(rest);
    auto lrest = lower(rest);
    s.forcedHint = lrest.find(L"forced") != lrest.npos || lrest.find(L"forzat") != lrest.npos;
    job.sources.push_back(s);
    job.customized = true;
    return job.sources.size() - 1;
}

void removeSource(Job& job, size_t source) {
    if (source == 0 || source >= job.sources.size()) return;
    job.sources.erase(job.sources.begin() + static_cast<std::ptrdiff_t>(source));
    std::vector<TrackEdit> kept;
    for (auto e : job.tracks) {
        if (e.source == source) continue;
        if (e.source > source) --e.source;
        kept.push_back(e);
    }
    job.tracks = std::move(kept);
}

void setDefault(Job& job, size_t row, bool value, bool exclusive) {
    if (row >= job.tracks.size()) return;
    job.tracks[row].isDefault = value;
    if (value && exclusive) {
        Kind kind = job.sourceTrack(job.tracks[row]).kind;
        for (size_t i = 0; i < job.tracks.size(); ++i)
            if (i != row && job.sourceTrack(job.tracks[i]).kind == kind) job.tracks[i].isDefault = false;
    }
    job.customized = true;
}

void keepLanguages(Job& job, const std::wstring& csv) {
    std::set<std::wstring> wanted;
    std::wstring item;
    for (wchar_t c : csv + L",") {
        if (c == L',' || c == L';' || c == L' ') {
            item = lower(trim(item));
            if (!item.empty()) wanted.insert(item);
            item.clear();
        } else item += c;
    }
    if (wanted.empty()) throw std::runtime_error("Indicare almeno una lingua, ad esempio: ita, jpn");
    for (auto& e : job.tracks) {
        Kind kind = job.sourceTrack(e).kind;
        if (kind != Kind::Audio && kind != Kind::Subtitles) continue;
        auto lang = lower(e.language.empty() ? std::wstring(L"und") : e.language);
        e.keep = wanted.count(lang) > 0;
    }
    job.customized = true;
}

bool hasChanges(const Job& job) {
    for (const auto& e : job.tracks) {
        const auto& st = job.sourceTrack(e);
        if (!e.keep || e.delayMs || e.source > 0 || e.name != st.name || e.language != st.language ||
            e.isDefault != st.isDefault || e.forced != st.forced)
            return true;
    }
    return false;
}

bool structural(const Job& job) {
    std::pair<size_t, size_t> last{0, 0};
    bool first = true;
    for (const auto& e : job.tracks) {
        if (!e.keep || e.delayMs || e.source > 0) return true;
        std::pair<size_t, size_t> key{e.source, e.track};
        if (!first && key < last) return true;
        last = key;
        first = false;
    }
    // Tutte le tracce del file principale devono essere presenti nella lista.
    size_t mainTracks = std::count_if(job.tracks.begin(), job.tracks.end(), [](const TrackEdit& e) { return e.source == 0; });
    return mainTracks != job.sources.front().tracks.size();
}

std::wstring summary(const Job& job) {
    if (!job.sources.front().analyzed) return L"—";
    int kept[4] = {}, removed = 0;
    for (const auto& e : job.tracks) {
        if (e.keep) ++kept[static_cast<int>(job.sourceTrack(e).kind)];
        else ++removed;
    }
    std::vector<std::wstring> parts;
    if (kept[0]) parts.push_back(std::to_wstring(kept[0]) + L" V");
    if (kept[1]) parts.push_back(std::to_wstring(kept[1]) + L" A");
    if (kept[2]) parts.push_back(std::to_wstring(kept[2]) + L" S");
    if (kept[3]) parts.push_back(std::to_wstring(kept[3]) + L" altro");
    auto text = parts.empty() ? std::wstring(L"nessuna traccia") : join(parts, L" · ");
    if (removed) text += L"  (−" + std::to_wstring(removed) + L")";
    return text;
}

Template makeTemplate(const Job& job) {
    Template t;
    for (const auto& e : job.tracks) {
        if (e.source != 0) continue;
        const auto& st = job.sourceTrack(e);
        Template::Entry x;
        x.kind = st.kind;
        x.ordinal = st.ordinal;
        x.keep = e.keep;
        x.delayMs = e.delayMs;
        // Si copiano solo le proprietà cambiate: le altre restano quelle di ciascun file.
        if (e.name != st.name) x.name = e.name;
        if (e.language != st.language) x.language = e.language;
        if (e.isDefault != st.isDefault) x.isDefault = e.isDefault;
        if (e.forced != st.forced) x.forced = e.forced;
        t.entries.push_back(x);
    }
    return t;
}

std::wstring applyTemplate(Job& job, const Template& t) {
    if (!job.sources.front().analyzed) return L"file non ancora analizzato";
    std::vector<TrackEdit> result, externals;
    std::vector<bool> used(job.tracks.size(), false);
    std::vector<std::wstring> warnings;
    for (const auto& x : t.entries) {
        bool found = false;
        for (size_t i = 0; i < job.tracks.size(); ++i) {
            const auto& e = job.tracks[i];
            if (used[i] || e.source != 0) continue;
            const auto& st = job.sourceTrack(e);
            if (st.kind != x.kind || st.ordinal != x.ordinal) continue;
            TrackEdit n = defaultEdit(job, 0, e.track);
            n.keep = x.keep;
            n.delayMs = x.delayMs;
            if (x.name) n.name = *x.name;
            if (x.language) n.language = *x.language;
            if (x.isDefault) n.isDefault = *x.isDefault;
            if (x.forced) n.forced = *x.forced;
            result.push_back(n);
            used[i] = true;
            found = true;
            break;
        }
        if (!found && x.keep) warnings.push_back(L"manca " + std::wstring(1, kindLetter(x.kind)) + std::to_wstring(x.ordinal));
    }
    for (size_t i = 0; i < job.tracks.size(); ++i) {
        if (used[i]) continue;
        const auto& e = job.tracks[i];
        if (e.source == 0) {
            const auto& st = job.sourceTrack(e);
            warnings.push_back(L"traccia in più " + std::wstring(1, kindLetter(st.kind)) + std::to_wstring(st.ordinal) + L" conservata");
            result.push_back(defaultEdit(job, 0, e.track));
        } else externals.push_back(e);
    }
    result.insert(result.end(), externals.begin(), externals.end());
    job.tracks = std::move(result);
    job.customized = true;
    return join(warnings, L"; ");
}

// ---------------------------------------------------------------------------
// Validazione e argomenti MKVToolNix
// ---------------------------------------------------------------------------
void validate(const Job& job, const Options& o) {
    if (!job.analyzed()) fail(L"File non analizzato: " + job.input().filename().wstring());
    bool any = false;
    for (const auto& e : job.tracks) {
        if (!e.keep) continue;
        any = true;
        const auto& st = job.sourceTrack(e);
        auto where = kindName(st.kind) + L" " + std::to_wstring(st.ordinal);
        if (!validLanguage(e.language)) fail(where + L": codice lingua non valido \"" + e.language + L"\"");
        for (wchar_t c : e.name)
            if (c < 32) fail(where + L": il nome traccia contiene caratteri di controllo");
        if (e.delayMs < -36000000 || e.delayMs > 36000000) fail(where + L": ritardo fuori intervallo");
    }
    if (!any) fail(L"Nessuna traccia selezionata per " + job.input().filename().wstring());
    (void)o;
}

bool needsRemux(const Job& job, const Options& o) {
    return !o.fastCopy || o.noChapters || o.noAttachments || o.noGlobalTags || structural(job);
}

std::vector<std::wstring> mergeArgs(const Job& job, const fs::path& out, const Options& o, size_t index) {
    std::vector<std::wstring> a{L"--output-charset", L"UTF-8", L"--gui-mode", L"-o", out.wstring()};
    if (o.setTitle) a.insert(a.end(), {L"--title", expand(o.title, outputStem(job, o), index, L"")});
    std::vector<int> fileIndex(job.sources.size(), -1);
    int nextFile = 0;
    for (size_t s = 0; s < job.sources.size(); ++s) {
        const auto& src = job.sources[s];
        std::vector<const TrackEdit*> kept;
        for (const auto& e : job.tracks)
            if (e.source == s && e.keep) kept.push_back(&e);
        if (s > 0 && kept.empty()) continue;  // traccia esterna esclusa del tutto
        fileIndex[s] = nextFile++;
        static const struct { Kind kind; const wchar_t* none; const wchar_t* some; } selectors[] = {
            {Kind::Video, L"-D", L"--video-tracks"},
            {Kind::Audio, L"-A", L"--audio-tracks"},
            {Kind::Subtitles, L"-S", L"--subtitle-tracks"},
            {Kind::Other, L"-B", L"--button-tracks"},
        };
        for (const auto& sel : selectors) {
            size_t total = 0;
            std::vector<std::wstring> ids;
            for (const auto& st : src.tracks) if (st.kind == sel.kind) ++total;
            for (auto* e : kept)
                if (src.tracks[e->track].kind == sel.kind) ids.push_back(std::to_wstring(src.tracks[e->track].id));
            if (!total || ids.size() == total) continue;
            if (ids.empty()) a.push_back(sel.none);
            else a.insert(a.end(), {sel.some, join(ids, L",")});
        }
        for (auto* e : kept) {
            auto id = std::to_wstring(src.tracks[e->track].id) + L":";
            a.insert(a.end(), {L"--track-name", id + e->name});
            if (!e->language.empty()) a.insert(a.end(), {L"--language", id + e->language});
            a.insert(a.end(), {L"--default-track-flag", id + (e->isDefault ? L"1" : L"0")});
            a.insert(a.end(), {L"--forced-display-flag", id + (e->forced ? L"1" : L"0")});
            if (e->delayMs) a.insert(a.end(), {L"--sync", id + std::to_wstring(e->delayMs)});
        }
        if (s == 0) {
            if (o.noChapters) a.push_back(L"--no-chapters");
            if (o.noAttachments) a.push_back(L"--no-attachments");
            if (o.noGlobalTags) a.push_back(L"--no-global-tags");
        }
        a.push_back(src.path.wstring());
    }
    std::vector<std::wstring> order;
    for (const auto& e : job.tracks)
        if (e.keep && fileIndex[e.source] >= 0)
            order.push_back(std::to_wstring(fileIndex[e.source]) + L":" + std::to_wstring(job.sourceTrack(e).id));
    if (!order.empty()) a.insert(a.end(), {L"--track-order", join(order, L",")});
    return a;
}

std::vector<std::wstring> propeditArgs(const Job& job, const fs::path& file, const Options& o, size_t index) {
    std::vector<std::wstring> a{L"--output-charset", L"UTF-8", L"--gui-mode", file.wstring()};
    if (o.setTitle)
        a.insert(a.end(), {L"--edit", L"info", L"--set", L"title=" + expand(o.title, outputStem(job, o), index, L"")});
    for (const auto& e : job.tracks) {
        if (e.source != 0) continue;
        const auto& st = job.sourceTrack(e);
        bool name = e.name != st.name, lang = e.language != st.language && !e.language.empty();
        bool def = e.isDefault != st.isDefault, forced = e.forced != st.forced;
        if (!name && !lang && !def && !forced) continue;
        a.insert(a.end(), {L"--edit", st.number > 0 ? L"track:@" + std::to_wstring(st.number) : L"track:" + std::to_wstring(e.track + 1)});
        if (name) {
            if (e.name.empty()) a.insert(a.end(), {L"--delete", L"name"});
            else a.insert(a.end(), {L"--set", L"name=" + e.name});
        }
        if (lang) a.insert(a.end(), {L"--set", L"language=" + e.language});
        if (def) a.insert(a.end(), {L"--set", std::wstring(L"flag-default=") + (e.isDefault ? L"1" : L"0")});
        if (forced) a.insert(a.end(), {L"--set", std::wstring(L"flag-forced=") + (e.forced ? L"1" : L"0")});
    }
    return a;
}

// ---------------------------------------------------------------------------
// CRC32
// ---------------------------------------------------------------------------
uint32_t crcUpdate(uint32_t crc, const unsigned char* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int j = 0; j < 8; ++j) v = (v >> 1) ^ ((v & 1) ? 0xEDB88320u : 0u);
            t[i] = v;
        }
        return t;
    }();
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return crc;
}

std::wstring crcFile(const fs::path& path, std::atomic_bool& cancel) {
    Handle f(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (f.h == INVALID_HANDLE_VALUE) throw std::runtime_error("Impossibile aprire file per CRC32");
    std::vector<unsigned char> buffer(1024 * 1024);
    DWORD n;
    uint32_t crc = 0xFFFFFFFFu;
    for (;;) {
        checkCancel(cancel);
        if (!ReadFile(f.h, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr)) throw std::runtime_error("Errore lettura CRC32");
        if (!n) break;
        crc = crcUpdate(crc, buffer.data(), n);
    }
    std::wostringstream s;
    s << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0') << (crc ^ 0xFFFFFFFFu);
    return s.str();
}

// ---------------------------------------------------------------------------
// Processi esterni
// ---------------------------------------------------------------------------
namespace {
DWORD runRaw(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel,
             const std::function<void(const char*, size_t)>& sink) {
    checkCancel(cancel);
    std::wstring cmd = quote(exe.wstring());
    for (const auto& arg : args) cmd += L" " + quote(arg);
    if (cmd.size() > 32000) throw std::runtime_error("Comando troppo lungo: ridurre il numero di tracce o modifiche");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) throw std::runtime_error("CreatePipe fallita");
    Handle read(rd), write(wr);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    Handle nullIn(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    // Ereditarietà limitata agli handle standard, anche se il processo ne ha altri.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) throw std::runtime_error("Attributi processo non disponibili");
    struct AttrGuard { LPPROC_THREAD_ATTRIBUTE_LIST p; ~AttrGuard() { DeleteProcThreadAttributeList(p); } } guard{list};
    HANDLE inherited[] = {wr, nullIn.h};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("Handle processo non disponibili");
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = wr;
    si.StartupInfo.hStdInput = nullIn.h;
    si.lpAttributeList = list;
    // Il job termina anche eventuali processi figli all'annullamento o alla chiusura.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.h || !SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Job Windows non disponibile");
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi))
        throw std::runtime_error("Avvio MKVToolNix fallito (errore Windows " + std::to_string(GetLastError()) +
                                 "): controllare percorso e dipendenze");
    Handle process(pi.hProcess), thread(pi.hThread);
    if (!AssignProcessToJobObject(job.h, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 2);
        WaitForSingleObject(pi.hProcess, INFINITE);
        throw std::runtime_error("Impossibile associare processo al job");
    }
    ResumeThread(pi.hThread);
    CloseHandle(write.h);
    write.h = INVALID_HANDLE_VALUE;
    for (;;) {
        if (cancel) {
            TerminateJobObject(job.h, 2);
            WaitForSingleObject(pi.hProcess, INFINITE);
            throw std::runtime_error("Operazione annullata");
        }
        DWORD available = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buf[8192];
            DWORD n = 0;
            if (ReadFile(rd, buf, (std::min)(available, DWORD(sizeof(buf))), &n, nullptr) && n) sink(buf, n);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 40) == WAIT_OBJECT_0) {
            // Un'ultima scrittura può arrivare tra il controllo e la fine del processo.
            DWORD remaining = 0;
            if (PeekNamedPipe(rd, nullptr, 0, nullptr, &remaining, nullptr) && remaining) continue;
            break;
        }
    }
    DWORD code = 2;
    GetExitCodeProcess(pi.hProcess, &code);
    return code;
}
}  // namespace

DWORD run(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel, const Log& log) {
    std::string pending;
    DWORD code = runRaw(exe, args, cancel, [&](const char* data, size_t n) {
        pending.append(data, n);
        size_t p;
        while ((p = pending.find_first_of("\r\n")) != pending.npos) {
            if (p) log(widen(pending.substr(0, p)));
            pending.erase(0, p + 1);
        }
        if (pending.size() > 16384) { log(widen(pending)); pending.clear(); }
    });
    if (!pending.empty()) log(widen(pending));
    return code;
}

DWORD capture(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel, std::string& out) {
    out.clear();
    return runRaw(exe, args, cancel, [&](const char* data, size_t n) {
        if (out.size() + n > 64u * 1024 * 1024) throw std::runtime_error("Output mkvmerge troppo grande");
        out.append(data, n);
    });
}

Source analyze(const fs::path& tools, const fs::path& file, std::atomic_bool& cancel) {
    std::string out;
    DWORD rc = capture(tools / L"mkvmerge.exe", {L"--output-charset", L"UTF-8", L"-J", file.wstring()}, cancel, out);
    auto start = out.find('{');
    if (start == out.npos) fail(L"Analisi fallita (codice " + std::to_wstring(rc) + L"): " + file.filename().wstring());
    return parseIdentify(out.substr(start), file);
}

fs::path process(const Job& job, size_t index, const Options& o, std::atomic_bool& cancel, const Log& log, const Progress& progress) {
    checkCancel(cancel);
    validate(job, o);
    for (const auto& s : job.sources) {
        std::error_code ec;
        if (!fs::is_regular_file(s.path, ec)) fail(L"File non trovato: " + s.path.wstring());
    }
    fs::create_directories(o.output);
    wchar_t temp[MAX_PATH];
    if (!GetTempFileNameW(o.output.c_str(), L"mkf", 0, temp))
        throw std::runtime_error("Impossibile creare temporaneo nella cartella output (percorso troppo lungo o permessi)");
    fs::path stage(temp);
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove(p, e); } } cleanup{stage};
    bool warning = false;
    auto toolLog = [&](const std::wstring& line) {
        if (line.rfind(L"#GUI#progress ", 0) == 0) {
            if (progress) progress(std::clamp(_wtoi(line.c_str() + 14), 0, 100));
            return;
        }
        if (line.rfind(L"#GUI#warning ", 0) == 0) { log(L"Avviso: " + line.substr(13)); return; }
        if (line.rfind(L"#GUI#error ", 0) == 0) { log(L"Errore: " + line.substr(11)); return; }
        if (line.rfind(L"#GUI#", 0) == 0) return;
        log(line);
    };
    auto execute = [&](const wchar_t* tool, const std::vector<std::wstring>& args) {
        DWORD rc = run(o.tools / tool, args, cancel, toolLog);
        if (rc > 1) throw std::runtime_error("MKVToolNix ha segnalato un errore (vedere il log)");
        if (rc == 1) warning = true;
    };
    if (needsRemux(job, o)) {
        size_t kept = std::count_if(job.tracks.begin(), job.tracks.end(), [](const TrackEdit& e) { return e.keep; });
        log(L"Remux con mkvmerge: " + std::to_wstring(kept) + L" flussi da " + std::to_wstring(job.sources.size()) + L" file...");
        // Il temporaneo è riservato a questo processo: mkvmerge sostituisce solo quel file.
        execute(L"mkvmerge.exe", mergeArgs(job, stage, o, index));
    } else {
        log(L"Struttura invariata: copia veloce...");
        auto callback = [](LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID data) -> DWORD {
            auto* ctx = static_cast<std::pair<std::atomic_bool*, const Progress*>*>(data);
            if (*ctx->second && total.QuadPart > 0) (*ctx->second)(static_cast<int>(done.QuadPart * 90 / total.QuadPart));
            return ctx->first->load() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
        };
        std::pair<std::atomic_bool*, const Progress*> ctx{&cancel, &progress};
        if (!CopyFileExW(job.input().c_str(), stage.c_str(), callback, &ctx, nullptr, 0)) {
            checkCancel(cancel);
            throw std::runtime_error("Copia fallita");
        }
        checkCancel(cancel);
        auto args = propeditArgs(job, stage, o, index);
        if (args.size() > 4) {
            log(L"Modifica intestazioni...");
            execute(L"mkvpropedit.exe", args);
        }
    }
    checkCancel(cancel);
    if (progress) progress(100);
    log(L"Calcolo CRC32 sul file finale...");
    auto crc = crcFile(stage, cancel);
    auto target = o.output / fileName(expand(o.pattern, outputStem(job, o), index, crc));
    checkCancel(cancel);
    // Rinomina nella stessa cartella SENZA sostituzione: collisioni e originali restano protetti.
    if (!MoveFileExW(stage.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Rinomina fallita: nome esistente, percorso troppo lungo o permessi");
    log(L"CRC32: " + crc + (warning ? L" — COMPLETATO CON AVVISI: verificare il log" : L" — completato"));
    return target;
}
// ---------------------------------------------------------------------------
// Rinomina sul posto
// ---------------------------------------------------------------------------
std::wstring replaceText(const std::wstring& text, const std::wstring& find, const std::wstring& with, bool ignoreCase) {
    if (find.empty()) return text;
    auto hay = ignoreCase ? lower(text) : text;
    auto needle = ignoreCase ? lower(find) : find;
    std::wstring out;
    size_t p = 0, q;
    while ((q = hay.find(needle, p)) != hay.npos) {
        out.append(text, p, q - p);
        out += with;
        p = q + needle.size();
    }
    return out + text.substr(p);
}

namespace {
std::wstring nameProblem(const std::wstring& name) {
    if (name.empty() || name.size() > 240 || name.back() == L'.' || name.back() == L' ') return L"nome vuoto, troppo lungo o con punto/spazio finale";
    for (wchar_t c : name)
        if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos) return L"carattere non valido nel nome";
    auto base = name.substr(0, name.find(L'.'));
    std::transform(base.begin(), base.end(), base.begin(), towupper);
    if (std::regex_match(base, std::wregex(L"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]"))) return L"nome riservato da Windows";
    return {};
}
}  // namespace

std::vector<fs::path> listMkv(const fs::path& folder) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && lower(it->path().extension().wstring()) == L".mkv") found.push_back(it->path());
    }
    if (ec) fail(L"Impossibile leggere la cartella: " + folder.wstring());
    std::sort(found.begin(), found.end(), [](const fs::path& a, const fs::path& b) { return _wcsicmp(a.wstring().c_str(), b.wstring().c_str()) < 0; });
    return found;
}

std::vector<RenameItem> planRename(const std::vector<fs::path>& mkvs, const RenameRules& rules) {
    std::vector<RenameItem> plan;
    if (rules.find.empty()) return plan;
    // Un file affiancato appartiene all'MKV con il nome più lungo che lo precede:
    // "Ep 1 parte 2.srt" va con "Ep 1 parte 2.mkv", non con "Ep 1.mkv".
    std::vector<std::pair<fs::path, size_t>> owners;
    if (rules.companions)
        for (size_t i = 0; i < mkvs.size(); ++i)
            for (const auto& c : findCompanions(mkvs[i])) {
                auto it = std::find_if(owners.begin(), owners.end(), [&](const auto& o) { return sameFile(o.first, c); });
                if (it == owners.end()) owners.push_back({c, i});
                else if (mkvs[i].stem().wstring().size() > mkvs[it->second].stem().wstring().size()) it->second = i;
            }
    for (size_t i = 0; i < mkvs.size(); ++i) {
        auto stem = mkvs[i].stem().wstring();
        auto newStem = replaceText(stem, rules.find, rules.with, rules.ignoreCase);
        if (newStem == stem) continue;
        RenameItem item{mkvs[i], mkvs[i].parent_path() / (newStem + mkvs[i].extension().wstring()), false, i, {}};
        item.error = nameProblem(item.to.filename().wstring());
        plan.push_back(item);
        for (const auto& [file, owner] : owners) {
            if (owner != i) continue;
            auto rest = file.filename().wstring().substr(stem.size());
            RenameItem c{file, file.parent_path() / (newStem + rest), true, i, {}};
            c.error = nameProblem(c.to.filename().wstring());
            plan.push_back(c);
        }
    }
    std::set<std::wstring> targets;
    for (auto& item : plan) {
        if (!targets.insert(lower(item.to.wstring())).second && item.error.empty()) item.error = L"un altro file avrebbe lo stesso nome";
        std::error_code ec;
        if (item.error.empty() && !sameFile(item.from, item.to) && fs::exists(item.to, ec)) item.error = L"esiste già un file con questo nome";
    }
    // Se l'MKV non si può rinominare, i suoi file affiancati restano com'erano.
    for (const auto& item : plan)
        if (!item.companion && !item.error.empty())
            for (auto& c : plan)
                if (c.companion && c.owner == item.owner && c.error.empty()) c.error = L"il video non viene rinominato";
    return plan;
}

std::vector<std::pair<fs::path, fs::path>> applyRename(std::vector<RenameItem>& plan) {
    std::vector<std::pair<fs::path, fs::path>> done;
    for (auto& item : plan) {
        if (!item.error.empty()) continue;
        // Nessun MOVEFILE_REPLACE_EXISTING: un file esistente non viene mai sovrascritto.
        if (MoveFileExW(item.from.c_str(), item.to.c_str(), 0)) done.push_back({item.from, item.to});
        else item.error = L"rinomina fallita (errore Windows " + std::to_wstring(GetLastError()) + L")";
    }
    return done;
}

}  // namespace app
