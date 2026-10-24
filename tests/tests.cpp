// Test senza framework. `build\tests.exe` esegue i test del core;
// `build\tests.exe <cartella MKVToolNix> <cartella output>` aggiunge l'integrazione reale.
#include "../src/core.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace app;

namespace {
int checks = 0;
void require(bool b, const char* m) {
    ++checks;
    if (!b) throw std::runtime_error(m);
}
template <class F> void rejects(F f, const char* m = "expected rejection") {
    bool failed = false;
    try { f(); } catch (...) { failed = true; }
    require(failed, m);
}
bool has(const std::vector<std::wstring>& args, const std::wstring& value) {
    for (const auto& a : args) if (a == value) return true;
    return false;
}
// Valore che segue `key` negli argomenti (es. "--track-order").
std::wstring after(const std::vector<std::wstring>& args, const std::wstring& key) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == key) return args[i + 1];
    return L"<assente>";
}
size_t count(const std::vector<std::wstring>& args, const std::wstring& value) {
    size_t n = 0;
    for (const auto& a : args) if (a == value) ++n;
    return n;
}

// Struttura tipica di un episodio: 1 video, audio jpn + ita, 2 sottotitoli.
const char* episodeJson(const char* audioName) {
    static std::string s;
    s = R"({
 "container": {"properties": {"title": "Ep \u00e8 \ud83c\udfac"}, "recognized": true, "supported": true, "type": "Matroska"},
 "errors": [], "file_name": "x.mkv",
 "tracks": [
  {"codec": "HEVC/H.265", "id": 0, "type": "video", "properties": {"number": 1, "language": "und", "default_track": true, "forced_track": false, "pixel_dimensions": "1920x1080"}},
  {"codec": "AAC", "id": 1, "type": "audio", "properties": {"number": 2, "language": "jpn", "track_name": ")" + std::string(audioName) + R"(", "default_track": true, "forced_track": false, "audio_channels": 2, "audio_sampling_frequency": 48000}},
  {"codec": "E-AC-3", "id": 2, "type": "audio", "properties": {"number": 3, "language": "ita", "default_track": false, "forced_track": false, "audio_channels": 6, "audio_sampling_frequency": 44100}},
  {"codec": "SubStationAlpha", "id": 3, "type": "subtitles", "properties": {"number": 4, "language": "ita", "track_name": "Completi", "default_track": false}},
  {"codec": "SubStationAlpha", "id": 4, "type": "subtitles", "properties": {"number": 5, "language": "ita", "track_name": "Cartelli", "forced_track": true, "default_track": false}}
 ]
})";
    return s.c_str();
}
const char* srtJson = R"({"container":{"recognized":true,"supported":true,"properties":{}},"tracks":[{"codec":"SubRip/SRT","id":0,"type":"subtitles","properties":{"language":"und","default_track":true}}]})";

Job episode(const wchar_t* path, const char* audioName = "Originale") {
    Job job = makeJob(path);
    job.sources[0] = parseIdentify(episodeJson(audioName), path);
    resetEdits(job);
    return job;
}

