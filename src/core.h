#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace app {
namespace fs = std::filesystem;

using Log = std::function<void(const std::wstring&)>;
using Progress = std::function<void(int percent)>;

// ---------------------------------------------------------------------------
// Flussi (tracce) di un file sorgente, come riportati da `mkvmerge -J`.
// ---------------------------------------------------------------------------
enum class Kind { Video, Audio, Subtitles, Other };

std::wstring kindName(Kind kind);      // "Video", "Audio", "Sottotitoli", "Altro"
wchar_t kindLetter(Kind kind);         // 'v', 'a', 's', 'o'

struct SourceTrack {
    int id = 0;                 // ID mkvmerge (zero-based, per file)
    int number = 0;             // numero traccia Matroska (per mkvpropedit track:@n)
    int ordinal = 0;            // posizione 1-based tra le tracce dello stesso tipo
    Kind kind = Kind::Other;
    std::wstring codec, language, name, info;
    bool isDefault = false, forced = false;
};

struct Source {
    fs::path path;
    bool analyzed = false;
    std::wstring title;                 // titolo contenitore attuale
    std::vector<SourceTrack> tracks;
    std::wstring languageHint;          // dedotta dal nome file (solo esterni)
    bool forcedHint = false;            // "forced" nel nome file (solo esterni)
};

// Una riga dell'editor: un flusso in uscita, nell'ordine finale.
struct TrackEdit {
    size_t source = 0;          // indice in Job::sources (0 = MKV principale)
    size_t track = 0;           // indice in Source::tracks
    bool keep = true;
    std::wstring name, language;
    bool isDefault = false, forced = false;
    int delayMs = 0;
};

struct Job {
    std::vector<Source> sources;        // [0] = file MKV principale, poi tracce esterne
    std::vector<TrackEdit> tracks;      // ordine di uscita
    bool customized = false;            // modificato a mano o da modello

    const fs::path& input() const { return sources.front().path; }
    bool analyzed() const;
    const SourceTrack& sourceTrack(const TrackEdit& e) const { return sources[e.source].tracks[e.track]; }
};

Job makeJob(const fs::path& mkv);
Source parseIdentify(const std::string& json, const fs::path& path);   // JSON UTF-8 di mkvmerge -J
TrackEdit defaultEdit(const Job& job, size_t source, size_t track);
void resetEdits(Job& job);              // tutte le tracce, ordine originale, proprietà originali
void syncEdits(Job& job);               // aggiunge tracce nuove, scarta riferimenti non più validi
size_t addExternal(Job& job, const fs::path& file);   // restituisce l'indice sorgente
void removeSource(Job& job, size_t source);
void setDefault(Job& job, size_t row, bool value, bool exclusive);
void keepLanguages(Job& job, const std::wstring& csv);   // filtra audio/sottotitoli per lingua
bool hasChanges(const Job& job);        // proprietà diverse dall'originale
bool structural(const Job& job);        // rimozioni, riordino, esterni o ritardi
std::wstring summary(const Job& job);   // "1 V · 2 A · 3 S"

// Modello di layout per applicare le stesse scelte a tutto il batch.
// Le tracce del file principale sono riconosciute per tipo e posizione (a1, a2, s1...).
struct Template {
    struct Entry {
        Kind kind = Kind::Other;
        int ordinal = 0;
        bool keep = true;
        int delayMs = 0;
        std::optional<std::wstring> name, language;     // vuoto = conserva quella del file
        std::optional<bool> isDefault, forced;
    };
    std::vector<Entry> entries;
};
Template makeTemplate(const Job& job);
std::wstring applyTemplate(Job& job, const Template& t);   // testo avvisi, vuoto se tutto combacia

// Tracce esterne con lo stesso nome del video (Episodio 01.ita.ac3, Episodio 01.srt...).
bool isExternalTrackFile(const fs::path& file);
std::vector<fs::path> findCompanions(const fs::path& mkv);
std::wstring guessLanguage(const std::wstring& text);     // "ita", "eng"... oppure vuoto
bool validLanguage(const std::wstring& code);

