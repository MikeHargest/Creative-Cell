#include "file_dialog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "util.h"

namespace {
std::string itemPath(IShellItem* item) {
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
    out = toUtf8(p);
    CoTaskMemFree(p);
  }
  return out;
}
}  // namespace

std::vector<std::string> openModelFilesDialog(GLFWwindow* window) {
  std::vector<std::string> out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
  const COMDLG_FILTERSPEC types[] = {
      {L"3D models (*.fbx; *.obj; *.gltf; *.glb; *.stl)", L"*.fbx;*.obj;*.gltf;*.glb;*.stl"},
      {L"All files", L"*.*"},
  };
  dlg->SetFileTypes(2, types);
  dlg->SetTitle(L"Add Files");
  if (SUCCEEDED(dlg->Show(glfwGetWin32Window(window)))) {
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(dlg->GetResults(&items))) {
      DWORD n = 0;
      items->GetCount(&n);
      for (DWORD i = 0; i < n; ++i) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &it))) {
          const std::string p = itemPath(it);
          if (!p.empty()) out.push_back(p);
          it->Release();
        }
      }
      items->Release();
    }
  }
  dlg->Release();
  return out;
}

std::string pickFolderDialog(GLFWwindow* window, const char* title) {
  std::string out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(toWide(title).c_str());
  if (SUCCEEDED(dlg->Show(glfwGetWin32Window(window)))) {
    IShellItem* it = nullptr;
    if (SUCCEEDED(dlg->GetResult(&it))) {
      out = itemPath(it);
      it->Release();
    }
  }
  dlg->Release();
  return out;
}
