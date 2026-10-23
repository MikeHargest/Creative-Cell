#include "model_loader.h"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <glm/gtc/type_ptr.hpp>

#include "ufbx/ufbx.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

namespace {
bool readFile(const std::filesystem::path& path, std::string& data) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  if (size < 0) return false;
  data.resize(static_cast<size_t>(size));
  f.seekg(0);
  return static_cast<bool>(f.read(data.data(), size)) || size == 0;
}

std::string lowerExt(const std::filesystem::path& p) {
  std::string ext = p.extension().u8string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// The number at `q` (spaces skipped), `q` moved past it; 0 when there is
// none (the word is skipped). from_chars: several times strtof's speed, and
// no locale.
float parseFloat(const char*& q, const char* end) {
  while (q < end && (isSpace(*q) || *q == '\n')) ++q;
  if (q < end && *q == '+') ++q;
  float v = 0.0f;
  const std::from_chars_result r = std::from_chars(q, end, v);
  if (r.ec == std::errc::invalid_argument) {
    while (q < end && !isSpace(*q) && *q != '\n') ++q;
    return 0.0f;
  }
  q = r.ptr;
  return r.ec == std::errc() ? v : 0.0f;  // (out of range: a denormal, near enough 0)
}

long parseLong(const char*& q, const char* end, bool& ok) {
  if (q < end && *q == '+') ++q;
  long v = 0;
  const std::from_chars_result r = std::from_chars(q, end, v);
  ok = r.ec == std::errc();
  if (r.ptr != q) q = r.ptr;
  return v;
}

// --- OBJ: one mesh per "o" / "g"; normals kept when every face has them ---
bool loadObj(const std::string& s, Model& out, const CancelFn& cancel) {
  std::vector<glm::vec3> pos, nrm;
  std::unordered_map<std::string, int> materials;
  std::vector<long> face, faceN;
  // Each mesh indexes the file's positions (compacted per mesh at the end).
  struct Raw {
    std::string name;
    std::vector<uint32_t> idx;
    std::vector<int32_t> nidx;  // per corner, -1 when the face has none
  };
  std::vector<Raw> raws;
  auto begin = [&](std::string name) {
    if (!raws.empty() && raws.back().idx.empty()) {
      if (!name.empty()) raws.back().name = std::move(name);
      return;
    }
    raws.push_back({std::move(name), {}, {}});
  };
  const char* p = s.c_str();
  const char* end = p + s.size();
  uint32_t lines = 0;
  while (p < end) {
    if ((++lines & 0xFFFF) == 0 && cancel && cancel()) return false;
    const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', end - p));
    if (!lineEnd) lineEnd = end;
    while (p < lineEnd && isSpace(*p)) ++p;
    const ptrdiff_t len = lineEnd - p;
    if (len > 2 && p[0] == 'v' && isSpace(p[1])) {
      const char* q = p + 2;
      float v[3];
      for (float& x : v) x = parseFloat(q, lineEnd);
      pos.emplace_back(v[0], v[1], v[2]);
    } else if (len > 3 && p[0] == 'v' && p[1] == 'n' && isSpace(p[2])) {
      const char* q = p + 3;
      float v[3];
      for (float& x : v) x = parseFloat(q, lineEnd);
      nrm.emplace_back(v[0], v[1], v[2]);
    } else if (len > 2 && (p[0] == 'o' || p[0] == 'g') && isSpace(p[1])) {
      const char* q = p + 2;
      const char* e = lineEnd;
      while (e > q && isSpace(e[-1])) --e;
      begin(std::string(q, e));
    } else if (len > 7 && std::strncmp(p, "usemtl", 6) == 0 && isSpace(p[6])) {
      const char* q = p + 7;
      const char* e = lineEnd;
      while (e > q && isSpace(e[-1])) --e;
      materials.emplace(std::string(q, e), 0);
    } else if (len > 2 && p[0] == 'f' && isSpace(p[1])) {
      // "f 1 2 3", "f 1/1 2/2", "f 1//1", "f 1/1/1", "f -3 -2 -1".
      face.clear();
      faceN.clear();
      const char* q = p + 2;
      while (q < lineEnd) {
        while (q < lineEnd && isSpace(*q)) ++q;
        if (q >= lineEnd) break;
        bool ok = false;
        const long idx = parseLong(q, lineEnd, ok);
        if (!ok) break;
        face.push_back(idx < 0 ? static_cast<long>(pos.size()) + idx : idx - 1);
        long n = -1;
        if (q < lineEnd && *q == '/') {
          ++q;
          if (q < lineEnd && *q != '/') parseLong(q, lineEnd, ok);  // texture coordinate: not needed
          if (q < lineEnd && *q == '/') {
            ++q;
            const long ni = parseLong(q, lineEnd, ok);
            if (ok) n = ni < 0 ? static_cast<long>(nrm.size()) + ni : ni - 1;
          }
        }
        faceN.push_back(n);
        while (q < lineEnd && !isSpace(*q)) ++q;
      }
      if (raws.empty()) begin("");
      Raw& r = raws.back();
      const long np = static_cast<long>(pos.size()), nn = static_cast<long>(nrm.size());
      for (size_t k = 2; k < face.size(); ++k) {
        const size_t c[3] = {0, k - 1, k};
        bool ok = true;
        for (size_t i : c) ok &= face[i] >= 0 && face[i] < np;
        if (!ok) continue;  // forward references are rare; dropped
        for (size_t i : c) {
          r.idx.push_back(static_cast<uint32_t>(face[i]));
          r.nidx.push_back(faceN[i] >= 0 && faceN[i] < nn ? static_cast<int32_t>(faceN[i]) : -1);
        }
      }
    }
    p = lineEnd + 1;
  }
  out.materials = static_cast<int>(materials.size());
  // Positions compacted per mesh: each mesh holds only what it uses.
  std::vector<uint32_t> remap(pos.size(), UINT32_MAX);
  for (Raw& r : raws) {
    if (r.idx.empty()) continue;
    SubMesh m;
    m.name = r.name;
    std::vector<uint32_t> used;
    for (uint32_t& i : r.idx) {
      if (remap[i] == UINT32_MAX) {
        remap[i] = static_cast<uint32_t>(m.positions.size());
        m.positions.push_back(pos[i]);
        used.push_back(i);
      }
      m.indices.push_back(remap[i]);
    }
    for (uint32_t i : used) remap[i] = UINT32_MAX;
    const bool allNormals = std::all_of(r.nidx.begin(), r.nidx.end(), [](int32_t n) { return n >= 0; });
    if (allNormals) {
      m.cornerNormals.reserve(r.nidx.size());
      for (int32_t n : r.nidx) m.cornerNormals.push_back(nrm[n]);
    }
    out.meshes.push_back(std::move(m));
  }
  return true;
}

// --- STL: one mesh, positions welded where they are equal ---
void loadStl(const std::string& s, Model& out) {
  SubMesh m;
  std::vector<glm::vec3> corners;
  uint32_t count = 0;
  if (s.size() >= 84) std::memcpy(&count, s.data() + 80, 4);
  const bool binary = s.size() >= 84 && 84 + static_cast<uint64_t>(count) * 50 == s.size();
  if (binary) {
    corners.reserve(static_cast<size_t>(count) * 3);
    for (uint32_t t = 0; t < count; ++t) {
      const char* rec = s.data() + 84 + static_cast<size_t>(t) * 50 + 12;  // past the normal
      for (int k = 0; k < 3; ++k) {
        float v[3];
        std::memcpy(v, rec + k * 12, 12);
        corners.emplace_back(v[0], v[1], v[2]);
      }
    }
  } else {
    // ASCII: every "vertex x y z", three to a facet.
    const char* p = s.c_str();
    const char* end = p + s.size();
    while ((p = std::strstr(p, "vertex")) != nullptr) {
      const char* q = p + 6;
      float v[3];
      for (float& x : v) x = parseFloat(q, end);
      corners.emplace_back(v[0], v[1], v[2]);
      p = q;
    }
    corners.resize(corners.size() / 3 * 3);
  }
  struct Hash {
    size_t operator()(const glm::vec3& v) const {
      uint32_t b[3];
      std::memcpy(b, &v, 12);
      return (b[0] * 73856093u) ^ (b[1] * 19349663u) ^ (b[2] * 83492791u);
    }
  };
  std::unordered_map<glm::vec3, uint32_t, Hash> weld;
  weld.reserve(corners.size() / 2);
  m.indices.reserve(corners.size());
  for (const glm::vec3& c : corners) {
    auto [it, fresh] = weld.emplace(c, static_cast<uint32_t>(m.positions.size()));
    if (fresh) m.positions.push_back(c);
    m.indices.push_back(it->second);
  }
  if (!m.indices.empty()) out.meshes.push_back(std::move(m));
}

// --- FBX through ufbx: every mesh where its node puts it, meters, Y up.
// Animation and embedded files (textures packed in) are skipped: a viewer
// doesn't need them, and they can be most of the file. (ufbx keeps its
// numbers as floats: UFBX_REAL_IS_FLOAT, half the memory of doubles.) ---
ufbx_progress_result fbxProgress(void* user, const ufbx_progress*) {
  const CancelFn& cancel = *static_cast<const CancelFn*>(user);
  return cancel() ? UFBX_PROGRESS_CANCEL : UFBX_PROGRESS_CONTINUE;
}

ufbx_load_opts fbxOptions(const CancelFn& cancel) {
  ufbx_load_opts opts = {};
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  opts.generate_missing_normals = true;
  opts.ignore_animation = true;
  opts.ignore_embedded = true;
  opts.read_buffer_size = 4u << 20;  // (the default 16 KB reads a big file slowly)
  if (cancel) {
    opts.progress_cb.fn = fbxProgress;
    opts.progress_cb.user = const_cast<CancelFn*>(&cancel);
    opts.progress_interval_hint = 8u << 20;
  }
  return opts;
}

// The scene's meshes into `out`; the scene freed.
bool fbxScene(ufbx_scene* scene, const ufbx_error& err, Model& out, std::string& error) {
  if (!scene) {
    error = err.type == UFBX_ERROR_CANCELLED
                ? kCancelled
                : std::string("Couldn't read the FBX: ") + std::string(err.description.data, err.description.length);
    return false;
  }
  out.materials = static_cast<int>(scene->materials.count);
  out.metric = true;
  std::vector<uint32_t> tri;
  for (size_t i = 0; i < scene->nodes.count; ++i) {
    const ufbx_node* node = scene->nodes.data[i];
    const ufbx_mesh* mesh = node->mesh;
    if (!mesh || mesh->faces.count == 0) continue;
    const ufbx_matrix& m = node->geometry_to_world;
    const ufbx_matrix nm = ufbx_matrix_for_normals(&m);
    const double det = m.m00 * (m.m11 * m.m22 - m.m12 * m.m21) - m.m01 * (m.m10 * m.m22 - m.m12 * m.m20) +
                       m.m02 * (m.m10 * m.m21 - m.m11 * m.m20);
    SubMesh sm;
    sm.name.assign(node->name.data, node->name.length);
    sm.positions.reserve(mesh->vertices.count);
    for (size_t v = 0; v < mesh->vertices.count; ++v) {
      const ufbx_vec3 p = ufbx_transform_position(&m, mesh->vertices.data[v]);
      sm.positions.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }
    const bool normals = mesh->vertex_normal.exists;
    tri.resize(mesh->max_face_triangles * 3);
    for (size_t f = 0; f < mesh->faces.count; ++f) {
      const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, mesh->faces.data[f]);
      for (uint32_t t = 0; t < n; ++t) {
        uint32_t c[3] = {tri[t * 3], tri[t * 3 + 1], tri[t * 3 + 2]};
        if (det < 0.0) std::swap(c[1], c[2]);
        for (uint32_t k : c) {
          sm.indices.push_back(mesh->vertex_indices.data[k]);
          if (normals) {
            ufbx_vec3 nv = ufbx_transform_direction(&nm, ufbx_get_vertex_vec3(&mesh->vertex_normal, k));
            glm::vec3 g(static_cast<float>(nv.x), static_cast<float>(nv.y), static_cast<float>(nv.z));
            const float l = glm::length(g);
            sm.cornerNormals.push_back(l > 1e-20f ? g / l : glm::vec3(0, 1, 0));
          }
        }
      }
    }
    if (!sm.indices.empty()) out.meshes.push_back(std::move(sm));
  }
  ufbx_free_scene(scene);
  return true;
}

