// Checks of the core without a window: the loaders (files written to a
// temp folder), the render mesh, the library (a fresh database) and the
// folder scan. Prints FAIL lines; exit code 1 if any.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "jobs.h"
#include "library.h"
#include "model_loader.h"
#include "scanner.h"
#include "util.h"

namespace {
int g_checks = 0, g_failed = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failed;
    std::printf("FAIL: %s\n", what);
  }
}

void write(const std::filesystem::path& p, const std::string& text) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << text;
}

// A unit cube as OBJ: 8 vertices, 6 quads (12 triangles), two objects.
const char* kCubeObj = R"(# cube
mtllib cube.mtl
o Top
v -1 -1 -1
v 1 -1 -1
v 1 1 -1
v -1 1 -1
v -1 -1 1
v 1 -1 1
v 1 1 1
v -1 1 1
usemtl grey
f 1 2 3 4
f 5 8 7 6
f 1 5 6 2
o Bottom
f 2 6 7 3
f 3 7 8 4
f 5 1 4 8
)";

// One triangle as glTF with the buffer inline (base64): positions only.
std::string triangleGltf() {
  // 3 vec3 floats = 36 bytes: (0,0,0) (1,0,0) (0,1,0)
  const unsigned char bytes[36] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0, 0, 0, 0};
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string enc;
  for (int i = 0; i < 36; i += 3) {
    const unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    enc += b64[(v >> 18) & 63];
    enc += b64[(v >> 12) & 63];
    enc += b64[(v >> 6) & 63];
    enc += b64[v & 63];
  }
  return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"Tri","mesh":0,"translation":[0,2,0]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" +
         enc + R"("}],
"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}]})";
}

std::string binaryStl() {
  std::string s(80, '\0');
  const uint32_t n = 1;
  s.append(reinterpret_cast<const char*>(&n), 4);
  const float tri[12] = {0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0};
  s.append(reinterpret_cast<const char*>(tri), 48);
  s.append(2, '\0');
  return s;
}
}  // namespace

