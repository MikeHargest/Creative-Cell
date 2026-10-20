#include "shell_integration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>

#include "shell_ids.h"
#include "util.h"

namespace {
const std::wstring kClasses = L"Software\\Classes\\";

std::wstring exePath() {
  wchar_t buf[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return buf;
}

std::wstring dllPath() { return (exeDir() / L"cc_thumbs.dll").wstring(); }

bool setString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
  return RegSetKeyValueW(HKEY_CURRENT_USER, (kClasses + key).c_str(), name, REG_SZ, value.c_str(),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

std::wstring getString(const std::wstring& key, const wchar_t* name) {
  wchar_t buf[1024] = L"";
  DWORD size = sizeof buf;
  if (RegGetValueW(HKEY_CURRENT_USER, (kClasses + key).c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &size) !=
      ERROR_SUCCESS) {
    return L"";
  }
  return buf;
}
}  // namespace

bool registerShellIntegration(std::string& error) {
  const std::wstring exe = exePath(), dll = dllPath();
  std::error_code ec;
  if (!std::filesystem::exists(dll, ec)) {
    error = "cc_thumbs.dll isn't next to the program.";
    return false;
  }
  const std::wstring open = L"\"" + exe + L"\" \"%1\"";
  const std::wstring clsidKey = L"CLSID\\" CC_THUMB_CLSID_STR;
  bool ok = true;
  // The thumbnail handler.
  ok &= setString(clsidKey, nullptr, L"Creative Cell Thumbnails");
  ok &= setString(clsidKey + L"\\InprocServer32", nullptr, dll);
  ok &= setString(clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
  // The file type Creative Cell opens.
  ok &= setString(CC_PROGID, nullptr, L"3D Model");
  ok &= setString(std::wstring(CC_PROGID) + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
  ok &= setString(std::wstring(CC_PROGID) + L"\\shell\\open\\command", nullptr, open);
  ok &= setString(std::wstring(CC_PROGID) + L"\\ShellEx\\" CC_THUMB_SHELLEX, nullptr, CC_THUMB_CLSID_STR);
  // The program, as "Open with" lists it.
  const std::wstring app = L"Applications\\CreativeCell.exe";
  ok &= setString(app, L"FriendlyAppName", L"Creative Cell");
  ok &= setString(app + L"\\shell\\open\\command", nullptr, open);
  for (const wchar_t* ext : kShellExtensions) {
    ok &= setString(std::wstring(ext) + L"\\OpenWithProgids", CC_PROGID, L"");
    ok &= setString(app + L"\\SupportedTypes", ext, L"");
    ok &= setString(std::wstring(ext) + L"\\ShellEx\\" CC_THUMB_SHELLEX, nullptr, CC_THUMB_CLSID_STR);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (!ok) error = "Couldn't write to the registry.";
  return ok;
}

void unregisterShellIntegration() {
  HKEY classes = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_ALL_ACCESS, &classes) != ERROR_SUCCESS) return;
  RegDeleteTreeW(classes, L"CLSID\\" CC_THUMB_CLSID_STR);
  RegDeleteTreeW(classes, CC_PROGID);
  RegDeleteTreeW(classes, L"Applications\\CreativeCell.exe");
  for (const wchar_t* ext : kShellExtensions) {
    RegDeleteKeyValueW(classes, (std::wstring(ext) + L"\\OpenWithProgids").c_str(), CC_PROGID);
    // The thumbnail slot only if it's still ours.
    const std::wstring slot = std::wstring(ext) + L"\\ShellEx\\" CC_THUMB_SHELLEX;
    if (_wcsicmp(getString(slot, nullptr).c_str(), CC_THUMB_CLSID_STR) == 0) RegDeleteTreeW(classes, slot.c_str());
  }
  RegCloseKey(classes);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool shellIntegrationRegistered() {
  // HKEY_CLASSES_ROOT: the user's keys over the machine's, as Explorer sees them.
  wchar_t buf[1024] = L"";
  DWORD size = sizeof buf;
  if (RegGetValueW(HKEY_CLASSES_ROOT, L"CLSID\\" CC_THUMB_CLSID_STR L"\\InprocServer32", nullptr, RRF_RT_REG_SZ,
                   nullptr, buf, &size) != ERROR_SUCCESS) {
    return false;
  }
  return _wcsicmp(buf, dllPath().c_str()) == 0;
}

bool shellIntegrationByInstaller() {
  DWORD v = 0, size = sizeof v;
  return RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\CreativeCell", L"ShellIntegration", RRF_RT_REG_DWORD, nullptr,
                      &v, &size) == ERROR_SUCCESS &&
         v == 1;
}

void openDefaultAppsSettings() {
  ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}