bool loadFbx(const std::string& s, Model& out, std::string& error, const CancelFn& cancel) {
  const ufbx_load_opts opts = fbxOptions(cancel);
  ufbx_error err;
  return fbxScene(ufbx_load_memory(s.data(), s.size(), &opts, &err), err, out, error);
}

bool loadFbxFile(const std::string& utf8Path, Model& out, std::string& error, const CancelFn& cancel) {
  const ufbx_load_opts opts = fbxOptions(cancel);
  ufbx_error err;
  return fbxScene(ufbx_load_file_len(utf8Path.data(), utf8Path.size(), &opts, &err), err, out, error);
}

// --- glTF / GLB through cgltf: files opened with wide paths (non-ASCII
// folders), meshes placed by their nodes ---
cgltf_result gltfRead(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size,
                      void** data) {
  std::string bytes;
  if (!readFile(std::filesystem::u8path(path), bytes)) return cgltf_result_file_not_found;
  void* mem = std::malloc(bytes.size() ? bytes.size() : 1);
  if (!mem) return cgltf_result_out_of_memory;
  std::memcpy(mem, bytes.data(), bytes.size());
  *size = bytes.size();
  *data = mem;
  return cgltf_result_success;
}

void gltfRelease(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

void gltfNode(const cgltf_node* node, Model& out, int& draco) {
  if (node->mesh) {
    float w[16];
    cgltf_node_transform_world(node, w);
    const glm::mat4 world = glm::make_mat4(w);
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(world)));
    const bool flip = glm::determinant(glm::mat3(world)) < 0.0f;
    SubMesh sm;
    sm.name = node->name ? node->name : (node->mesh->name ? node->mesh->name : "");
    bool allNormals = true;
    std::vector<glm::vec3> normals;
    for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
      const cgltf_primitive& prim = node->mesh->primitives[pi];
      if (prim.has_draco_mesh_compression) {
        ++draco;
        continue;
      }
      if (prim.type != cgltf_primitive_type_triangles) continue;
      const cgltf_accessor* pa = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
      if (!pa || (!pa->buffer_view && !pa->is_sparse)) continue;
      const cgltf_accessor* na = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
      const uint32_t base = static_cast<uint32_t>(sm.positions.size());
      for (cgltf_size v = 0; v < pa->count; ++v) {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(pa, v, p, 3);
        sm.positions.push_back(glm::vec3(world * glm::vec4(p[0], p[1], p[2], 1.0f)));
        if (na) {
          float n[3] = {0, 1, 0};
          cgltf_accessor_read_float(na, v, n, 3);
          glm::vec3 g = nm * glm::vec3(n[0], n[1], n[2]);
          const float l = glm::length(g);
          normals.push_back(l > 1e-20f ? g / l : glm::vec3(0, 1, 0));
        } else {
          normals.emplace_back(0.0f);
        }
      }
      allNormals &= na != nullptr;
      const cgltf_size n = prim.indices ? prim.indices->count : pa->count;
      for (cgltf_size t = 0; t + 2 < n; t += 3) {
        uint32_t c[3];
        for (int k = 0; k < 3; ++k) {
          c[k] = base + static_cast<uint32_t>(prim.indices ? cgltf_accessor_read_index(prim.indices, t + k) : t + k);
        }
        if (flip) std::swap(c[1], c[2]);
        const uint32_t limit = static_cast<uint32_t>(sm.positions.size());
        if (c[0] >= limit || c[1] >= limit || c[2] >= limit) continue;
        sm.indices.insert(sm.indices.end(), c, c + 3);
      }
    }
    if (allNormals) {
      sm.cornerNormals.reserve(sm.indices.size());
      for (uint32_t i : sm.indices) sm.cornerNormals.push_back(normals[i]);
    }
    if (!sm.indices.empty()) out.meshes.push_back(std::move(sm));
  }
  for (cgltf_size i = 0; i < node->children_count; ++i) gltfNode(node->children[i], out, draco);
}

