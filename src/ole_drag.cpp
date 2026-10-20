#include "ole_drag.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>

#include <cstring>

#include "util.h"

namespace {
// The file list as Windows passes it: DROPFILES then wide paths, each
// NUL-ended, and one more NUL.
HGLOBAL makeHdrop(const std::vector<std::wstring>& paths) {
  size_t chars = 1;
  for (const std::wstring& p : paths) chars += p.size() + 1;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
  if (!h) return nullptr;
  auto* df = static_cast<DROPFILES*>(GlobalLock(h));
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  wchar_t* out = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(df) + sizeof(DROPFILES));
  for (const std::wstring& p : paths) {
    std::memcpy(out, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
    out += p.size() + 1;
  }
  *out = L'\0';
  GlobalUnlock(h);
  return h;
}

class FileDataObject : public IDataObject {
 public:
  explicit FileDataObject(std::vector<std::wstring> paths) : paths_(std::move(paths)) {
    effectFormat_ = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Preferred DropEffect"));
  }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *ppv = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return r;
  }

  // IDataObject
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fmt, STGMEDIUM* med) override {
    if (!(fmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    HGLOBAL h = nullptr;
    if (fmt->cfFormat == CF_HDROP) {
      h = makeHdrop(paths_);
    } else if (fmt->cfFormat == effectFormat_) {
      // Explorer: copy, never move.
      h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
      if (h) {
        *static_cast<DWORD*>(GlobalLock(h)) = DROPEFFECT_COPY;
        GlobalUnlock(h);
      }
    } else {
      return DV_E_FORMATETC;
    }
    if (!h) return E_OUTOFMEMORY;
    med->tymed = TYMED_HGLOBAL;
    med->hGlobal = h;
    med->pUnkForRelease = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fmt) override {
    if (!(fmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    return fmt->cfFormat == CF_HDROP || fmt->cfFormat == effectFormat_ ? S_OK : DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
    out->ptd = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
    if (dir != DATADIR_GET) return E_NOTIMPL;
    FORMATETC fmts[2] = {{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
                         {effectFormat_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
    return SHCreateStdEnumFmtEtc(2, fmts, out);
  }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

 private:
  std::vector<std::wstring> paths_;
  CLIPFORMAT effectFormat_ = 0;
  LONG refs_ = 1;
};

class DropSource : public IDropSource {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppv = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
    if (escape) return DRAGDROP_S_CANCEL;
    if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

 private:
  LONG refs_ = 1;
};
}  // namespace

bool dragFilesOut(const std::vector<std::string>& utf8Paths) {
  if (utf8Paths.empty()) return false;
  std::vector<std::wstring> paths;
  for (const std::string& p : utf8Paths) paths.push_back(u8(p).make_preferred().wstring());
  auto* data = new FileDataObject(std::move(paths));
  auto* source = new DropSource();
  ReleaseCapture();  // (the window held the mouse for the in-app drag)
  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
  source->Release();
  data->Release();
  return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}
