#include "scanner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "model_loader.h"
#include "util.h"

namespace {
int64_t unixTime(const FILETIME& ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<int64_t>(u.QuadPart / 10000000ull) - 11644473600ll;
}

bool modelName(const wchar_t* name) {
  const wchar_t* dot = wcsrchr(name, L'.');
  return dot && isModelExtension(lower(toUtf8(dot)));
}
}  // namespace

std::vector<FileEntry> scanFolder(const std::string& folder, const std::atomic<bool>* cancel) {
  std::vector<FileEntry> out;
  std::vector<std::wstring> stack{toWide(folder)};
  while (!stack.empty()) {
    if (cancel && cancel->load()) break;
    const std::wstring dir = std::move(stack.back());
    stack.pop_back();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      const wchar_t* n = fd.cFileName;
      if (n[0] == L'.') continue;  // ".", "..", ".git", ...
      const std::wstring full = dir + L"\\" + n;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) stack.push_back(full);  // no junction loops
      } else if (modelName(n)) {
        FileEntry e;
        e.path = toUtf8(full);
        e.size = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.mtime = unixTime(fd.ftLastWriteTime);
        out.push_back(std::move(e));
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  return out;
}

bool statFile(const std::string& path, FileEntry& out) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(toWide(path).c_str(), GetFileExInfoStandard, &d)) return false;
  if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
  out.path = path;
  out.size = (static_cast<int64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
  out.mtime = unixTime(d.ftLastWriteTime);
  return true;
}