// `path` locates external buffers (.bin); empty — only embedded ones (GLB,
// data: URIs) can be read.
bool loadGltf(const std::string& bytes, const std::filesystem::path& path, Model& out, std::string& error) {
  cgltf_options opts = {};
  opts.file.read = gltfRead;
  opts.file.release = gltfRelease;
  cgltf_data* data = nullptr;
  if (cgltf_parse(&opts, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
    error = "Couldn't read the glTF.";
    return false;
  }
  if (cgltf_load_buffers(&opts, data, path.empty() ? "" : path.u8string().c_str()) != cgltf_result_success) {
    cgltf_free(data);
    error = "The glTF's buffers (.bin) are missing or broken.";
    return false;
  }
  out.materials = static_cast<int>(data->materials_count);
  out.metric = true;
  int draco = 0;
  const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
  if (scene) {
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) gltfNode(scene->nodes[i], out, draco);
  } else {
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
      if (!data->nodes[i].parent) gltfNode(&data->nodes[i], out, draco);
    }
  }
  cgltf_free(data);
  if (out.meshes.empty() && draco) {
    error = "Draco-compressed glTF isn't supported yet.";
    return false;
  }
  return true;
}

// Area-weighted face normals spread to corners, kept apart across bends
// sharper than the angle (auto smooth).
std::vector<glm::vec3> smoothCornerNormals(const SubMesh& m, float angleDeg) {
  const size_t triCount = m.indices.size() / 3;
  std::vector<glm::vec3> face(triCount);
  for (size_t t = 0; t < triCount; ++t) {
    const glm::vec3& a = m.positions[m.indices[t * 3]];
    const glm::vec3& b = m.positions[m.indices[t * 3 + 1]];
    const glm::vec3& c = m.positions[m.indices[t * 3 + 2]];
    face[t] = glm::cross(b - a, c - a);  // length = 2 * area
  }
  // Faces round each vertex (CSR).
  std::vector<uint32_t> start(m.positions.size() + 1, 0), ring(m.indices.size());
  for (uint32_t v : m.indices) ++start[v + 1];
  for (size_t i = 1; i < start.size(); ++i) start[i] += start[i - 1];
  std::vector<uint32_t> fill(start.begin(), start.end() - 1);
  for (size_t i = 0; i < m.indices.size(); ++i) ring[fill[m.indices[i]]++] = static_cast<uint32_t>(i / 3);
  const float cosLimit = std::cos(glm::radians(angleDeg));
  std::vector<glm::vec3> out(m.indices.size());
  for (size_t i = 0; i < m.indices.size(); ++i) {
    const uint32_t v = m.indices[i];
    const glm::vec3& own = face[i / 3];
    const float ownLen = glm::length(own);
    glm::vec3 sum(0.0f);
    for (uint32_t k = start[v]; k < start[v + 1]; ++k) {
      const glm::vec3& f = face[ring[k]];
      const float fl = glm::length(f);
      if (ownLen <= 0.0f || fl <= 0.0f) continue;
      if (glm::dot(own, f) >= cosLimit * ownLen * fl) sum += f;
    }
    const float l = glm::length(sum);
    out[i] = l > 0.0f ? sum / l : (ownLen > 0.0f ? own / ownLen : glm::vec3(0, 1, 0));
  }
  return out;
}
}  // namespace

