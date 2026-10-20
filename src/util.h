#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& w);
std::string lower(std::string s);
// Case-insensitive substring test (ASCII letters; the rest compared as is).
bool containsNoCase(const std::string& haystack, const std::string& needle);

std::filesystem::path u8(const std::string& utf8Path);
std::string pathU8(const std::filesystem::path& p);

// "1.91 MB", "48 712", "Oct 20, 2026  19:01".
std::string formatBytes(int64_t bytes);
std::string formatCount(int64_t n);
std::string formatDate(int64_t unixSeconds);
std::string formatLength(float meters, bool metric);

int64_t nowSeconds();
// A file's modification time as Unix seconds (0 when it can't be read).
int64_t fileTimeSeconds(const std::filesystem::path& p);

// Where the app keeps its library and caches: %LOCALAPPDATA%\CreativeCell
// unless set (command line --data <dir>).
void setDataDir(const std::filesystem::path& dir);
const std::filesystem::path& dataDir();
std::filesystem::path exeDir();

// Windows shell.
void revealInExplorer(const std::string& utf8Path);
bool openWithDefaultApp(const std::string& utf8Path);
// To the Recycle Bin (can be restored from there).
bool moveToRecycleBin(const std::vector<std::string>& utf8Paths);
