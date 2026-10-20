#include "soft_render.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {
// renderer.cpp's clay shader, per vertex (view-space normal `nv`, world
// normal's y for the sky light).
glm::vec3 clay(const glm::vec3& nv, float worldY, bool back) {
  const glm::vec3 key = glm::normalize(glm::vec3(-0.45f, 0.65f, 0.62f));
  const glm::vec3 fill = glm::normalize(glm::vec3(0.70f, -0.15f, 0.50f));
  const float wrap = std::max((glm::dot(nv, key) + 0.30f) / 1.30f, 0.0f);
  const float f = std::max(glm::dot(nv, fill), 0.0f);
  const float hemi = 0.5f + 0.5f * worldY;
  const float rim = std::pow(1.0f - std::max(nv.z, 0.0f), 3.0f);
  const glm::vec3 h = glm::normalize(key + glm::vec3(0, 0, 1));
  const float spec = std::pow(std::max(glm::dot(nv, h), 0.0f), 48.0f) * 0.10f;
  const float l = 0.16f + 0.12f * hemi + 0.58f * wrap + 0.12f * f + 0.16f * rim;
  glm::vec3 c = glm::vec3(0.78f) * l + spec;
  if (back) c *= 0.55f;
  return glm::clamp(c, 0.0f, 1.0f);
}
}  // namespace

bool renderThumbnailCpu(const RenderMesh& m, int size, std::vector<uint8_t>& rgba) {
  if (m.empty() || size <= 0) return false;
  const int ss = 2, W = size * ss;
  // The view: as Renderer::thumbnail.
  const float yaw = glm::radians(38.0f), pitch = glm::radians(24.0f);
  const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
  const glm::vec3 c = (m.min + m.max) * 0.5f;
  const float r = std::max(glm::length(m.max - m.min) * 0.5f, 1e-6f);
  const glm::mat4 view = glm::lookAt(c + dir * r * 4.0f, c, glm::vec3(0, 1, 0));
  const glm::mat3 rot(view);
  glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner((i & 1) ? m.max.x : m.min.x, (i & 2) ? m.max.y : m.min.y, (i & 4) ? m.max.z : m.min.z);
    const glm::vec3 v = glm::vec3(view * glm::vec4(corner, 1.0f));
    lo = glm::min(lo, v);
    hi = glm::max(hi, v);
  }
  const glm::vec2 mid((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
  const float half = std::max(hi.x - lo.x, hi.y - lo.y) * 0.5f * 1.10f + 1e-6f;
  const float scale = W / (2.0f * half);

  // Vertices to the screen (x right, y down) with their depth and light.
  const size_t vcount = m.vertices.size() / 6;
  std::vector<glm::vec3> screen(vcount);
  std::vector<glm::vec3> nview(vcount);
  std::vector<float> worldY(vcount);
  for (size_t i = 0; i < vcount; ++i) {
    const float* v = &m.vertices[i * 6];
    const glm::vec3 p = glm::vec3(view * glm::vec4(v[0], v[1], v[2], 1.0f));
    screen[i] = glm::vec3((p.x - mid.x) * scale + W * 0.5f, W * 0.5f - (p.y - mid.y) * scale, p.z);
    const glm::vec3 n(v[3], v[4], v[5]);
    nview[i] = glm::normalize(rot * n);
    worldY[i] = n.y;
  }

  std::vector<float> depth(static_cast<size_t>(W) * W, -FLT_MAX);
  std::vector<glm::vec3> color(static_cast<size_t>(W) * W, glm::vec3(0.0f));
  for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
    const uint32_t ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
    const glm::vec3 &a = screen[ia], &b = screen[ib], &cc = screen[ic];
    const float area = (b.x - a.x) * (cc.y - a.y) - (b.y - a.y) * (cc.x - a.x);
    if (area == 0.0f) continue;
    // y points down: a front face (counter-clockwise in GL) has area < 0.
    const bool back = area > 0.0f;
    auto shade = [&](uint32_t i) { return clay(back ? -nview[i] : nview[i], back ? -worldY[i] : worldY[i], back); };
    const glm::vec3 ca = shade(ia), cb = shade(ib), cco = shade(ic);
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, cc.x}))));
    const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, cc.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, cc.y}))));
    const int y1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, cc.y}))));
    const float inv = 1.0f / area;
    for (int y = y0; y <= y1; ++y) {
      const float py = y + 0.5f;
      for (int x = x0; x <= x1; ++x) {
        const float px = x + 0.5f;
        // Barycentrics (all >= 0 inside, either winding).
        const float w0 = ((b.x - px) * (cc.y - py) - (b.y - py) * (cc.x - px)) * inv;
        const float w1 = ((cc.x - px) * (a.y - py) - (cc.y - py) * (a.x - px)) * inv;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
        const float z = w0 * a.z + w1 * b.z + w2 * cc.z;
        const size_t k = static_cast<size_t>(y) * W + x;
        if (z <= depth[k]) continue;
        depth[k] = z;
        color[k] = ca * w0 + cb * w1 + cco * w2;
      }
    }
  }

  // Down to `size`, coverage as alpha.
  rgba.assign(static_cast<size_t>(size) * size * 4, 0);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      glm::vec3 sum(0.0f);
      int covered = 0;
      for (int sy = 0; sy < ss; ++sy) {
        for (int sx = 0; sx < ss; ++sx) {
          const size_t k = static_cast<size_t>(y * ss + sy) * W + (x * ss + sx);
          if (depth[k] == -FLT_MAX) continue;
          sum += color[k];
          ++covered;
        }
      }
      if (!covered) continue;
      const glm::vec3 col = sum / static_cast<float>(covered);
      uint8_t* o = &rgba[(static_cast<size_t>(y) * size + x) * 4];
      o[0] = static_cast<uint8_t>(col.r * 255.0f + 0.5f);
      o[1] = static_cast<uint8_t>(col.g * 255.0f + 0.5f);
      o[2] = static_cast<uint8_t>(col.b * 255.0f + 0.5f);
      o[3] = static_cast<uint8_t>(covered * 255 / (ss * ss));
    }
  }
  return true;
}
