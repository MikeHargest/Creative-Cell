#pragma once

// What Creative Cell puts into Windows: the thumbnail handler's COM class
// (cc_thumbs.dll) and the file types it opens and draws.
#define CC_THUMB_CLSID_STR L"{A754A1D3-E473-47F8-A3AC-F53738315A69}"
// {A754A1D3-E473-47F8-A3AC-F53738315A69}
#define CC_THUMB_CLSID_INIT \
  { 0xa754a1d3, 0xe473, 0x47f8, { 0xa3, 0xac, 0xf5, 0x37, 0x38, 0x31, 0x5a, 0x69 } }

#define CC_PROGID L"CreativeCell.Model"
// IThumbnailProvider's shell extension slot.
#define CC_THUMB_SHELLEX L"{e357fccd-a995-4576-b01f-234630154e96}"

inline const wchar_t* const kShellExtensions[] = {L".fbx", L".obj", L".gltf", L".glb", L".stl"};
