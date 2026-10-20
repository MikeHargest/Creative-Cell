#pragma once

#include "gl_loader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "model_loader.h"

// A RenderMesh on the GPU.
struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0;
  std::vector<RenderMesh::Part> parts;
  glm::vec3 min{0.0f}, max{0.0f};
  size_t vertexCount = 0, indexCount = 0;
  bool valid() const { return vao != 0; }
  glm::vec3 center() const { return (min + max) * 0.5f; }
  float radius() const { return glm::length(max - min) * 0.5f; }
  void release();
};
void uploadMesh(const RenderMesh& mesh, GpuMesh& out);

// An offscreen picture: drawn multisampled, resolved into a texture that
// ImGui shows (or that's read back for a thumbnail).
class RenderTarget {
 public:
  ~RenderTarget() { release(); }
  // (Re)made at this size; a no-op when it already is.
  bool resize(int w, int h, int samples);
  void bind();     // draw into it
  void resolve();  // multisampled -> texture
  GLuint texture() const { return colorTex_; }
  int width() const { return w_; }
  int height() const { return h_; }
  // The resolved picture, rows top to bottom, RGBA.
  void read(std::vector<uint8_t>& rgba);
  void release();

 private:
  GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
  GLuint fbo_ = 0, colorTex_ = 0;
  int w_ = 0, h_ = 0, samples_ = 0;
};

enum class Shading { Solid, SolidWire, Wire, Points };

struct ViewParams {
  glm::mat4 view{1.0f}, proj{1.0f};
  Shading shading = Shading::Solid;
  bool grid = true;
  float floorY = 0.0f;   // the grid's height (under the model)
  float gridStep = 1.0f;  // a cell
  int highlightPart = -1;  // drawn lighter
  int soloPart = -1;       // only this one drawn
  glm::vec3 background{0.06f};
  bool clearAlpha = false;  // transparent background (thumbnails)
};

class Renderer {
 public:
  bool init();
  void shutdown();
  // Draws into the bound framebuffer (viewport already set).
  void draw(const GpuMesh& mesh, const ViewParams& p);
  // The model fit into a square picture from the 3/4 view, background
  // transparent. Rows top to bottom.
  bool thumbnail(const GpuMesh& mesh, int size, std::vector<uint8_t>& rgba);

 private:
  void drawGrid(const ViewParams& p);

  GLuint solid_ = 0, flat_ = 0, grid_ = 0;
  GLuint gridVao_ = 0, gridVbo_ = 0;
  GLuint emptyVao_ = 0;
  GLsizei gridVerts_ = 0;
  RenderTarget thumbTarget_;
};

// A grid step that suits a model of this size: 1, 2 or 5 x a power of ten.
float niceGridStep(float radius);
