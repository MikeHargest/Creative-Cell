#include "nav_cube.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "camera.h"
#include "ui_style.h"

namespace {
struct Face {
  glm::vec3 n, u, v;
  const char* label;
};
// Y up; "Front" looks at the model from +Z.
const Face kFaces[6] = {
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, "RIGHT"}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, "LEFT"},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, "TOP"},   {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "BOTTOM"},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "FRONT"},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, "BACK"},
};
// Cell borders along a face axis: edge band, middle, edge band.
const float kCuts[4] = {-1.0f, -0.62f, 0.62f, 1.0f};

float cross2(const ImVec2& a, const ImVec2& b, const ImVec2& p) {
  return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

bool insideQuad(const ImVec2 q[4], const ImVec2& p) {
  bool pos = false, neg = false;
  for (int i = 0; i < 4; ++i) {
    float c = cross2(q[i], q[(i + 1) % 4], p);
    pos |= c > 0.0f;
    neg |= c < 0.0f;
  }
  return !(pos && neg);
}

ImU32 shade(float facing, float boost) {
  const int c = static_cast<int>(std::clamp(0.13f + 0.12f * facing + boost, 0.0f, 1.0f) * 255.0f);
  return IM_COL32(c, c, c, 255);
}
}  // namespace

bool navCube(const char* id, ImVec2 origin, float size, const glm::mat4& view, Camera& camera) {
  ImGui::SetCursorScreenPos(origin);
  ImGui::InvisibleButton(id, ImVec2(size, size));
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();

  // Orbit by dragging; a click that never moved is a snap.
  static bool dragged = false;
  if (ImGui::IsItemActivated()) dragged = false;
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
    dragged = true;
    camera.orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
  }

  const glm::mat3 rot(view);
  const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
  const float scale = size * 0.27f;
  auto project = [&](const glm::vec3& p) {
    const glm::vec3 v = rot * p;
    return ImVec2(center.x + v.x * scale, center.y - v.y * scale);
  };
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 mouse = ImGui::GetIO().MousePos;

  bool haveHover = false;
  glm::vec3 hoverDir(0.0f);
  if (hovered || active) {
    for (const Face& f : kFaces) {
      if ((rot * f.n).z <= 0.01f) continue;
      for (int i = 0; i < 3 && !haveHover; ++i) {
        for (int j = 0; j < 3 && !haveHover; ++j) {
          ImVec2 q[4] = {project(f.n + f.u * kCuts[i] + f.v * kCuts[j]), project(f.n + f.u * kCuts[i + 1] + f.v * kCuts[j]),
                         project(f.n + f.u * kCuts[i + 1] + f.v * kCuts[j + 1]),
                         project(f.n + f.u * kCuts[i] + f.v * kCuts[j + 1])};
          if (insideQuad(q, mouse)) {
            haveHover = true;
            hoverDir = f.n + f.u * static_cast<float>(i - 1) + f.v * static_cast<float>(j - 1);
          }
        }
      }
    }
  }

  ImFont* font = ui::fonts.sans;
  const float labelSize = std::max(8.0f, size * 0.095f);
  for (const Face& f : kFaces) {
    const float facing = (rot * f.n).z;
    if (facing <= 0.01f) continue;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        const glm::vec3 dir = f.n + f.u * static_cast<float>(i - 1) + f.v * static_cast<float>(j - 1);
        const bool lit = haveHover && dir == hoverDir && !dragged;
        ImVec2 q[4] = {project(f.n + f.u * kCuts[i] + f.v * kCuts[j]), project(f.n + f.u * kCuts[i + 1] + f.v * kCuts[j]),
                       project(f.n + f.u * kCuts[i + 1] + f.v * kCuts[j + 1]),
                       project(f.n + f.u * kCuts[i] + f.v * kCuts[j + 1])};
        dl->AddQuadFilled(q[0], q[1], q[2], q[3], lit ? IM_COL32(235, 235, 235, 255) : shade(facing, (i == 1 && j == 1) ? 0.02f : 0.0f));
      }
    }
    ImVec2 outline[4] = {project(f.n - f.u - f.v), project(f.n + f.u - f.v), project(f.n + f.u + f.v),
                         project(f.n - f.u + f.v)};
    dl->AddPolyline(outline, 4, IM_COL32(70, 70, 70, 255), 1.0f, ImDrawFlags_Closed);
    if (facing > 0.3f) {
      const ImVec2 c = project(f.n);
      const ImVec2 ts = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, f.label);
      const int a = static_cast<int>(std::min(1.0f, (facing - 0.3f) * 3.0f) * 200.0f);
      const bool litFace = haveHover && hoverDir == f.n && !dragged;
      dl->AddText(font, labelSize, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                  litFace ? IM_COL32(20, 20, 20, 255) : IM_COL32(220, 220, 220, a), f.label);
    }
  }

  if (ImGui::IsItemDeactivated() && !dragged && haveHover) camera.lookAlongAxis(hoverDir);
  return hovered || active;
}