int main() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / L"cc_selftest";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const std::filesystem::path models = root / L"models";
  // A folder name with Cyrillic in it: paths must survive as UTF-8.
  const std::filesystem::path sub = models / L"папка";
  write(models / L"cube.obj", kCubeObj);
  write(sub / L"tri.gltf", triangleGltf());
  write(models / L"tri.stl", binaryStl());
  write(models / L"notes.txt", "not a model");
  write(models / L".hidden" / L"skip.obj", kCubeObj);

  // --- loaders ---
  {
    Model m;
    std::string err;
    check(loadModel(pathU8(models / L"cube.obj"), m, err), "OBJ loads");
    check(m.meshes.size() == 2, "OBJ: two objects");
    check(m.triangleCount() == 12, "OBJ: 12 triangles");
    check(m.meshes.size() == 2 && m.meshes[0].name == "Top" && m.meshes[1].name == "Bottom", "OBJ: object names");
    check(m.materials == 1, "OBJ: one material");
    RenderMesh rm = buildRenderMesh(m);
    check(rm.indices.size() == 36, "render mesh: 36 indices");
    // A cube's corners split by the auto smooth: 3 normals each = 24 vertices.
    check(rm.vertices.size() / 6 == 24, "render mesh: hard edges split (24 vertices)");
    check(rm.parts.size() == 2, "render mesh: two parts");
    check(rm.min == glm::vec3(-1) && rm.max == glm::vec3(1), "render mesh: bounds");
    ModelStats st = modelStats(m, rm);
    check(st.size == glm::vec3(2), "stats: size 2x2x2");
  }
  {
    Model m;
    std::string err;
    check(loadModel(pathU8(sub / L"tri.gltf"), m, err), ("glTF loads: " + err).c_str());
    check(m.triangleCount() == 1 && m.metric, "glTF: one triangle, meters");
    check(!m.meshes.empty() && m.meshes[0].name == "Tri", "glTF: node name");
    check(!m.meshes.empty() && m.meshes[0].positions[0].y == 2.0f, "glTF: node translation applied");
  }
  {
    Model m;
    std::string err;
    check(loadModel(pathU8(models / L"tri.stl"), m, err), "STL loads");
    check(m.triangleCount() == 1 && m.vertexCount() == 3, "STL: one triangle");
  }
  {
    Model m;
    std::string err;
    check(!loadModel(pathU8(models / L"notes.txt"), m, err), "a text file isn't a model");
  }

  // --- scan ---
  const std::vector<FileEntry> files = scanFolder(pathU8(models));
  check(files.size() == 3, "scan: three models, .txt and .hidden skipped");

  // --- library ---
  {
    Library lib;
    std::string err;
    check(lib.open(root / L"lib.db", err), "library opens");
    const int64_t f = lib.addFolder(pathU8(models));
    check(f > 0, "folder added");
    check(lib.addFolder(pathU8(sub)) == f, "a subfolder of a watched one is that one");
    Library::SyncResult r = lib.syncFolder(f, files);
    check(r.added == 3 && lib.assets().size() == 3, "sync: three added");
    r = lib.syncFolder(f, files);
    check(r.added == 0 && r.changed == 0 && r.missing == 0, "sync again: nothing new");
    std::vector<FileEntry> fewer(files.begin(), files.end() - 1);
    r = lib.syncFolder(f, fewer);
    check(r.missing == 1, "sync: a gone file is missing");
    r = lib.syncFolder(f, files);
    int missing = 0;
    for (const Asset& a : lib.assets()) missing += a.missing;
    check(missing == 0, "sync: found again");
    const int64_t id = lib.assets()[0].id;
    check(lib.assets()[0].needsAnalysis(), "new asset needs analysis");
    ModelStats st;
    st.triangles = 12;
    lib.setAnalysis(id, st, lib.assets()[0].mtime);
    check(!lib.find(id)->needsAnalysis() && lib.find(id)->triangles == 12, "analysis stored");
    const int64_t c = lib.createCollection("Props");
    lib.addToCollection(c, {id});
    check(lib.inCollection(c, id) && lib.collectionSize(c) == 1, "collection membership");
    lib.setHidden({id}, true);
    check(lib.collectionSize(c) == 0, "hidden asset not counted");
    lib.setHidden({id}, false);
    lib.setSetting("ui.tile", "200");
    lib.close();

    Library again;
    check(again.open(root / L"lib.db", err), "library reopens");
    check(again.assets().size() == 3, "assets persisted");
    check(again.find(id) && again.find(id)->triangles == 12, "analysis persisted");
    check(again.inCollection(c, id), "collection persisted");
    check(again.settingInt("ui.tile", 0) == 200, "setting persisted");
    bool cyrillic = false;
    for (const Asset& a : again.assets()) cyrillic |= a.path.find("\xD0\xBF\xD0\xB0\xD0\xBF\xD0\xBA\xD0\xB0") != std::string::npos;
    check(cyrillic, "UTF-8 path kept");
    again.removeFolder(f);
    check(again.assets().empty() && again.folders().empty(), "folder removed with its assets");
  }

  // --- jobs ---
  {
    Jobs jobs(2);
    std::atomic<int> sum{0};
    for (int i = 1; i <= 100; ++i) jobs.run([&sum, i] { sum += i; });
    while (jobs.busy() > 0) std::this_thread::yield();
    check(sum == 5050, "jobs run");
    int onMain = 0;
    jobs.run([&jobs, &onMain] { jobs.post([&onMain] { onMain = 1; }); });
    while (jobs.busy() > 0) std::this_thread::yield();
    jobs.drainMain(100.0);
    check(onMain == 1, "post reaches the main thread");
  }

  std::filesystem::remove_all(root, ec);
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
