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
uniform int uAOOn;
uniform sampler2D uAO;
uniform vec2 uViewport;
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
  if (uAOOn != 0) {
    float ao = pow(texture(uAO, gl_FragCoord.xy / uViewport).r, 1.4);
    l *= ao;
    spec *= ao;
  }
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
uniform int uLines;
uniform int uShadowOn;
uniform sampler2D uShadow;
uniform mat4 uLightViewProj;
uniform float uShadowTexel;
uniform float uShadowReach;
out vec4 oColor;
// How much of the sun this point of the floor sees (a soft 5x5 look-up).
float sunSeen(vec3 p) {
  vec4 lc = uLightViewProj * vec4(p, 1.0);
  vec3 s = lc.xyz / lc.w * 0.5 + 0.5;
  if (s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z >= 1.0) return 1.0;
  float lit = 0.0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      lit += s.z - uShadowTexel * 2.0 <= texture(uShadow, s.xy + vec2(x, y) * uShadowTexel * 1.5).r ? 1.0 : 0.0;
    }
  }
  return lit / 25.0;
}
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
  float lines = uLines != 0 ? a * fade : 0.0;
  // The model's shadow: the floor darkened, fading away from the model.
  float shade = 0.0;
  if (uShadowOn != 0) {
    shade = (1.0 - sunSeen(vW)) * 0.55 * (1.0 - smoothstep(uShadowReach * 0.5, uShadowReach, length(vW.xz - uCenter.xz)));
  }
  float total = lines + shade * (1.0 - lines);
  if (total <= 0.002) discard;
  oColor = vec4(vec3(0.85) * lines / total, total);
}
)GLSL";

// Render: one sun (with its shadow map) and the sky, in linear light with
// a filmic curve. The sun stays put in the world; turn the model to see it
// from the other side.
const char* kLitVs = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uView, uProj, uLightViewProj;
out vec3 vW;
out vec3 vN;
out vec4 vLightClip;
void main() {
  vW = aPos;
  vN = aNrm;
  vLightClip = uLightViewProj * vec4(aPos, 1.0);
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

const char* kLitFs = R"GLSL(#version 330 core
in vec3 vW;
in vec3 vN;
in vec4 vLightClip;
uniform vec3 uSun;
uniform vec3 uCamPos;
uniform sampler2D uShadow;
uniform float uShadowTexel;
uniform int uAOOn;
uniform sampler2D uAO;
uniform vec2 uViewport;
uniform float uTint;
out vec4 oColor;
float sunSeen(vec3 n, vec3 l) {
  vec3 p = vLightClip.xyz / vLightClip.w * 0.5 + 0.5;
  if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
  float bias = uShadowTexel * (1.5 + 3.0 * (1.0 - max(dot(n, l), 0.0)));
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) lit += p.z - bias <= texture(uShadow, p.xy + vec2(x, y) * uShadowTexel).r ? 1.0 : 0.0;
  }
  return lit / 9.0;
}
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
  vec3 N = normalize(vN);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos - vW);
  vec3 L = normalize(uSun);
  vec3 H = normalize(L + V);
  float ndl = max(dot(N, L), 0.0);
  float sun = ndl > 0.0 ? sunSeen(N, L) : 0.0;
  float ao = uAOOn != 0 ? pow(texture(uAO, gl_FragCoord.xy / uViewport).r, 1.6) : 1.0;
  vec3 albedo = vec3(0.45);
  vec3 sky = mix(vec3(0.05), vec3(0.30), N.y * 0.5 + 0.5);
  float spec = pow(max(dot(N, H), 0.0), 60.0) * 0.35 * ndl * sun;
  float rim = pow(1.0 - max(dot(N, V), 0.0), 5.0) * 0.10;
  vec3 c = albedo * (ndl * sun * 1.6 + sky * ao) + vec3(spec) + vec3(rim * ao);
  c = pow(aces(c), vec3(1.0 / 2.2));
  if (!gl_FrontFacing) c *= 0.6;
  oColor = vec4(mix(c, vec3(1.0), uTint), 1.0);
}
)GLSL";

// Ambient occlusion (as Expanse's): view normals and depth, then 16 samples
// around each pixel in its hemisphere, then a 4x4 blur.
const char* kGeomVs = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uView, uProj;
out vec3 vViewPos;
out vec3 vViewN;
void main() {
  vec4 vp = uView * vec4(aPos, 1.0);
  vViewPos = vp.xyz;
  vViewN = mat3(uView) * aNrm;
  gl_Position = uProj * vp;
}
)GLSL";

