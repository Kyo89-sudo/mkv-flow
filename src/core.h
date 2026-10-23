#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <cstdint>
namespace app {
namespace fs = std::filesystem;
using Log = std::function<void(const std::wstring&)>;
struct Rule { std::wstring selector, name, language, def, forced; };
struct Options {
    fs::path tools, output;
    std::wstring pattern=L"{name} [{crc}]", title;
    bool setTitle=false, remux=true;
    std::vector<Rule> rules;
};
std::wstring quote(const std::wstring& s);
std::vector<Rule> parseRules(const std::wstring& text);
std::wstring expand(std::wstring text, const std::wstring& name, size_t index, const std::wstring& crc);
std::wstring fileName(const std::wstring& text);
uint32_t crcUpdate(uint32_t crc, const unsigned char* data, size_t size);
std::wstring crcFile(const fs::path& path, std::atomic_bool& cancel);
DWORD run(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel, Log log);
fs::path process(const fs::path& input, size_t index, const Options& options, std::atomic_bool& cancel, Log log);
}