size_t Model::triangleCount() const {
  size_t n = 0;
  for (const SubMesh& m : meshes) n += m.indices.size() / 3;
  return n;
}

size_t Model::vertexCount() const {
  size_t n = 0;
  for (const SubMesh& m : meshes) n += m.positions.size();
  return n;
}

bool isModelExtension(const std::string& e) {
  return e == ".obj" || e == ".fbx" || e == ".gltf" || e == ".glb" || e == ".stl";
}

namespace {
// What every loader's result must pass.
bool checkModel(Model& out, std::string& error) {
  for (const SubMesh& m : out.meshes) {
    for (const glm::vec3& p : m.positions) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        error = "The file has broken vertex coordinates.";
        return false;
      }
    }
  }
  if (out.triangleCount() == 0) {
    error = "No triangles found in the file.";
    return false;
  }
  return true;
}
}  // namespace

const char* const kCancelled = "Cancelled.";

bool loadModel(const std::string& utf8Path, Model& out, std::string& error, const CancelFn& cancel) {
  out = Model();
  const std::filesystem::path path = std::filesystem::u8path(utf8Path);
  if (lowerExt(path) == ".fbx") {
    if (!std::filesystem::exists(path)) {
      error = "Couldn't read the file.";
      return false;
    }
    return loadFbxFile(utf8Path, out, error, cancel) && checkModel(out, error);
  }
  std::string data;
  if (!readFile(path, data)) {
    error = "Couldn't read the file.";
    return false;
  }
  return loadModelFromMemory(data, lowerExt(path), out, error, path, cancel);
}