const char* kGeomFs = R"GLSL(#version 330 core
in vec3 vViewPos;
in vec3 vViewN;
out vec4 oColor;
void main() {
  vec3 n = normalize(vViewN);
  if (!gl_FrontFacing) n = -n;
  oColor = vec4(n, vViewPos.z);
}
)GLSL";

const char* kFullscreenVs = R"GLSL(#version 330 core
out vec2 vUv;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kSsaoFs = R"GLSL(#version 330 core
in vec2 vUv;
uniform sampler2D uGeom;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform float uRadius;
uniform vec3 uKernel[16];
out vec4 oColor;
vec3 viewPos(vec2 uv, float z) {
  vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  vec3 ray = p.xyz / p.w;
  return ray * (z / ray.z);
}
void main() {
  vec4 g = texture(uGeom, vUv);
  if (g.w == 0.0) {
    oColor = vec4(1.0);
    return;
  }
  vec3 P = viewPos(vUv, g.w);
  vec3 N = normalize(g.xyz);
  // A turn per pixel from a 4x4 pattern; the blur averages it out.
  vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
  float ang = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
  vec3 rnd = vec3(cos(ang), sin(ang), 0.37);
  vec3 T = normalize(rnd - N * dot(rnd, N));
  mat3 tbn = mat3(T, cross(N, T), N);
  float occ = 0.0;
  for (int i = 0; i < 16; ++i) {
    vec3 s = P + tbn * uKernel[i] * uRadius;
    vec4 c = uProj * vec4(s, 1.0);
    vec2 suv = c.xy / c.w * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
    float sz = texture(uGeom, suv).w;
    if (sz == 0.0) continue;
    float range = smoothstep(0.0, 1.0, uRadius / abs(P.z - sz));
    occ += (sz >= s.z + uRadius * 0.03 ? 1.0 : 0.0) * range;
  }
  oColor = vec4(1.0 - occ / 16.0);
}
)GLSL";

const char* kBlurFs = R"GLSL(#version 330 core
in vec2 vUv;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 oColor;
void main() {
  float sum = 0.0;
  for (int y = -2; y < 2; ++y) {
    for (int x = -2; x < 2; ++x) sum += texture(uSrc, vUv + (vec2(x, y) + 0.5) * uTexel).r;
  }
  oColor = vec4(sum / 16.0);
}
)GLSL";

constexpr int kShadowSize = 2048;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6;  // GL_DRAW_FRAMEBUFFER_BINDING

// Hemisphere of sample offsets (z up), denser near the middle.
const glm::vec3* ssaoKernel() {
  static glm::vec3 kernel[16];
  static bool made = false;
  if (!made) {
    uint32_t seed = 12345u;
    auto rnd = [&] {
      seed = seed * 1664525u + 1013904223u;
      return (seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < 16; ++i) {
      glm::vec3 v;
      do {
        v = glm::vec3(rnd() * 2.0f - 1.0f, rnd() * 2.0f - 1.0f, rnd());
      } while (glm::length(v) > 1.0f || glm::length(v) < 0.1f || v.z < 0.15f);
      const float t = static_cast<float>(i) / 16.0f;
      kernel[i] = glm::normalize(v) * glm::mix(0.15f, 1.0f, t * t);
    }
    made = true;
  }
  return kernel;
}

// From the upper front right (yaw 35, 50 degrees above the horizon), as Expanse's default sun.
glm::vec3 sunDirection() {
  const float yaw = glm::radians(35.0f), pitch = glm::radians(50.0f);
  return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
}

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

GLint uloc(GLuint prog, const char* name) { return glx::GetUniformLocation(prog, name); }

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
  static uint64_t serials = 0;
  out.serial = ++serials;
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
  lit_ = program(kLitVs, kLitFs);
  geom_ = program(kGeomVs, kGeomFs);
  ssao_ = program(kFullscreenVs, kSsaoFs);
  blur_ = program(kFullscreenVs, kBlurFs);
  glx::GenVertexArrays(1, &emptyVao_);
  // Samplers: 1 the shadow map, 2 the occlusion.
  for (GLuint prog : {solid_, lit_, grid_}) {
    glx::UseProgram(prog);
    glx::Uniform1i(uloc(prog, "uShadow"), 1);
    glx::Uniform1i(uloc(prog, "uAO"), 2);
  }
  glx::UseProgram(ssao_);
  glx::Uniform3fv(uloc(ssao_, "uKernel"), 16, glm::value_ptr(ssaoKernel()[0]));
  glx::UseProgram(0);
  return solid_ && flat_ && grid_ && lit_ && geom_ && ssao_ && blur_;
}

