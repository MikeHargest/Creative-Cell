#include "renderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace {
// Clay: a matte light grey, lit by a key light from the upper left, a soft
// fill and a rim — reads the shape without any color.
const char* kSolidVs = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uView, uProj;
out vec3 vN;
void main() {
  vN = aNrm;
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

const char* kSolidFs = R"GLSL(#version 330 core
in vec3 vN;
uniform mat4 uView;
uniform vec3 uBase;
uniform float uTint;
out vec4 oColor;
void main() {
  vec3 n = normalize(vN);
  if (!gl_FrontFacing) n = -n;
  vec3 nv = normalize(mat3(uView) * n);
  vec3 key = normalize(vec3(-0.45, 0.65, 0.62));
  vec3 fill = normalize(vec3(0.70, -0.15, 0.50));
  float wrap = max((dot(nv, key) + 0.30) / 1.30, 0.0);
  float f = max(dot(nv, fill), 0.0);
  float hemi = 0.5 + 0.5 * n.y;
  float rim = pow(1.0 - max(nv.z, 0.0), 3.0);
  vec3 h = normalize(key + vec3(0.0, 0.0, 1.0));
  float spec = pow(max(dot(nv, h), 0.0), 48.0) * 0.10;
  float l = 0.16 + 0.12 * hemi + 0.58 * wrap + 0.12 * f + 0.16 * rim;
  vec3 c = uBase * l + spec;
  if (!gl_FrontFacing) c *= 0.55;
  c = mix(c, vec3(1.0), uTint);
  oColor = vec4(c, 1.0);
}
)GLSL";

const char* kFlatVs = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uView, uProj;
void main() { gl_Position = uProj * uView * vec4(aPos, 1.0); }
)GLSL";

const char* kFlatFs = R"GLSL(#version 330 core
uniform vec4 uColor;
out vec4 oColor;
void main() { oColor = uColor; }
)GLSL";

// The floor: one big quad, lines worked out per pixel (anti-aliased by
// their screen width), every tenth stronger, fading out with distance.
const char* kGridVs = R"GLSL(#version 330 core
uniform mat4 uView, uProj;
uniform vec3 uCenter;
uniform float uExtent;
out vec3 vW;
void main() {
  vec2 c = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
  vW = vec3(uCenter.x + c.x * uExtent, uCenter.y, uCenter.z + c.y * uExtent);
  gl_Position = uProj * uView * vec4(vW, 1.0);
}
)GLSL";

const char* kGridFs = R"GLSL(#version 330 core
in vec3 vW;
uniform float uStep;
uniform vec3 uCenter;
uniform float uFade;
out vec4 oColor;
float lines(vec2 p) {
  vec2 d = fwidth(p);
  vec2 g = abs(fract(p - 0.5) - 0.5) / max(d, vec2(1e-6));
  return 1.0 - min(min(g.x, g.y), 1.0);
}
void main() {
  float minor = lines(vW.xz / uStep);
  float major = lines(vW.xz / (uStep * 10.0));
  vec2 ad = abs(vW.xz) / max(fwidth(vW.xz), vec2(1e-6));
  float axis = 1.0 - min(min(ad.x, ad.y), 1.0);
  float a = max(max(minor * 0.10, major * 0.20), axis * 0.38);
  float fade = 1.0 - smoothstep(uFade * 0.35, uFade, length(vW.xz - uCenter.xz));
  oColor = vec4(vec3(0.85), a * fade);
}
)GLSL";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glx::CreateShader(type);
  glx::ShaderSource(s, 1, &src, nullptr);
  glx::CompileShader(s);
  GLint ok = 0;
  glx::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glx::GetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader: %s\n", log);
  }
  return s;
}

GLuint program(const char* vs, const char* fs) {
  GLuint p = glx::CreateProgram();
  GLuint a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
  glx::AttachShader(p, a);
  glx::AttachShader(p, b);
  glx::LinkProgram(p);
  glx::DeleteShader(a);
  glx::DeleteShader(b);
  GLint ok = 0;
  glx::GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glx::GetProgramInfoLog(p, sizeof log, nullptr, log);
    std::fprintf(stderr, "program: %s\n", log);
    glx::DeleteProgram(p);
    return 0;
  }
  return p;
}