// ---------------------------------------------------------------------------
// Opzioni batch, nomi e pipeline.
// ---------------------------------------------------------------------------
struct Options {
    fs::path tools, output;
    std::wstring pattern = L"{name} [{crc}]", title;
    bool setTitle = false;
    bool fastCopy = true;               // solo intestazioni quando la struttura non cambia
    bool noChapters = false, noAttachments = false, noGlobalTags = false;
    std::wstring replaceFind, replaceWith;   // trova/sostituisci applicato a {name}
    bool replaceIgnoreCase = true;
    bool enableCrc = true;
};

std::wstring quote(const std::wstring& s);
std::wstring expand(std::wstring text, const std::wstring& name, size_t index, const std::wstring& crc);
std::wstring namingPattern(const std::wstring& pattern, bool enableCrc);
std::wstring fileName(const std::wstring& text);
std::wstring outputStem(const Job& job, const Options& o);   // {name} dopo trova/sostituisci
void validate(const Job& job);
bool needsRemux(const Job& job, const Options& o);
std::vector<std::wstring> mergeArgs(const Job& job, const fs::path& out, const Options& o, size_t index);
std::vector<std::wstring> propeditArgs(const Job& job, const fs::path& file, const Options& o, size_t index);

uint32_t crcUpdate(uint32_t crc, const unsigned char* data, size_t size);
std::wstring crcFile(const fs::path& path, std::atomic_bool& cancel);

// Esegue un programma senza shell. Le righe di output passano a `log`.
DWORD run(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel, const Log& log);
// Come run, ma restituisce l'output grezzo (UTF-8).
DWORD capture(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel, std::string& out);

Source analyze(const fs::path& tools, const fs::path& file, std::atomic_bool& cancel);
fs::path process(const Job& job, size_t index, const Options& o, std::atomic_bool& cancel,
                 const Log& log, const Progress& progress = {});

// ---------------------------------------------------------------------------
// Rinomina sul posto: trova/sostituisci nei nomi dei file di una cartella.
// ---------------------------------------------------------------------------
std::wstring replaceText(const std::wstring& text, const std::wstring& find, const std::wstring& with, bool ignoreCase);

std::wstring stripCrc(const std::wstring& stem);      // "Ep 01 [A1B2C3D4]" → "Ep 01"
std::wstring existingCrc(const std::wstring& stem);   // "A1B2C3D4" oppure vuoto

struct RenameItem {
    fs::path from, to;
    bool companion = false;     // sottotitolo/audio con lo stesso nome dell'MKV
    size_t owner = 0;           // indice dell'MKV a cui appartiene
    std::wstring error;         // vuoto = rinominabile
    bool pendingCrc = false;    // anteprima: il CRC32 sarà calcolato al momento
};
struct RenameRules {
    std::wstring find, with;
    std::wstring pattern = L"{name} [{crc}]";   // {name} = nome senza vecchio CRC, dopo trova/sostituisci
    bool ignoreCase = true;
    bool companions = true;
    bool enableCrc = true;
    bool wantsCrc() const { return enableCrc && pattern.find(L"{crc}") != std::wstring::npos; }
};
std::vector<fs::path> listMkv(const fs::path& folder);
// {crc}: valore in `crcs` (uno per MKV) se non vuoto, altrimenti il CRC già nel nome (rinominare non
// cambia il contenuto); se manca anche quello l'elemento resta in attesa (pendingCrc, mostrato "CRC32").
std::vector<RenameItem> planRename(const std::vector<fs::path>& mkvs, const RenameRules& rules,
                                   const std::vector<std::wstring>* crcs = nullptr);
// Rinomina senza mai sostituire file esistenti. Restituisce le coppie eseguite (per annullare).
std::vector<std::pair<fs::path, fs::path>> applyRename(std::vector<RenameItem>& plan);

std::wstring widen(const std::string& utf8);
}