void Renderer::shutdown() {
  thumbTarget_.release();
  releasePass(aoGeom_);
  releasePass(aoRaw_);
  releasePass(aoBlur_);
  if (shadowFbo_) glx::DeleteFramebuffers(1, &shadowFbo_);
  if (shadowTex_) glDeleteTextures(1, &shadowTex_);
  shadowFbo_ = shadowTex_ = 0;
  for (GLuint* prog : {&solid_, &flat_, &grid_, &lit_, &geom_, &ssao_, &blur_}) {
    if (*prog) glx::DeleteProgram(*prog);
    *prog = 0;
  }
  if (emptyVao_) glx::DeleteVertexArrays(1, &emptyVao_);
  emptyVao_ = 0;
}

bool Renderer::ensurePass(Pass& t, int w, int h, GLenum format, bool depth) {
  if (t.fbo && t.w == w && t.h == h) return true;
  releasePass(t);
  t.w = w, t.h = h;
  const bool r8 = format == GL_R8;
  glGenTextures(1, &t.color);
  glBindTexture(GL_TEXTURE_2D, t.color);
  glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, r8 ? GL_RED : GL_RGBA, r8 ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, r8 ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, r8 ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glx::GenFramebuffers(1, &t.fbo);
  glx::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glx::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
  if (depth) {
    glx::GenRenderbuffers(1, &t.depth);
    glx::BindRenderbuffer(GL_RENDERBUFFER, t.depth);
    glx::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glx::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
  }
  const bool ok = glx::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (!ok) releasePass(t);
  return ok;
}

void Renderer::releasePass(Pass& t) {
  if (t.fbo) glx::DeleteFramebuffers(1, &t.fbo);
  if (t.color) glDeleteTextures(1, &t.color);
  if (t.depth) glx::DeleteRenderbuffers(1, &t.depth);
  t = Pass();
}