void setMatrices(GLuint prog, const glm::mat4& view, const glm::mat4& proj) {
  glx::UniformMatrix4fv(glx::GetUniformLocation(prog, "uView"), 1, GL_FALSE, glm::value_ptr(view));
  glx::UniformMatrix4fv(glx::GetUniformLocation(prog, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
}

void drawParts(const GpuMesh& m, GLenum mode, int solo, GLint tintLoc, int highlight) {
  for (size_t i = 0; i < m.parts.size(); ++i) {
    if (solo >= 0 && static_cast<int>(i) != solo) continue;
    if (tintLoc >= 0) glx::Uniform1f(tintLoc, static_cast<int>(i) == highlight ? 0.22f : 0.0f);
    const RenderMesh::Part& p = m.parts[i];
    glDrawElements(mode, static_cast<GLsizei>(p.indexCount), GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(static_cast<uintptr_t>(p.firstIndex) * sizeof(uint32_t)));
  }
}
}  // namespace

void GpuMesh::release() {
  if (vao) glx::DeleteVertexArrays(1, &vao);
  if (vbo) glx::DeleteBuffers(1, &vbo);
  if (ebo) glx::DeleteBuffers(1, &ebo);
  vao = vbo = ebo = 0;
  parts.clear();
  vertexCount = indexCount = 0;
}

void uploadMesh(const RenderMesh& mesh, GpuMesh& out) {
  out.release();
  if (mesh.empty()) return;
  glx::GenVertexArrays(1, &out.vao);
  glx::GenBuffers(1, &out.vbo);
  glx::GenBuffers(1, &out.ebo);
  glx::BindVertexArray(out.vao);
  glx::BindBuffer(GL_ARRAY_BUFFER, out.vbo);
  glx::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)), mesh.vertices.data(),
                  GL_STATIC_DRAW);
  glx::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
  glx::BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                  mesh.indices.data(), GL_STATIC_DRAW);
  glx::EnableVertexAttribArray(0);
  glx::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glx::EnableVertexAttribArray(1);
  glx::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
  glx::BindVertexArray(0);
  out.parts = mesh.parts;
  out.min = mesh.min;
  out.max = mesh.max;
  out.vertexCount = mesh.vertices.size() / 6;
  out.indexCount = mesh.indices.size();
}

bool RenderTarget::resize(int w, int h, int samples) {
  w = std::max(w, 1);
  h = std::max(h, 1);
  if (w == w_ && h == h_ && samples == samples_ && fbo_) return true;
  release();
  w_ = w, h_ = h, samples_ = samples;
  GLint maxSamples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  const int s = std::min(samples, static_cast<int>(maxSamples));
  glx::GenFramebuffers(1, &msFbo_);
  glx::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
  glx::GenRenderbuffers(1, &msColor_);
  glx::BindRenderbuffer(GL_RENDERBUFFER, msColor_);
  glx::RenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_RGBA8, w, h);
  glx::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
  glx::GenRenderbuffers(1, &msDepth_);
  glx::BindRenderbuffer(GL_RENDERBUFFER, msDepth_);
  glx::RenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_DEPTH_COMPONENT24, w, h);
  glx::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
  const bool msOk = glx::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

  glGenTextures(1, &colorTex_);
  glBindTexture(GL_TEXTURE_2D, colorTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glx::GenFramebuffers(1, &fbo_);
  glx::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glx::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
  const bool ok = glx::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glx::BindFramebuffer(GL_FRAMEBUFFER, 0);
  return msOk && ok;
}

void RenderTarget::bind() {
  glx::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
  glViewport(0, 0, w_, h_);
}

