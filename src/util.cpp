#include "util.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

std::wstring toWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool containsNoCase(const std::string& h, const std::string& n) {
  if (n.empty()) return true;
  auto it = std::search(h.begin(), h.end(), n.begin(), n.end(), [](unsigned char a, unsigned char b) {
    return std::tolower(a) == std::tolower(b);
  });
  return it != h.end();
}

std::filesystem::path u8(const std::string& p) { return std::filesystem::path(toWide(p)); }
std::string pathU8(const std::filesystem::path& p) { return toUtf8(p.wstring()); }

std::string formatBytes(int64_t b) {
  char buf[32];
  if (b < 1024) {
    std::snprintf(buf, sizeof buf, "%lld B", static_cast<long long>(b));
  } else if (b < 1024 * 1024) {
    std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
  } else if (b < 1024ll * 1024 * 1024) {
    std::snprintf(buf, sizeof buf, "%.2f MB", b / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof buf, "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
  }
  return buf;
}

std::string formatCount(int64_t n) {
  if (n < 0) return "—";
  std::string digits = std::to_string(n), out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i && (digits.size() - i) % 3 == 0) out += ' ';
    out += digits[i];
  }
  return out;
}

std::string formatDate(int64_t t) {
  if (t <= 0) return "—";
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  localtime_s(&tm, &tt);
  char buf[64];
  std::strftime(buf, sizeof buf, "%b %d, %Y  %H:%M", &tm);
  return buf;
}

std::string formatLength(float v, bool metric) {
  char buf[32];
  if (!metric) {
    std::snprintf(buf, sizeof buf, v >= 100.0f ? "%.0f" : v >= 10.0f ? "%.1f" : "%.2f", v);
    return buf;
  }
  if (v >= 1.0f) {
    std::snprintf(buf, sizeof buf, "%.2f m", v);
  } else if (v >= 0.01f) {
    std::snprintf(buf, sizeof buf, "%.1f cm", v * 100.0f);
  } else {
    std::snprintf(buf, sizeof buf, "%.1f mm", v * 1000.0f);
  }
  return buf;
}

int64_t nowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t fileTimeSeconds(const std::filesystem::path& p) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
  ULARGE_INTEGER u;
  u.LowPart = d.ftLastWriteTime.dwLowDateTime;
  u.HighPart = d.ftLastWriteTime.dwHighDateTime;
  return static_cast<int64_t>(u.QuadPart / 10000000ull) - 11644473600ll;
}

namespace {
std::filesystem::path g_dataDir;
}

void setDataDir(const std::filesystem::path& dir) { g_dataDir = dir; }

const std::filesystem::path& dataDir() {
  if (g_dataDir.empty()) {
    wchar_t* local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
      g_dataDir = std::filesystem::path(local) / L"CreativeCell";
    } else {
      g_dataDir = exeDir() / L"data";
    }
    CoTaskMemFree(local);
  }
  std::error_code ec;
  std::filesystem::create_directories(g_dataDir, ec);
  return g_dataDir;
}

std::filesystem::path exeDir() {
  wchar_t buf[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(buf).parent_path();
}

void revealInExplorer(const std::string& utf8Path) {
  const std::wstring p = u8(utf8Path).make_preferred().wstring();
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(p.c_str());
  if (pidl) {
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
  } else {
    ShellExecuteW(nullptr, L"open", u8(utf8Path).parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}

bool openWithDefaultApp(const std::string& utf8Path) {
  const std::wstring p = u8(utf8Path).make_preferred().wstring();
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool moveToRecycleBin(const std::vector<std::string>& paths) {
  if (paths.empty()) return true;
  std::wstring list;  // double-NUL-terminated
  for (const std::string& p : paths) {
    list += u8(p).make_preferred().wstring();
    list.push_back(L'\0');
  }
  list.push_back(L'\0');
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = list.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}