void Renderer::drawAo(const GpuMesh& m, const ViewParams& p, int w, int h) {
  if (!ensurePass(aoGeom_, w, h, GL_RGBA32F, true) || !ensurePass(aoRaw_, w, h, GL_R8, false) ||
      !ensurePass(aoBlur_, w, h, GL_R8, false)) {
    return;
  }
  glx::BindFramebuffer(GL_FRAMEBUFFER, aoGeom_.fbo);
  glViewport(0, 0, w, h);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glx::UseProgram(geom_);
  setMatrices(geom_, p.view, p.proj);
  glx::BindVertexArray(m.vao);
  drawParts(m, GL_TRIANGLES, p.soloPart, -1, -1);

  glDisable(GL_DEPTH_TEST);
  glx::BindVertexArray(emptyVao_);
  glx::BindFramebuffer(GL_FRAMEBUFFER, aoRaw_.fbo);
  glx::UseProgram(ssao_);
  glx::UniformMatrix4fv(uloc(ssao_, "uProj"), 1, GL_FALSE, glm::value_ptr(p.proj));
  const glm::mat4 invProj = glm::inverse(p.proj);
  glx::UniformMatrix4fv(uloc(ssao_, "uInvProj"), 1, GL_FALSE, glm::value_ptr(invProj));
  // A twentieth of the model: creases and contacts, not the whole shape.
  glx::Uniform1f(uloc(ssao_, "uRadius"), std::max(m.radius() * 0.05f, 1e-6f));
  glx::ActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, aoGeom_.color);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glx::BindFramebuffer(GL_FRAMEBUFFER, aoBlur_.fbo);
  glx::UseProgram(blur_);
  const glm::vec2 texel(1.0f / w, 1.0f / h);
  glx::Uniform2fv(uloc(blur_, "uTexel"), 1, glm::value_ptr(texel));
  glBindTexture(GL_TEXTURE_2D, aoRaw_.color);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::updateShadowMap(const GpuMesh& m, const ViewParams& p) {
  if (!shadowFbo_) {
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glx::GenFramebuffers(1, &shadowFbo_);
    glx::BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glx::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  }
  if (shadowSerial_ == m.serial && shadowSolo_ == p.soloPart && shadowFloor_ == p.floorY) return;
  shadowSerial_ = m.serial, shadowSolo_ = p.soloPart, shadowFloor_ = p.floorY;
  // An ortho box around the model, wide enough for its shadow on the floor.
  const glm::vec3 sun = sunDirection();
  const glm::vec3 c = m.center();
  const float r = std::max(m.radius(), 1e-6f) * 1.8f;
  lightViewProj_ = glm::ortho(-r, r, -r, r, 0.0f, 4.0f * r) * glm::lookAt(c + sun * (2.0f * r), c, glm::vec3(0, 1, 0));
  glx::BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
  glViewport(0, 0, kShadowSize, kShadowSize);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.5f, 3.0f);
  glx::UseProgram(flat_);
  setMatrices(flat_, glm::mat4(1.0f), lightViewProj_);
  glx::BindVertexArray(m.vao);
  drawParts(m, GL_TRIANGLES, p.soloPart, -1, -1);
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::drawGrid(const ViewParams& p, bool lines, bool shadow) {
  glx::UseProgram(grid_);
  setMatrices(grid_, p.view, p.proj);
  const glm::vec3 center(0.0f, p.floorY, 0.0f);
  const float extent = p.gridStep * 400.0f;
  glx::Uniform3fv(glx::GetUniformLocation(grid_, "uCenter"), 1, glm::value_ptr(center));
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uExtent"), extent);
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uStep"), p.gridStep);
  glx::Uniform1f(glx::GetUniformLocation(grid_, "uFade"), p.gridStep * 60.0f);
  glx::Uniform1i(uloc(grid_, "uLines"), lines ? 1 : 0);
  glx::Uniform1i(uloc(grid_, "uShadowOn"), shadow ? 1 : 0);
  if (shadow) {
    glx::UniformMatrix4fv(uloc(grid_, "uLightViewProj"), 1, GL_FALSE, glm::value_ptr(lightViewProj_));
    glx::Uniform1f(uloc(grid_, "uShadowTexel"), 1.0f / kShadowSize);
    glx::Uniform1f(uloc(grid_, "uShadowReach"), p.gridStep * 40.0f);
    glx::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glx::ActiveTexture(GL_TEXTURE0);
  }
  glEnable(GL_BLEND);
  glx::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glx::BindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void Renderer::draw(const GpuMesh& m, const ViewParams& p) {
  const bool render = p.shading == Shading::Render && m.valid();
  const bool aoOn = m.valid() && (render || (p.ao && (p.shading == Shading::Solid || p.shading == Shading::SolidWire)));
  GLint vp[4], fbo = 0;
  glGetIntegerv(GL_VIEWPORT, vp);
  if (render || aoOn) {
    // The passes draw elsewhere; then back to the caller's framebuffer.
    glGetIntegerv(kDrawFramebufferBinding, &fbo);
    if (render) updateShadowMap(m, p);
    if (aoOn) drawAo(m, p, vp[2], vp[3]);
    glx::BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
  }
  const glm::vec2 viewport(static_cast<float>(vp[2]), static_cast<float>(vp[3]));
  auto bindAo = [&](GLuint prog) {
    glx::Uniform1i(uloc(prog, "uAOOn"), aoOn && aoBlur_.color ? 1 : 0);
    glx::Uniform2fv(uloc(prog, "uViewport"), 1, glm::value_ptr(viewport));
    glx::ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, aoOn ? aoBlur_.color : 0);
    glx::ActiveTexture(GL_TEXTURE0);
  };
  glClearColor(p.background.r, p.background.g, p.background.b, p.clearAlpha ? 0.0f : 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  if (m.valid()) {
    glx::BindVertexArray(m.vao);
    const bool solid = p.shading == Shading::Solid || p.shading == Shading::SolidWire;
    if (render) {
      glx::UseProgram(lit_);
      setMatrices(lit_, p.view, p.proj);
      glx::UniformMatrix4fv(uloc(lit_, "uLightViewProj"), 1, GL_FALSE, glm::value_ptr(lightViewProj_));
      const glm::vec3 sun = sunDirection();
      const glm::vec3 eye = glm::vec3(glm::inverse(p.view)[3]);
      glx::Uniform3fv(uloc(lit_, "uSun"), 1, glm::value_ptr(sun));
      glx::Uniform3fv(uloc(lit_, "uCamPos"), 1, glm::value_ptr(eye));
      glx::Uniform1f(uloc(lit_, "uShadowTexel"), 1.0f / kShadowSize);
      bindAo(lit_);
      glx::ActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadowTex_);
      glx::ActiveTexture(GL_TEXTURE0);
      drawParts(m, GL_TRIANGLES, p.soloPart, uloc(lit_, "uTint"), p.highlightPart);
    } else if (solid) {
      glx::UseProgram(solid_);
      setMatrices(solid_, p.view, p.proj);
      bindAo(solid_);
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
    if (p.shading != Shading::Solid && !render) {
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
  if (p.grid || render) drawGrid(p, p.grid, render);
  glx::UseProgram(0);
  for (GLenum unit : {GL_TEXTURE1, GL_TEXTURE2}) {
    glx::ActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glx::ActiveTexture(GL_TEXTURE0);
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