void RenderTarget::resolve() {
  glx::BindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
  glx::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
  glx::BlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glx::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::read(std::vector<uint8_t>& rgba) {
  rgba.resize(static_cast<size_t>(w_) * h_ * 4);
  glx::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glx::BindFramebuffer(GL_FRAMEBUFFER, 0);
  const size_t row = static_cast<size_t>(w_) * 4;
  std::vector<uint8_t> tmp(row);
  for (int y = 0; y < h_ / 2; ++y) {
    uint8_t* a = rgba.data() + y * row;
    uint8_t* b = rgba.data() + (h_ - 1 - y) * row;
    std::copy(a, a + row, tmp.data());
    std::copy(b, b + row, a);
    std::copy(tmp.begin(), tmp.end(), b);
  }
}

void RenderTarget::release() {
  if (msFbo_) glx::DeleteFramebuffers(1, &msFbo_);
  if (fbo_) glx::DeleteFramebuffers(1, &fbo_);
  if (msColor_) glx::DeleteRenderbuffers(1, &msColor_);
  if (msDepth_) glx::DeleteRenderbuffers(1, &msDepth_);
  if (colorTex_) glDeleteTextures(1, &colorTex_);
  msFbo_ = fbo_ = msColor_ = msDepth_ = colorTex_ = 0;
  w_ = h_ = samples_ = 0;
}

bool Renderer::init() {
  solid_ = program(kSolidVs, kSolidFs);
  flat_ = program(kFlatVs, kFlatFs);
  grid_ = program(kGridVs, kGridFs);
  glx::GenVertexArrays(1, &emptyVao_);
  return solid_ && flat_ && grid_;
}

void Renderer::shutdown() {
  thumbTarget_.release();
  if (solid_) glx::DeleteProgram(solid_);
  if (flat_) glx::DeleteProgram(flat_);
  if (grid_) glx::DeleteProgram(grid_);
  if (emptyVao_) glx::DeleteVertexArrays(1, &emptyVao_);
  solid_ = flat_ = grid_ = emptyVao_ = 0;
}

void Renderer::drawGrid(const ViewParams& p) {
  glx::UseProgram(grid_);
  setMatrices(grid_, p.view, p.proj);
  const glm::vec3 center(0.0f, p.floorY, 0.0f);
  const float extent = p.gridStep * 400.0f;
  glx::Uniform3fv(glx::GetUniformLocation(grid_, "uCenter"), 1, glm::value_ptr(center));
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uExtent"), extent);
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uStep"), p.gridStep);
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uFade"), p.gridStep * 60.0f);
  glEnable(GL_BLEND);
  glx::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glx::BindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void Renderer::draw(const GpuMesh& m, const ViewParams& p) {
  glClearColor(p.background.r, p.background.g, p.background.b, p.clearAlpha ? 0.0f : 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  if (m.valid()) {
    glx::BindVertexArray(m.vao);
    const bool solid = p.shading == Shading::Solid || p.shading == Shading::SolidWire;
    if (solid) {
      glx::UseProgram(solid_);
      setMatrices(solid_, p.view, p.proj);
      const glm::vec3 base(0.78f);
      glx::Uniform3fv(glx::GetUniformLocation(solid_, "uBase"), 1, glm::value_ptr(base));
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
      drawParts(m, GL_TRIANGLES, p.soloPart, glx::GetUniformLocation(solid_, "uTint"), p.highlightPart);
      glDisable(GL_POLYGON_OFFSET_FILL);
    } else {
      // Hidden lines / points: the surface into depth only.
      glx::UseProgram(flat_);
      setMatrices(flat_, p.view, p.proj);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
      drawParts(m, GL_TRIANGLES, p.soloPart, -1, -1);
      glDisable(GL_POLYGON_OFFSET_FILL);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    if (p.shading != Shading::Solid) {
      glx::UseProgram(flat_);
      setMatrices(flat_, p.view, p.proj);
      const GLint colorLoc = glx::GetUniformLocation(flat_, "uColor");
      glEnable(GL_BLEND);
      glx::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
      const glm::vec4 color = p.shading == Shading::SolidWire ? glm::vec4(0.05f, 0.05f, 0.05f, 0.55f)
                              : p.shading == Shading::Wire    ? glm::vec4(0.86f, 0.86f, 0.86f, 0.85f)
                                                              : glm::vec4(0.90f, 0.90f, 0.90f, 0.90f);
      glx::Uniform4fv(colorLoc, 1, glm::value_ptr(color));
      if (p.shading == Shading::Points) {
        glPointSize(2.0f);
        drawParts(m, GL_POINTS, p.soloPart, -1, -1);
      } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        drawParts(m, GL_TRIANGLES, p.soloPart, -1, -1);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      }
      if (p.highlightPart >= 0 && p.shading != Shading::SolidWire) {
        // The hovered part stands out in the line views too.
        const glm::vec4 hi(1.0f, 1.0f, 1.0f, 1.0f);
        glx::Uniform4fv(colorLoc, 1, glm::value_ptr(hi));
        glPolygonMode(GL_FRONT_AND_BACK, p.shading == Shading::Points ? GL_POINT : GL_LINE);
        drawParts(m, GL_TRIANGLES, p.highlightPart, -1, -1);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      }
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
    }
    glx::BindVertexArray(0);
  }
  if (p.grid) drawGrid(p);
  glx::UseProgram(0);
  glDisable(GL_DEPTH_TEST);
}

bool Renderer::thumbnail(const GpuMesh& m, int size, std::vector<uint8_t>& rgba) {
  if (!m.valid() || !thumbTarget_.resize(size, size, 8)) return false;
  // From the front right, a little above; orthographic, fit to the
  // model's outline on screen.
  const float yaw = glm::radians(38.0f), pitch = glm::radians(24.0f);
  const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
  const glm::vec3 c = m.center();
  const float r = std::max(m.radius(), 1e-6f);
  const glm::mat4 view = glm::lookAt(c + dir * r * 4.0f, c, glm::vec3(0, 1, 0));
  glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner((i & 1) ? m.max.x : m.min.x, (i & 2) ? m.max.y : m.min.y, (i & 4) ? m.max.z : m.min.z);
    const glm::vec3 v = glm::vec3(view * glm::vec4(corner, 1.0f));
    lo = glm::min(lo, v);
    hi = glm::max(hi, v);
  }
  const glm::vec2 mid((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
  const float half = std::max(hi.x - lo.x, hi.y - lo.y) * 0.5f * 1.10f + 1e-6f;
  const glm::mat4 proj = glm::ortho(mid.x - half, mid.x + half, mid.y - half, mid.y + half, -hi.z - r, -lo.z + r);
  ViewParams p;
  p.view = view;
  p.proj = proj;
  p.grid = false;
  p.background = glm::vec3(0.0f);
  p.clearAlpha = true;
  GLint prevViewport[4];
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  thumbTarget_.bind();
  draw(m, p);
  thumbTarget_.resolve();
  thumbTarget_.read(rgba);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
  return true;
}

float niceGridStep(float radius) {
  const float target = std::max(radius, 1e-6f) / 4.0f;
  const float p = std::pow(10.0f, std::floor(std::log10(target)));
  const float m = target / p;
  return p * (m < 1.5f ? 1.0f : m < 3.5f ? 2.0f : m < 7.5f ? 5.0f : 10.0f);
}