bool loadModelFromMemory(const std::string& data, const std::string& ext, Model& out, std::string& error,
                         const std::filesystem::path& sourcePath, const CancelFn& cancel) {
  out = Model();
  if (ext == ".gltf" || ext == ".glb") {
    if (!loadGltf(data, sourcePath, out, error)) return false;
  } else if (ext == ".obj") {
    if (!loadObj(data, out, cancel)) {
      error = kCancelled;
      return false;
    }
  } else if (ext == ".stl") {
    loadStl(data, out);
  } else if (ext == ".fbx") {
    if (!loadFbx(data, out, error, cancel)) return false;
  } else {
    error = "Unsupported format.";
    return false;
  }
  return checkModel(out, error);
}

RenderMesh buildRenderMesh(const Model& model, float smoothAngleDeg) {
  RenderMesh rm;
  rm.min = glm::vec3(FLT_MAX);
  rm.max = glm::vec3(-FLT_MAX);
  size_t corners = 0;
  for (const SubMesh& m : model.meshes) corners += m.indices.size();
  rm.indices.reserve(corners);
  rm.vertices.reserve(corners * 6 / 2);
  std::vector<uint32_t> head, next;
  for (const SubMesh& m : model.meshes) {
    const std::vector<glm::vec3> computed =
        m.cornerNormals.size() == m.indices.size() ? std::vector<glm::vec3>() : smoothCornerNormals(m, smoothAngleDeg);
    const std::vector<glm::vec3>& normals = computed.empty() ? m.cornerNormals : computed;
    RenderMesh::Part part;
    part.name = m.name;
    part.firstIndex = static_cast<uint32_t>(rm.indices.size());
    part.min = glm::vec3(FLT_MAX);
    part.max = glm::vec3(-FLT_MAX);
    // One output vertex per (position, normal): corners of a position are
    // chained from head[position]; equal normals share the vertex.
    head.assign(m.positions.size(), UINT32_MAX);
    next.clear();
    const uint32_t base = static_cast<uint32_t>(rm.vertices.size() / 6);
    for (size_t i = 0; i < m.indices.size(); ++i) {
      const uint32_t p = m.indices[i];
      const glm::vec3& n = normals[i];
      uint32_t found = UINT32_MAX;
      for (uint32_t v = head[p]; v != UINT32_MAX; v = next[v]) {
        const float* q = &rm.vertices[(base + v) * 6 + 3];
        if (q[0] == n.x && q[1] == n.y && q[2] == n.z) {
          found = v;
          break;
        }
      }
      if (found == UINT32_MAX) {
        found = static_cast<uint32_t>(next.size());
        next.push_back(head[p]);
        head[p] = found;
        const glm::vec3& pos = m.positions[p];
        rm.vertices.insert(rm.vertices.end(), {pos.x, pos.y, pos.z, n.x, n.y, n.z});
        part.min = glm::min(part.min, pos);
        part.max = glm::max(part.max, pos);
      }
      rm.indices.push_back(base + found);
    }
    part.indexCount = static_cast<uint32_t>(rm.indices.size()) - part.firstIndex;
    if (part.indexCount == 0) continue;
    rm.min = glm::min(rm.min, part.min);
    rm.max = glm::max(rm.max, part.max);
    rm.parts.push_back(std::move(part));
  }
  if (rm.parts.empty()) rm.min = rm.max = glm::vec3(0.0f);
  return rm;
}

ModelStats modelStats(const Model& model, const RenderMesh& mesh) {
  ModelStats s;
  s.triangles = static_cast<int64_t>(model.triangleCount());
  s.vertices = static_cast<int64_t>(model.vertexCount());
  s.parts = static_cast<int>(model.meshes.size());
  s.materials = model.materials;
  s.size = mesh.max - mesh.min;
  s.metric = model.metric;
  return s;
}
