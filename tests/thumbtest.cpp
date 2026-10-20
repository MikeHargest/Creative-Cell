// cc_thumbtest <model> <out.png> [size]: cc_thumbs.dll (next to this exe)
// used as Explorer uses it — COM class, the file as a stream — and the
// bitmap it returns written to a PNG. No registration needed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "shell_ids.h"
#include "stb/stb_image_write.h"
#include "util.h"

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::printf("usage: cc_thumbtest <model> <out.png> [size]\n");
    return 2;
  }
  const UINT size = argc > 3 ? static_cast<UINT>(_wtoi(argv[3])) : 256;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  HMODULE dll = LoadLibraryW((exeDir() / L"cc_thumbs.dll").c_str());
  if (!dll) {
    std::printf("FAIL: cc_thumbs.dll not loaded\n");
    return 1;
  }
  using GetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
  auto get = reinterpret_cast<GetClassObject>(GetProcAddress(dll, "DllGetClassObject"));
  const CLSID clsid = CC_THUMB_CLSID_INIT;
  IClassFactory* factory = nullptr;
  if (!get || FAILED(get(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory)))) {
    std::printf("FAIL: no class factory\n");
    return 1;
  }
  IInitializeWithStream* init = nullptr;
  if (FAILED(factory->CreateInstance(nullptr, IID_IInitializeWithStream, reinterpret_cast<void**>(&init)))) {
    std::printf("FAIL: no instance\n");
    return 1;
  }
  IStream* stream = nullptr;
  if (FAILED(SHCreateStreamOnFileEx(argv[1], STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream)) ||
      FAILED(init->Initialize(stream, STGM_READ))) {
    std::printf("FAIL: couldn't open the file\n");
    return 1;
  }
  IThumbnailProvider* provider = nullptr;
  init->QueryInterface(IID_IThumbnailProvider, reinterpret_cast<void**>(&provider));
  HBITMAP bmp = nullptr;
  WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
  const DWORD t0 = GetTickCount();
  const HRESULT hr = provider ? provider->GetThumbnail(size, &bmp, &alpha) : E_NOINTERFACE;
  const DWORD ms = GetTickCount() - t0;
  if (FAILED(hr) || !bmp) {
    std::printf("FAIL: GetThumbnail 0x%08lx\n", static_cast<unsigned long>(hr));
    return 1;
  }
  BITMAP info = {};
  GetObjectW(bmp, sizeof info, &info);
  const int w = info.bmWidth, h = std::abs(info.bmHeight);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  HDC dc = GetDC(nullptr);
  GetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  // BGRA premultiplied -> RGBA straight.
  for (size_t i = 0; i < px.size(); i += 4) {
    const unsigned a = px[i + 3];
    const unsigned char b = px[i], r = px[i + 2];
    px[i] = static_cast<unsigned char>(a ? r * 255 / a : 0);
    px[i + 1] = static_cast<unsigned char>(a ? px[i + 1] * 255 / a : 0);
    px[i + 2] = static_cast<unsigned char>(a ? b * 255 / a : 0);
  }
  stbi_write_png(toUtf8(argv[2]).c_str(), w, h, 4, px.data(), w * 4);
  std::printf("ok: %dx%d, alpha %d, %lu ms\n", w, h, static_cast<int>(alpha), static_cast<unsigned long>(ms));
  DeleteObject(bmp);
  provider->Release();
  init->Release();
  stream->Release();
  factory->Release();
  return 0;
}
