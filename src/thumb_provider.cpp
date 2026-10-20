// cc_thumbs.dll: Explorer's thumbnails of 3D files, drawn as the library
// draws them (model_loader + soft_render). Windows loads it in an isolated
// process and hands it the file as a stream.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include <propsys.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "model_loader.h"
#include "shell_ids.h"
#include "soft_render.h"
#include "util.h"

namespace {
const CLSID kClsid = CC_THUMB_CLSID_INIT;
std::atomic<long> g_objects{0}, g_locks{0};
constexpr ULONGLONG kMaxBytes = 512ull * 1024 * 1024;  // bigger files get no thumbnail

// The format from the bytes, when the stream has no name.
std::string sniffExtension(const std::string& d) {
  if (d.size() >= 4 && std::memcmp(d.data(), "glTF", 4) == 0) return ".glb";
  if (d.size() >= 18 && std::memcmp(d.data(), "Kaydara FBX Binary", 18) == 0) return ".fbx";
  if (d.rfind("; FBX", 0) == 0 || d.find("FBXHeaderExtension") < 4096) return ".fbx";
  if (d.size() >= 84) {
    uint32_t n = 0;
    std::memcpy(&n, d.data() + 80, 4);
    if (84 + static_cast<uint64_t>(n) * 50 == d.size()) return ".stl";
  }
  if (d.rfind("solid", 0) == 0 && d.find("facet") < 4096) return ".stl";
  const size_t first = d.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && d[first] == '{') return ".gltf";
  return ".obj";
}

class ThumbnailProvider : public IThumbnailProvider, public IInitializeWithStream {
 public:
  ThumbnailProvider() { ++g_objects; }
  ~ThumbnailProvider() {
    if (stream_) stream_->Release();
    --g_objects;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    static const QITAB qit[] = {
        QITABENT(ThumbnailProvider, IInitializeWithStream),
        QITABENT(ThumbnailProvider, IThumbnailProvider),
        {nullptr, 0},
    };
    return QISearch(this, qit, riid, ppv);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return r;
  }

  HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD) override {
    if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    stream_ = stream;
    stream_->AddRef();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
    if (!stream_ || !bitmap) return E_UNEXPECTED;
    *bitmap = nullptr;
    // The bytes, and the name if the stream knows it.
    STATSTG st = {};
    std::string ext;
    if (SUCCEEDED(stream_->Stat(&st, STATFLAG_DEFAULT))) {
      if (st.pwcsName) {
        const wchar_t* dot = wcsrchr(st.pwcsName, L'.');
        if (dot) ext = lower(toUtf8(dot));
        CoTaskMemFree(st.pwcsName);
      }
      if (st.cbSize.QuadPart > kMaxBytes) return E_FAIL;
    }
    std::string data;
    data.reserve(static_cast<size_t>(st.cbSize.QuadPart));
    char buf[1 << 16];
    for (;;) {
      ULONG got = 0;
      const HRESULT hr = stream_->Read(buf, sizeof buf, &got);
      if (got) data.append(buf, got);
      if (FAILED(hr) || got == 0 || data.size() > kMaxBytes) break;
    }
    if (data.empty() || data.size() > kMaxBytes) return E_FAIL;
    if (!isModelExtension(ext)) ext = sniffExtension(data);

    Model model;
    std::string error;
    if (!loadModelFromMemory(data, ext, model, error)) return E_FAIL;
    data.clear();
    data.shrink_to_fit();
    const RenderMesh mesh = buildRenderMesh(model);
    model = Model();
    const int size = std::clamp(static_cast<int>(cx), 16, 1024);
    std::vector<uint8_t> rgba;
    if (!renderThumbnailCpu(mesh, size, rgba)) return E_FAIL;

    // A top-down 32-bit DIB, BGRA premultiplied.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return E_OUTOFMEMORY;
    uint8_t* out = static_cast<uint8_t*>(bits);
    for (size_t i = 0; i < static_cast<size_t>(size) * size; ++i) {
      const uint8_t* s = &rgba[i * 4];
      const unsigned a = s[3];
      out[i * 4 + 0] = static_cast<uint8_t>(s[2] * a / 255);
      out[i * 4 + 1] = static_cast<uint8_t>(s[1] * a / 255);
      out[i * 4 + 2] = static_cast<uint8_t>(s[0] * a / 255);
      out[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    *bitmap = bmp;
    if (alpha) *alpha = WTSAT_ARGB;
    return S_OK;
  }

 private:
  LONG refs_ = 1;
  IStream* stream_ = nullptr;
};

class ClassFactory : public IClassFactory {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }  // (one static instance)
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* p = new (std::nothrow) ThumbnailProvider();
    if (!p) return E_OUTOFMEMORY;
    const HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock) ++g_locks;
    else --g_locks;
    return S_OK;
  }
};
ClassFactory g_factory;
}  // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (clsid != kClsid) return CLASS_E_CLASSNOTAVAILABLE;
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE; }
