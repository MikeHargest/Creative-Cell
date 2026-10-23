#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// One mesh of a file (an FBX / glTF node's mesh, an OBJ object or group),
// already placed in the file's world, Y up. FBX and glTF come in meters;
// OBJ and STL as they are (they have no unit).
struct SubMesh {
  std::string name;
  std::vector<glm::vec3> positions;
  std::vector<uint32_t> indices;  // 3 per triangle
  // One normal per triangle corner (in `indices` order) when the file has
  // them; empty — buildRenderMesh works them out (auto smooth).
  std::vector<glm::vec3> cornerNormals;
};

struct Model {
  std::vector<SubMesh> meshes;
  int materials = 0;
  bool metric = false;  // units are meters (FBX, glTF)
  size_t triangleCount() const;
  size_t vertexCount() const;
};

// Asked now and then while a file loads (from the loading thread): true
// gives up — the load fails with kCancelled. FBX and OBJ listen; the others
// are quick.
using CancelFn = std::function<bool()>;
extern const char* const kCancelled;

// Reads an OBJ, STL, FBX, glTF or GLB file. False with a message when it
// can't be read or holds no triangles. An FBX is read from the disk as it
// is parsed (never whole in memory).
bool loadModel(const std::string& utf8Path, Model& out, std::string& error, const CancelFn& cancel = {});
// The same from bytes already read; `lowerExt` with the dot (".fbx").
// `sourcePath` lets a .gltf find its .bin files (empty: embedded only).
bool loadModelFromMemory(const std::string& data, const std::string& lowerExt, Model& out, std::string& error,
                         const std::filesystem::path& sourcePath = {}, const CancelFn& cancel = {});

// The extensions loadModel reads, lower case with the dot.
bool isModelExtension(const std::string& lowerExt);

// What the GPU draws: per-vertex position and normal, interleaved
// (6 floats), indexed; each mesh of the model a range of the indices.
struct RenderMesh {
  struct Part {
    std::string name;
    uint32_t firstIndex = 0, indexCount = 0;
    glm::vec3 min{0.0f}, max{0.0f};
  };
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
  std::vector<Part> parts;
  glm::vec3 min{0.0f}, max{0.0f};
  bool empty() const { return indices.empty(); }
};

// Splits vertices where normals differ, so hard edges stay hard. Meshes
// without normals get them from the faces: smooth across bends under
// `smoothAngleDeg`, a clean break above it.
RenderMesh buildRenderMesh(const Model& model, float smoothAngleDeg = 40.0f);

// What the library keeps about a file (from the loaded model).
struct ModelStats {
  int64_t triangles = 0, vertices = 0;
  int parts = 0, materials = 0;
  glm::vec3 size{0.0f};
  bool metric = false;
};
ModelStats modelStats(const Model& model, const RenderMesh& mesh);