void unitTests() {
    auto bytes = reinterpret_cast<const unsigned char*>("123456789");
    require((crcUpdate(0xFFFFFFFFu, bytes, 9) ^ 0xFFFFFFFFu) == 0xCBF43926u, "CRC vector");
    uint32_t crc = crcUpdate(0xFFFFFFFFu, bytes, 4);
    crc = crcUpdate(crc, bytes + 4, 5);
    require((crc ^ 0xFFFFFFFFu) == 0xCBF43926u, "CRC streaming");
    require(quote(L"a b\\") == L"\"a b\\\\\"", "quote trailing slash");
    require(quote(L"a\"b") == L"\"a\\\"b\"", "quote embedded quote");
    require(expand(L"{name}-{n}-{crc}", L"{crc}", 2, L"ABC") == L"{crc}-2-ABC", "token injection");
    require(fileName(L"Film [ABCDEF12]") == L"Film [ABCDEF12].mkv", "name");
    rejects([] { fileName(L"CON"); });
    rejects([] { fileName(L"../film"); });
    rejects([] { fileName(L"film."); });

    // --- Analisi JSON
    Job job = episode(L"C:\\Serie\\Ep 01.mkv");
    const auto& src = job.sources[0];
    require(src.title == L"Ep \u00e8 \U0001F3AC", "JSON unicode escapes and surrogates");
    require(src.tracks.size() == 5, "track count");
    require(src.tracks[1].kind == Kind::Audio && src.tracks[1].ordinal == 1 && src.tracks[2].ordinal == 2, "audio ordinals");
    require(src.tracks[4].kind == Kind::Subtitles && src.tracks[4].ordinal == 2 && src.tracks[4].forced, "subtitle flags");
    require(src.tracks[0].info == L"1920x1080" && src.tracks[2].info == L"6 ch, 44100 Hz" && src.tracks[1].info == L"2 ch, 48 kHz", "track info");
    require(!src.tracks[3].isDefault && src.tracks[3].name == L"Completi", "track name");
    rejects([] { parseIdentify(R"({"container":{"recognized":false},"errors":["boh"]})", L"x.txt"); }, "unrecognized file");
    rejects([] { parseIdentify(R"({"container": )", L"x"); }, "truncated JSON");
    rejects([] { parseIdentify(R"({"a":1} x)", L"x"); }, "trailing JSON");

    // --- Nessuna modifica: copia veloce, nessun argomento traccia
    Options o;
    o.tools = L"C:\\MKVToolNix";
    o.output = L"C:\\Out";
    require(job.tracks.size() == 5 && !hasChanges(job) && !structural(job), "default layout");
    require(!needsRemux(job, o), "fast copy when unchanged");
    require(propeditArgs(job, L"t.mkv", o, 1).size() == 4, "no propedit edits");
    require(summary(job) == L"1 V · 2 A · 2 S", "summary");

    // --- Solo proprietà: resta copia veloce con mkvpropedit
    job.tracks[2].name = L"Italiano";
    setDefault(job, 2, true, true);
    require(!job.tracks[1].isDefault && job.tracks[2].isDefault && job.tracks[0].isDefault, "exclusive default per type");
    require(hasChanges(job) && !structural(job) && !needsRemux(job, o), "header-only edit");
    auto pe = propeditArgs(job, L"t.mkv", o, 1);
    require(has(pe, L"track:@3") && has(pe, L"name=Italiano") && has(pe, L"flag-default=1"), "propedit a2");
    require(has(pe, L"track:@2") && has(pe, L"flag-default=0"), "propedit a1 default cleared");
    require(!has(pe, L"track:@1") && !has(pe, L"track:@4"), "propedit untouched tracks");
    job.tracks[3].name.clear();
    require(has(propeditArgs(job, L"t.mkv", o, 1), L"--delete"), "propedit delete name");
    o.setTitle = true;
    o.title = L"{name} - {n}";
    require(after(propeditArgs(job, L"t.mkv", o, 7), L"--set") == L"title=" + job.input().stem().wstring() + L" - 7", "propedit title");
    o.fastCopy = false;
    require(needsRemux(job, o), "forced remux");
    o.fastCopy = true;
    o.setTitle = false;

    // --- Struttura: riordino e rimozione
    std::swap(job.tracks[1], job.tracks[2]);
    job.tracks[4].keep = false;  // via i cartelli
    require(structural(job) && needsRemux(job, o), "reorder is structural");
    auto ma = mergeArgs(job, L"C:\\Out\\tmp.mkv", o, 1);
    require(after(ma, L"--track-order") == L"0:0,0:2,0:1,0:3", "track order");
    require(after(ma, L"--subtitle-tracks") == L"3" && !has(ma, L"--audio-tracks") && !has(ma, L"-S"), "subtitle selection");
    require(has(ma, L"2:Italiano") && has(ma, L"3:") && !has(ma, L"4:Cartelli"), "track names");
    require(has(ma, L"--gui-mode") && after(ma, L"-o") == L"C:\\Out\\tmp.mkv", "output");
    require(count(ma, L"--default-track-flag") == 4 && has(ma, L"2:1") && has(ma, L"1:0"), "default flags");
    job.tracks[3].keep = false;
    ma = mergeArgs(job, L"o.mkv", o, 1);
    require(has(ma, L"-S") && !has(ma, L"--subtitle-tracks"), "no subtitles");
    job.tracks[3].keep = true;
    job.tracks[1].delayMs = -120;
    require(after(mergeArgs(job, L"o.mkv", o, 1), L"--sync") == L"2:-120", "delay");
    o.noChapters = true;
    ma = mergeArgs(job, L"o.mkv", o, 1);
    require(has(ma, L"--no-chapters") && !has(ma, L"--no-attachments"), "container options");
    o.noChapters = false;

    // --- Tracce esterne
    auto ext = addExternal(job, L"C:\\Serie\\Ep 01.ita.forced.srt");
    require(ext == 1 && job.sources[1].languageHint == L"ita" && job.sources[1].forcedHint, "external hints");
    require(!job.analyzed(), "external pending analysis");
    rejects([&] { validate(job, o); }, "validate requires analysis");
    job.sources[1] = [&] { auto s = parseIdentify(srtJson, job.sources[1].path); s.languageHint = L"ita"; s.forcedHint = true; return s; }();
    syncEdits(job);
    require(job.tracks.size() == 6 && job.tracks[5].source == 1, "external appended");
    require(job.tracks[5].language == L"ita" && job.tracks[5].forced && !job.tracks[5].isDefault, "external defaults");
    ma = mergeArgs(job, L"o.mkv", o, 1);
    require(ma.back() == L"0:0,0:2,0:1,0:3,1:0" && ma[ma.size() - 3] == L"C:\\Serie\\Ep 01.ita.forced.srt", "external in order");
    std::rotate(job.tracks.begin() + 3, job.tracks.begin() + 5, job.tracks.end());  // srt prima dei sottotitoli interni
    require(after(mergeArgs(job, L"o.mkv", o, 1), L"--track-order") == L"0:0,0:2,0:1,1:0,0:3", "external moved");
    rejects([&] { addExternal(job, L"c:\\serie\\EP 01.mkv"); }, "main as external");
    require(addExternal(job, L"C:\\Serie\\Ep 01.ita.forced.srt") == 1, "duplicate external");
    job.tracks[3].keep = false;
    ma = mergeArgs(job, L"o.mkv", o, 1);
    require(!has(ma, L"C:\\Serie\\Ep 01.ita.forced.srt") && after(ma, L"--track-order").find(L"1:") == std::wstring::npos, "excluded external file");
    removeSource(job, 1);
    require(job.sources.size() == 1 && job.tracks.size() == 5, "remove external");
    validate(job, o);

    // --- Filtro lingue
    Job filtered = episode(L"C:\\Serie\\Ep 02.mkv");
    keepLanguages(filtered, L"ita");
    require(filtered.tracks[0].keep && !filtered.tracks[1].keep && filtered.tracks[2].keep && filtered.tracks[3].keep, "keep languages");
    rejects([&] { keepLanguages(filtered, L" , "); });

    // --- Modello batch: solo le modifiche passano agli altri file
    Job first = episode(L"C:\\Serie\\Ep 01.mkv");
    first.tracks[2].name = L"Italiano";
    setDefault(first, 2, true, true);
    std::swap(first.tracks[1], first.tracks[2]);
    first.tracks[4].keep = false;
    auto model = makeTemplate(first);
    Job other = episode(L"C:\\Serie\\Ep 02.mkv", "Giapponese");
    require(applyTemplate(other, model).empty(), "template matches");
    require(other.sourceTrack(other.tracks[1]).id == 2 && other.tracks[1].name == L"Italiano" && other.tracks[1].isDefault, "template a2 first");
    require(other.tracks[2].name == L"Giapponese" && !other.tracks[2].isDefault, "template keeps unchanged names");
    require(!other.tracks[4].keep && other.tracks[3].keep, "template removal");
    require(mergeArgs(other, L"o.mkv", o, 2) != mergeArgs(first, L"o.mkv", o, 1), "own paths");
    Job shorter = makeJob(L"C:\\Serie\\Ep 03.mkv");
    shorter.sources[0] = parseIdentify(R"({"container":{"recognized":true,"supported":true},"tracks":[
        {"id":0,"type":"video","properties":{"number":1}},{"id":1,"type":"audio","properties":{"number":2,"language":"jpn"}},
        {"id":2,"type":"audio","properties":{"number":3,"language":"ita"}},{"id":3,"type":"audio","properties":{"number":4}}]})",
        L"C:\\Serie\\Ep 03.mkv");
    resetEdits(shorter);
    auto warnings = applyTemplate(shorter, model);
    require(warnings.find(L"manca s1") != std::wstring::npos && warnings.find(L"a3") != std::wstring::npos, "template warnings");
    require(shorter.tracks.size() == 4 && shorter.tracks.back().keep, "extra track kept");

    // --- Lingue e file affiancati
    require(guessLanguage(L".ITA.forced") == L"ita" && guessLanguage(L"_eng") == L"eng" && guessLanguage(L" [Japanese]") == L"jpn", "guess");
    require(guessLanguage(L"commento regista").empty(), "guess none");
    require(validLanguage(L"ita") && validLanguage(L"pt-BR") && validLanguage(L"") && !validLanguage(L"italiano") && !validLanguage(L"x y"), "language codes");
    auto dir = fs::temp_directory_path() / (L"mkvflow-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(dir);
    for (auto name : {L"Ep 1.mkv", L"Ep 1.ita.ac3", L"Ep 1 [eng].srt", L"Ep 10.srt", L"Ep 1.txt", L"Ep 1 v2.mkv", L"Altro.srt"})
        std::ofstream(dir / name) << "x";
    auto found = findCompanions(dir / L"Ep 1.mkv");
    require(found.size() == 2 && found[0].filename() == L"Ep 1 [eng].srt" && found[1].filename() == L"Ep 1.ita.ac3", "companions");
    std::error_code ec;
    fs::remove_all(dir, ec);
    // --- Sostituzione applicata ai nomi del batch
    Options ro;
    ro.replaceFind = L"island";
    ro.replaceWith = L"MIRCrew";
    ro.setTitle = true;
    ro.title = L"{name}";
    Job rj = episode(L"S01E01-IsLAND.mkv");
    require(outputStem(rj, ro) == L"S01E01-MIRCrew", "output stem replace");
    require(after(propeditArgs(rj, L"t.mkv", ro, 1), L"--set") == L"title=S01E01-MIRCrew", "title uses replaced name");
    require(after(mergeArgs(rj, L"t.mkv", ro, 1), L"--title") == L"S01E01-MIRCrew", "merge title uses replaced name");

    // --- Rinomina sul posto
    require(replaceText(L"S01E01-IsLAND", L"island", L"MIRCrew", true) == L"S01E01-MIRCrew", "replace ignore case");
    require(replaceText(L"S01E01-IsLAND", L"island", L"MIRCrew", false) == L"S01E01-IsLAND", "replace case sensitive");
    require(replaceText(L"a.a.a", L"a", L"aa", false) == L"aa.aa.aa", "replace non recursive");
    auto rdir = fs::temp_directory_path() / (L"mkvflow-rename-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(rdir);
    for (auto name : {L"S01E01-IsLAND.mkv", L"S01E01-IsLAND.ita.srt", L"S01E02-IsLAND.mkv", L"S01E02-IsLAND.mkv.bak",
                      L"S01E03-ISLAND.mkv", L"S01E03-MIRCrew.mkv", L"S01E03-ISLAND.eng.srt", L"Extra.mkv"})
        std::ofstream(rdir / name) << "x";
    auto mkvs = listMkv(rdir);
    require(mkvs.size() == 5, "list mkv");
    RenameRules rules;
    rules.find = L"IsLAND";
    rules.with = L"MIRCrew";
    rules.pattern = L"{name}";
    auto plan = planRename(mkvs, rules);
    auto entry = [&](const wchar_t* from) -> const RenameItem* {
        for (const auto& i : plan) if (i.from.filename() == from) return &i;
        return nullptr;
    };
    require(plan.size() == 5, "plan size");
    require(entry(L"S01E01-IsLAND.mkv") && entry(L"S01E01-IsLAND.mkv")->to.filename() == L"S01E01-MIRCrew.mkv" && entry(L"S01E01-IsLAND.mkv")->error.empty(), "plan mkv");
    require(entry(L"S01E01-IsLAND.ita.srt") && entry(L"S01E01-IsLAND.ita.srt")->companion && entry(L"S01E01-IsLAND.ita.srt")->to.filename() == L"S01E01-MIRCrew.ita.srt", "plan companion");
    require(entry(L"S01E03-ISLAND.mkv") && !entry(L"S01E03-ISLAND.mkv")->error.empty(), "collision with existing");
    require(entry(L"S01E03-ISLAND.eng.srt") && !entry(L"S01E03-ISLAND.eng.srt")->error.empty(), "companion follows failed video");
    require(!entry(L"Extra.mkv") && !entry(L"S01E02-IsLAND.mkv.bak"), "untouched files");
    rules.with = L"bad:name";
    require(!planRename(mkvs, rules).front().error.empty(), "invalid characters");
    rules.with = L"MIRCrew";
    rules.companions = false;
    require(planRename(mkvs, rules).size() == 3, "without companions");
    rules.find.clear();
    require(planRename(mkvs, rules).empty(), "empty find");
    fs::remove_all(rdir, ec);

    // --- Struttura "nome [CRC32].mkv"
    require(stripCrc(L"Ep 01 [a1b2c3d4]") == L"Ep 01" && existingCrc(L"Ep 01 [a1b2c3d4]") == L"A1B2C3D4", "strip crc");
    require(stripCrc(L"Ep_01_(A1B2C3D4)") == L"Ep_01" && stripCrc(L"Ep 01 [1080p]") == L"Ep 01 [1080p]", "strip crc variants");
    require(stripCrc(L"[A1B2C3D4]") == L"[A1B2C3D4]" && existingCrc(L"Ep 01").empty(), "strip crc edge");
    require(stripCrc(L"Ep [DEADBEEF] fine") == L"Ep [DEADBEEF] fine", "crc only at the end");
    auto cdir = fs::temp_directory_path() / (L"mkvflow-crcname-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(cdir);
    for (auto name : {L"S01E01-IsLAND [11111111].mkv", L"S01E01-IsLAND.ita.srt", L"S01E02-IsLAND.mkv", L"S01E02-IsLAND [22222222].eng.ass"})
        std::ofstream(cdir / name) << "x";
    auto cm = listMkv(cdir);
    RenameRules cr;
    cr.find = L"island";
    cr.with = L"MIRCrew";
    auto preview = planRename(cm, cr);
    require(preview.size() == 4 && !preview[0].pendingCrc && preview[0].to.filename() == L"S01E01-MIRCrew [11111111].mkv", "preview reuses existing crc");
    require(preview[2].pendingCrc && preview[2].to.filename() == L"S01E02-MIRCrew [CRC32].mkv", "preview placeholder when missing");
    std::vector<std::wstring> crcs{L"AAAA0001", L"BBBB0002"};  // es. dopo una verifica o un titolo cambiato
    auto real = planRename(cm, cr, &crcs);
    auto target = [&](const wchar_t* from) {
        for (const auto& i : real) if (i.from.filename() == from) return i.to.filename().wstring();
        return std::wstring(L"<assente>");
    };
    require(target(L"S01E01-IsLAND [11111111].mkv") == L"S01E01-MIRCrew [AAAA0001].mkv", "old crc replaced");
    require(target(L"S01E01-IsLAND.ita.srt") == L"S01E01-MIRCrew [AAAA0001].ita.srt", "companion without crc follows");
    require(target(L"S01E02-IsLAND.mkv") == L"S01E02-MIRCrew [BBBB0002].mkv", "crc added");
    require(target(L"S01E02-IsLAND [22222222].eng.ass") == L"S01E02-MIRCrew [BBBB0002] [22222222].eng.ass", "companion keeps its suffix");
    cr.find.clear();
    std::vector<std::wstring> same{L"", L"BBBB0002"};  // il primo riusa il CRC del nome
    auto unchanged = planRename(cm, cr, &same);
    require(unchanged.size() == 2 && unchanged[0].from.filename() == L"S01E02-IsLAND.mkv", "already correct name skipped");
    Options so;
    require(outputStem(makeJob(L"Ep 05 [ABCDEF12].mkv"), so) == L"Ep 05", "batch drops old crc");
    fs::remove_all(cdir, ec);
}

void processTests(const wchar_t* self) {
    std::atomic_bool cancel = false;
    std::wstring captured;
    require(run(self, {L"--child", L"echo", L"spazi è \"quote\" e slash\\"}, cancel, [&](const auto& s) { captured += s; }) == 1, "process exit code");
    require(captured == L"UTF-8: èlast line", "process UTF8 and final unterminated output");
    std::string raw;
    require(capture(self, {L"--child", L"echo", L"spazi è \"quote\" e slash\\"}, cancel, raw) == 1 && raw == "UTF-8: \xC3\xA8\nlast line", "raw capture");
    std::thread stop([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); cancel = true; });
    bool aborted = false;
    try { run(self, {L"--child", L"sleep", L"x"}, cancel, [](auto) {}); } catch (...) { aborted = true; }
    stop.join();
    require(aborted, "active process cancellation");
    // Rinomina reale: nessuna sovrascrittura, annullabile.
    auto rdir = fs::temp_directory_path() / (L"mkvflow-rename-run-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(rdir);
    for (auto name : {L"S01E01-IsLAND.mkv", L"S01E01-IsLAND.srt", L"S01E01-IsLAND.flac", L"S01E02-island.mkv"}) std::ofstream(rdir / name) << name[5];
    RenameRules rules;
    rules.find = L"island";
    rules.with = L"MIRCrew";
    rules.pattern = L"{name}";
    rules.companions = false;  // Same policy as the Rename page: only MKV files.
    auto plan = planRename(listMkv(rdir), rules);
    auto done = applyRename(plan);
    if (done.size() != 2) for (const auto& item : plan)
        std::wcerr << L"Rename test: " << item.from << L" -> " << item.to << L": " << item.error << L'\n';
    require(done.size() == 2 && fs::exists(rdir / L"S01E01-MIRCrew.mkv") && fs::exists(rdir / L"S01E02-MIRCrew.mkv"), "rename applied");
    require(fs::exists(rdir / L"S01E01-IsLAND.srt") && fs::exists(rdir / L"S01E01-IsLAND.flac") &&
            !fs::exists(rdir / L"S01E01-MIRCrew.srt") && !fs::exists(rdir / L"S01E01-MIRCrew.flac"), "rename leaves subtitles and audio untouched");
    require(!fs::exists(rdir / L"S01E01-IsLAND.mkv"), "old name gone");
    std::ofstream(rdir / L"S01E01-IsLAND.mkv") << "nuovo";
    std::vector<RenameItem> undo{{rdir / L"S01E01-MIRCrew.mkv", rdir / L"S01E01-IsLAND.mkv", false, 0, {}}};
    require(applyRename(undo).empty() && !undo[0].error.empty(), "rename never replaces");
    std::error_code rec;
    fs::remove_all(rdir, rec);

}

void integrationTests(const fs::path& tools, const fs::path& rootArg) {
    std::atomic_bool cancel = false;
    fs::path root = fs::absolute(rootArg);
    fs::create_directories(root);
    auto log = [](const std::wstring& s) { std::wcout << s << L'\n'; };
    // Piccolo WAV PCM sintetico e un SRT: nessun file multimediale scaricato.
    fs::path wav = root / L"fixture.wav";
    {
        std::ofstream f(wav, std::ios::binary);
        auto u16 = [&](uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); };
        auto u32 = [&](uint32_t v) { u16(static_cast<uint16_t>(v)); u16(static_cast<uint16_t>(v >> 16)); };
        f.write("RIFF", 4); u32(16036); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
        f.write("data", 4); u32(16000);
        for (int i = 0; i < 8000; ++i) u16(0);
    }
    fs::path fixture = root / L"sorgente è [test].mkv";
    require(run(tools / L"mkvmerge.exe", {L"-o", fixture.wstring(), L"--language", L"0:jpn", L"--track-name", L"0:Originale", wav.wstring(),
                                          L"--language", L"0:ita", L"--default-track-flag", L"0:0", wav.wstring()}, cancel, log) <= 1, "fixture mux");
    fs::path srt = root / L"sorgente è [test].ita.srt";
    std::ofstream(srt, std::ios::binary) << "1\r\n00:00:00,000 --> 00:00:00,500\r\nCiao\r\n\r\n";
    auto original = crcFile(fixture, cancel);

    auto identify = [&](const fs::path& file) { return analyze(tools, file, cancel); };
    Job job = makeJob(fixture);
    job.sources[0] = identify(fixture);
    resetEdits(job);
    require(job.tracks.size() == 2 && job.sources[0].tracks[0].language == L"jpn" && job.sources[0].tracks[1].language == L"ita", "fixture analysis");

    // 1) Solo intestazioni: copia veloce + mkvpropedit
    Options o;
    o.tools = tools;
    o.output = root / L"headers";
    o.setTitle = true;
    o.title = L"Titolo è {n}";
    job.tracks[1].name = L"Italiano";
    setDefault(job, 1, true, true);
    require(!needsRemux(job, o), "headers-only path");
    auto a = process(job, 1, o, cancel, log);
    require(a.stem().wstring().find(crcFile(a, cancel)) != std::wstring::npos, "CRC in name (headers)");
    auto ra = identify(a);
    require(ra.title == L"Titolo è 1" && ra.tracks[1].name == L"Italiano" && ra.tracks[1].isDefault && !ra.tracks[0].isDefault, "header edits persisted");
    require(crcFile(fixture, cancel) == original, "original unchanged");

    // 2) Struttura: italiano prima, via il giapponese, SRT esterno con lingua dal nome
    Job remux = makeJob(fixture);
    remux.sources[0] = identify(fixture);
    auto ext = addExternal(remux, srt);
    auto hint = remux.sources[ext];
    remux.sources[ext] = identify(srt);
    remux.sources[ext].languageHint = hint.languageHint;
    resetEdits(remux);
    std::swap(remux.tracks[0], remux.tracks[1]);
    remux.tracks[1].keep = false;
    setDefault(remux, 0, true, true);
    remux.tracks[2].name = L"Italiano SRT";
    remux.tracks[2].delayMs = 250;
    o.output = root / L"remux";
    o.setTitle = false;
    int lastProgress = -1;
    auto b = process(remux, 2, o, cancel, log, [&](int p) { lastProgress = p; });
    require(lastProgress == 100, "progress reported");
    auto rb = identify(b);
    require(rb.tracks.size() == 2 && rb.tracks[0].kind == Kind::Audio && rb.tracks[0].language == L"ita" && rb.tracks[0].isDefault, "remux audio");
    require(rb.tracks[1].kind == Kind::Subtitles && rb.tracks[1].language == L"ita" && rb.tracks[1].name == L"Italiano SRT", "remux external subtitles");
    require(b.stem().wstring().find(crcFile(b, cancel)) != std::wstring::npos, "CRC in name (remux)");

    // 3) Collisione: stesso input deterministico, nessuna sovrascrittura
    o.output = root / L"collision";
    auto first = process(job, 3, o, cancel, log);
    auto before = crcFile(first, cancel);
    rejects([&] { process(job, 3, o, cancel, log); }, "collision rejected");
    require(crcFile(first, cancel) == before, "collision preserves existing");

    // 4) Errore di mkvmerge dopo la creazione del temporaneo: pulizia
    fs::path junk = root / L"rotto.ac3";
    std::ofstream(junk, std::ios::binary) << "non audio";
    Job broken = makeJob(fixture);
    broken.sources[0] = identify(fixture);
    Source fake;
    fake.path = junk;
    fake.analyzed = true;
    fake.tracks.push_back({});
    fake.tracks[0].kind = Kind::Audio;
    fake.tracks[0].ordinal = 1;
    broken.sources.push_back(fake);
    resetEdits(broken);
    o.output = root / L"bad";
    rejects([&] { process(broken, 4, o, cancel, log); }, "mkvmerge failure");
    require(fs::is_empty(o.output), "failure cleans temporary");

    cancel = true;
    rejects([&] { process(job, 1, o, cancel, log); });
    rejects([&] { crcFile(a, cancel); });
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc >= 2 && std::wstring(argv[1]) == L"--child") {
            if (argc < 4) return 9;
            if (std::wstring(argv[2]) == L"sleep") { Sleep(10000); return 0; }
            if (std::wstring(argv[3]) != L"spazi è \"quote\" e slash\\") return 8;
            DWORD written;
            const char line[] = "UTF-8: \xC3\xA8\nlast line";
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, sizeof(line) - 1, &written, nullptr);
            return 1;
        }
        unitTests();
        std::wcout << L"Core: OK (" << checks << L" verifiche)\n";
#ifndef MKVFLOW_NO_PROCESS_TESTS
        processTests(fs::absolute(argv[0]).c_str());
        std::wcout << L"Processi: OK\n";
        if (argc >= 3) {
            integrationTests(argv[1], argv[2]);
            std::wcout << L"Integrazione MKVToolNix: OK\n";
        }
#endif
        std::wcout << L"\nPASS: tutti i test superati (" << checks << L" verifiche)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
