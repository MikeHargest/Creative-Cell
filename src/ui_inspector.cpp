// The right panel: the focused model in 3D, and what's known about it.
#include "app.h"  // (first: windows.h before GLFW)

#include <imgui_internal.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

#include "nav_cube.h"
#include "util.h"

using namespace ui;

namespace {
// A label over a value, in a dark card (the inspector's numbers).
void statCard(const char* label, const std::string& value, float width) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = px(52);
  ImGui::Dummy(ImVec2(width, h));
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), col::bg2, px(6));
  dl->AddText(fonts.sans, px(11.5f), ImVec2(p.x + px(10), p.y + px(8)), col::text3, label);
  textClipped(dl, fonts.mono, px(14), ImVec2(p.x + px(10), p.y + px(26)), col::text, value.c_str(), width - px(20));
}

void dimText(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}
}  // namespace

void App::drawInspector(ImVec2 pos, ImVec2 size) {
  beginPanel("##inspector", pos, size, col::bg1);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddLine(pos, ImVec2(pos.x, pos.y + size.y), col::line);
  const float minViewer = px(180), minInfo = px(160);
  const float viewerH =
      infoHidden_ ? size.y : std::clamp(size.y * viewerFrac_, minViewer, std::max(minViewer, size.y - minInfo));
  drawViewer(ImVec2(pos.x + 1, pos.y), ImVec2(size.x - 1, viewerH));
  // Fold / unfold the info: bottom middle of the view.
  {
    const ImVec2 bs(px(40), px(20));
    const ImVec2 b0(pos.x + (size.x - bs.x) * 0.5f, pos.y + viewerH - bs.y - px(8));
    dl->AddRectFilled(b0, ImVec2(b0.x + bs.x, b0.y + bs.y), IM_COL32(16, 16, 16, 220), px(6));
    ImGui::SetCursorScreenPos(b0);
    if (ImGui::InvisibleButton("##infoToggle", bs)) {
      infoHidden_ = !infoHidden_;
      uiStateDirtyAt_ = ImGui::GetTime();
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) dl->AddRectFilled(b0, ImVec2(b0.x + bs.x, b0.y + bs.y), col::bg3, px(6));
    drawIcon(dl, infoHidden_ ? Icon::ChevronUp : Icon::ChevronDown, ImVec2(b0.x + bs.x * 0.5f, b0.y + bs.y * 0.5f),
             px(11), hov ? col::text : col::text2);
    if (hov) tooltip(infoHidden_ ? "Show the details" : "Hide the details");
  }
  if (infoHidden_) {
    endPanel();
    return;
  }

  // Info below, scrolling.
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, pos.y + viewerH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
  ImGui::BeginChild("##info", ImVec2(size.x - 1, size.y - viewerH), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  const std::vector<int64_t> sel = selectedIds();
  const Asset* a = lib_.find(focused_);
  if (sel.size() > 1) {
    drawMultiInfo(sel);
  } else if (a && isSelected(a->id)) {
    drawAssetInfo(*a);
  } else {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const char* t = "Select a model to see it here";
    const ImVec2 ts = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, t);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(fonts.sans, textSize(),
                                        ImVec2(p.x + (avail.x - ts.x) * 0.5f, p.y + px(30)), col::text3, t);
  }
  ImGui::EndChild();
  endPanel();

  // Between the view and the info.
  float frac = viewerFrac_ * size.y;
  if (splitter("##split_viewer", ImVec2(pos.x + 1, pos.y + viewerH - ui::px(2)), ImVec2(size.x - 1, ui::px(5)), false,
               frac, 1.0f, minViewer, size.y - minInfo)) {
    viewerFrac_ = std::clamp(frac / size.y, 0.15f, 0.9f);
    uiStateDirtyAt_ = ImGui::GetTime();
  }
}

void App::drawViewer(ImVec2 pos, ImVec2 size) {
  const ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(pos.x + size.x, pos.y + size.y);
  Viewer& v = viewer_;
  v.camera.update(io.DeltaTime);
  if (v.turntable && v.mesh.valid()) v.camera.orbit(-io.DeltaTime * 60.0f, 0.0f);

  // Input over the whole view (the overlays are drawn later and win).
  ImGui::SetCursorScreenPos(pos);
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##view", size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (active) {
    const ImVec2 d = io.MouseDelta;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyShift) {
      v.camera.orbit(d.x, d.y);
      v.turntable = false;
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
               (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyShift)) {
      v.camera.pan(d.x, d.y);
    }
  }
  if (hovered && io.MouseWheel != 0.0f) v.camera.dolly(-io.MouseWheel * 0.15f);
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && v.mesh.valid()) {
    frameViewer();
  }

  // The picture.
  const float fbScale = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  const int w = std::max(1, static_cast<int>(size.x * fbScale)), h = std::max(1, static_cast<int>(size.y * fbScale));
  v.aspect = static_cast<float>(w) / h;
  // Drawn again only when something it shows changed: a still view of a
  // heavy model costs nothing.
  const glm::mat4 viewM = v.camera.viewMatrix();
  std::vector<float> sig(glm::value_ptr(viewM), glm::value_ptr(viewM) + 16);
  for (float f : {static_cast<float>(w), static_cast<float>(h), static_cast<float>(v.shading), v.grid ? 1.0f : 0.0f,
                  v.ao ? 1.0f : 0.0f, static_cast<float>(v.hoverPart), static_cast<float>(v.soloPart), v.floorY,
                  v.gridStep, static_cast<float>(v.mesh.serial)}) {
    sig.push_back(f);
  }
  const bool fresh = sig == v.drawnSig && v.target.texture() && v.target.width() == w && v.target.height() == h;
  if (fresh) {
    dl->AddImage(static_cast<ImTextureID>(v.target.texture()), pos, end, ImVec2(0, 1), ImVec2(1, 0));
  } else if (v.target.resize(w, h, 8)) {
    v.drawnSig = std::move(sig);
    const glm::vec3 eye = v.camera.position();
    const float dist = glm::length(eye - v.camera.target());
    const float r = v.mesh.valid() ? std::max(v.mesh.radius(), 1e-4f) : 1.0f;
    ViewParams p;
    p.view = v.camera.viewMatrix();
    const float nearZ = std::max(dist * 0.002f, r * 0.0005f);
    const float farZ = dist + r * 40.0f + v.gridStep * 400.0f;
    p.proj = glm::perspective(glm::radians(35.0f), static_cast<float>(w) / h, nearZ, farZ);
    p.shading = v.shading;
    p.ao = v.ao;
    p.grid = v.grid && v.mesh.valid();
    p.floorY = v.floorY;
    p.gridStep = v.gridStep;
    p.highlightPart = v.hoverPart;
    p.soloPart = v.soloPart;
    // Render: a lighter floor, so the shadow on it reads.
    p.background = glm::vec3(v.shading == Shading::Render ? 0.15f : 0.043f);
    v.target.bind();
    renderer_.draw(v.mesh, p);
    v.target.resolve();
    glViewport(0, 0, static_cast<int>(io.DisplaySize.x * fbScale), static_cast<int>(io.DisplaySize.y * fbScale));
    dl->AddImage(static_cast<ImTextureID>(v.target.texture()), pos, end, ImVec2(0, 1), ImVec2(1, 0));
  }
  dl->AddLine(ImVec2(pos.x, end.y - 1), ImVec2(end.x, end.y - 1), col::line);

  // Center: loading / error / nothing.
  const ImVec2 c((pos.x + end.x) * 0.5f, (pos.y + end.y) * 0.5f);
  auto centerText = [&](const char* text, float dy, ImU32 color) {
    const ImVec2 ts = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, text);
    dl->AddText(fonts.sans, textSize(), ImVec2(c.x - ts.x * 0.5f, c.y + dy), color, text);
  };
  if (v.loading && ImGui::GetTime() - v.loadStarted > 0.12) {
    const float t = static_cast<float>(ImGui::GetTime());
    for (int i = 0; i < 3; ++i) {
      const int alpha = 80 + static_cast<int>(150 * std::max(0.0f, std::sin(t * 5.0f - i * 0.8f)));
      dl->AddCircleFilled(ImVec2(c.x + (i - 1) * px(12), c.y - px(6)), px(3), IM_COL32(230, 230, 230, alpha));
    }
    centerText("Loading", px(8), col::text3);
  } else if (!v.error.empty()) {
    drawIcon(dl, Icon::Warning, ImVec2(c.x, c.y - px(16)), px(22), col::text3);
    centerText(v.error.c_str(), px(4), col::text2);
  } else if (!v.mesh.valid() && v.assetId == 0) {
    drawIcon(dl, Icon::Box, ImVec2(c.x, c.y - px(14)), px(30), IM_COL32(60, 60, 60, 255));
    centerText("No model selected", px(14), col::text3);
  }

  if (!v.mesh.valid()) return;

  // Overlays: shading and the outliner (top left), the cube with the view tools
  // under it (top right).
  const float chipH = px(24);
  ImGui::SetCursorScreenPos(ImVec2(pos.x + px(10), pos.y + px(10)));
  {
    const ImVec2 b0 = ImGui::GetCursorScreenPos();
    const char* names[] = {"SOLID", "EDGES", "WIRE", "POINTS", "RENDER"};
    // AO: a switch for the solid views (Render always has it).
    const bool aoChip = v.shading == Shading::Solid || v.shading == Shading::SolidWire;
    float total = aoChip ? fonts.sans->CalcTextSizeA(smallSize(), FLT_MAX, 0, "AO").x + px(16) + px(9) : 0.0f;
    for (const char* n : names) total += fonts.sans->CalcTextSizeA(smallSize(), FLT_MAX, 0, n).x + px(16);
    dl->AddRectFilled(ImVec2(b0.x - px(3), b0.y - px(3)), ImVec2(b0.x + total + px(3), b0.y + chipH + px(3)),
                      IM_COL32(16, 16, 16, 220), px(7));
    for (int i = 0; i < 5; ++i) {
      if (i) ImGui::SameLine(0, 0);
      if (chip(names[i], static_cast<int>(v.shading) == i, chipH)) {
        v.shading = static_cast<Shading>(i);
        uiStateDirtyAt_ = ImGui::GetTime();
      }
      if (ImGui::IsItemHovered()) {
        const char* tips[] = {"Solid  (1)", "Solid with edges  (2)", "Wireframe, hidden lines removed  (3)",
                              "Vertices  (4)", "Render: sun, shadows, ambient occlusion  (5)"};
        tooltip(tips[i]);
      }
    }
    if (aoChip) {
      ImGui::SameLine(0, 0);
      const ImVec2 sp = ImGui::GetCursorScreenPos();
      dl->AddLine(ImVec2(sp.x + px(4), sp.y + px(6)), ImVec2(sp.x + px(4), sp.y + chipH - px(6)), col::line);
      ImGui::SetCursorScreenPos(ImVec2(sp.x + px(9), sp.y));
      if (chip("AO", v.ao, chipH)) {
        v.ao = !v.ao;
        uiStateDirtyAt_ = ImGui::GetTime();
      }
      if (ImGui::IsItemHovered()) tooltip("Ambient occlusion: creases and contacts darker");
    }
  }
  // Outliner: the model's meshes, under the shading; hover lights one up, a
  // click shows only it.
  v.hoverPart = -1;
  const float outlinerTop = pos.y + px(10) + chipH + px(10);
  const float outlinerBottom = end.y - px(12);
  if (v.mesh.parts.size() > 1 && outlinerBottom - outlinerTop > px(40)) {
    const float headH = px(28), rowH = px(24);
    const bool open = !(infoFolded_ & 8);
    const float listH = open ? std::min(rowH * v.mesh.parts.size() + px(4), outlinerBottom - outlinerTop - headH - px(8))
                             : 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(7), outlinerTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(16, 16, 16, 220));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(7));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(3), px(3)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##outliner", ImVec2(std::min(px(240), size.x * 0.45f), headH + listH + px(6)),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    char header[64];
    std::snprintf(header, sizeof header, "MESHES  %zu", v.mesh.parts.size());
    bool nowOpen = open;
    foldHeader(header, nowOpen);
    if (nowOpen != open) {
      infoFolded_ ^= 8;
      uiStateDirtyAt_ = ImGui::GetTime();
    }
    if (open) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
      ImGui::BeginChild("##outlinerList", ImVec2(0, listH), 0);
      ImDrawList* odl = ImGui::GetWindowDrawList();
      const float w = ImGui::GetContentRegionAvail().x;
      for (size_t i = 0; i < v.mesh.parts.size(); ++i) {
        const RenderMesh::Part& part = v.mesh.parts[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool solo = v.soloPart == static_cast<int>(i);
        ImGui::InvisibleButton("##part", ImVec2(w, rowH));
        const bool hov = ImGui::IsItemHovered();
        if (hov) v.hoverPart = static_cast<int>(i);
        if (ImGui::IsItemClicked()) v.soloPart = solo ? -1 : static_cast<int>(i);
        if (hov) tooltip(solo ? "Click: show all" : "Click: show only this one");
        if (solo || hov) odl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), solo ? col::bg4 : col::bg3, px(5));
        drawIcon(odl, Icon::Layers, ImVec2(p.x + px(13), p.y + rowH * 0.5f), px(12), solo ? col::text : col::text3);
        const std::string tris = formatCount(part.indexCount / 3);
        const float tw = fonts.mono->CalcTextSizeA(px(12), FLT_MAX, 0, tris.c_str()).x;
        textClipped(odl, fonts.sans, px(13), ImVec2(p.x + px(26), p.y + (rowH - px(13)) * 0.5f),
                    solo || hov ? col::text : col::text2, part.name.empty() ? "(unnamed)" : part.name.c_str(),
                    w - px(40) - tw);
        odl->AddText(fonts.mono, px(12), ImVec2(p.x + w - tw - px(8), p.y + (rowH - px(12)) * 0.5f), col::text3,
                     tris.c_str());
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
  }
  {
    // A column centred under the cube.
    const float b = px(28);
    const ImVec2 b0(end.x - px(8) - px(84) * 0.5f - b * 0.5f, pos.y + px(8) + px(84) + px(6));
    dl->AddRectFilled(ImVec2(b0.x - px(3), b0.y - px(3)), ImVec2(b0.x + b + px(3), b0.y + (viewerOnly_ ? 3 : 4) * b + px(3)),
                      IM_COL32(16, 16, 16, 220), px(7));
    int slot = 0;
    auto next = [&]() { ImGui::SetCursorScreenPos(ImVec2(b0.x, b0.y + b * slot++)); };
    next();
    if (iconButton("##frame", Icon::Frame, b, false, "Frame the model  (F, double-click)")) {
      frameViewer();
    }
    next();
    if (iconButton("##gridToggle", Icon::Grid, b, v.grid, "Floor grid  (G)")) {
      v.grid = !v.grid;
      uiStateDirtyAt_ = ImGui::GetTime();
    }
    next();
    if (iconButton("##turn", Icon::Refresh, b, v.turntable, "Turntable")) v.turntable = !v.turntable;
    if (!viewerOnly_) next();
    if (!viewerOnly_ && iconButton("##maximize", viewerMax_ ? Icon::Collapse : Icon::Expand, b, viewerMax_,
                   viewerMax_ ? "Back  (Esc, F11)" : "Full window  (F11)")) {
      viewerMax_ = !viewerMax_;
    }
  }
  // Size of the grid cell and the triangle count, bottom right.
  {
    char buf[96];
    const Asset* a = lib_.find(v.assetId);
    const bool metric = a ? a->metric : viewer_.statsValid && viewer_.stats.metric;
    std::snprintf(buf, sizeof buf, "%s tris   grid %s", formatCount(static_cast<int64_t>(v.mesh.indexCount / 3)).c_str(),
                  formatLength(v.gridStep, metric).c_str());
    const float fs = px(12);
    const ImVec2 ts = fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, buf);
    dl->AddText(fonts.mono, fs, ImVec2(end.x - ts.x - px(12), end.y - ts.y - px(12)), col::text3, buf);
  }
  const float cube = px(84);
  navCube("##cube", ImVec2(end.x - cube - px(8), pos.y + px(8)), cube, v.camera.viewMatrix(), v.camera);
}

void App::drawAssetInfo(const Asset& a) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  // A section's header; returns whether it's unfolded (`button`: its + was clicked).
  auto section = [&](const char* label, int bit, bool* button = nullptr, const char* tip = nullptr) {
    bool open = !(infoFolded_ & bit);
    const bool clicked = foldHeader(label, open, Icon::Plus, button != nullptr, tip);
    if (button) *button = clicked;
    if (open != !(infoFolded_ & bit)) {
      infoFolded_ ^= bit;
      uiStateDirtyAt_ = ImGui::GetTime();
    }
    return open;
  };

  // Name and kind.
  ImGui::PushFont(nullptr, px(16));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(a.name.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  {
    std::string kind = a.ext;
    for (char& ch : kind) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    mono("%s  \xC2\xB7  %s", kind.c_str(), formatBytes(a.size).c_str());
  }
  // Path: a click copies it.
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##path", ImVec2(w, px(18)));
    const bool hov = ImGui::IsItemHovered();
    textClipped(dl, fonts.mono, px(12), ImVec2(p.x, p.y + px(2)), hov ? col::text2 : col::text3, a.path.c_str(), w);
    if (hov) tooltip((a.path + "\n\nClick to copy").c_str());
    if (ImGui::IsItemClicked()) {
      ImGui::SetClipboardText(a.path.c_str());
      toast("Path copied");
    }
  }
  ImGui::Dummy(ImVec2(0, px(4)));

  // Actions.
  const float bw = (w - px(8) * 2 - px(34)) * 0.5f;
  if (button("Open", false, bw, Icon::External, true)) openWithDefaultApp(a.path);
  if (ImGui::IsItemHovered()) tooltip("In the app Windows opens this format with  (Enter)");
  ImGui::SameLine(0, px(8));
  if (button("Show in Folder", false, bw, Icon::Reveal, true)) revealInExplorer(a.path);
  ImGui::SameLine(0, px(8));
  {
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    dl->AddRect(bp, ImVec2(bp.x + px(34), bp.y + px(30)), col::line, px(5));
    if (iconButton("##more", Icon::Dots, px(30), false, "More")) ImGui::OpenPopup("##moreMenu");
  }
  if (ImGui::BeginPopup("##moreMenu")) {
    if (ImGui::MenuItem("Copy Path", "Ctrl+C")) ImGui::SetClipboardText(a.path.c_str());
    if (ImGui::BeginMenu("Add to Collection")) {
      collectionMenuItems({a.id});
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Remove from Library", "Del")) removeFromLibrary({a.id});
    if (ImGui::MenuItem("Delete File...", "Shift+Del")) confirmDelete_ = {a.id};
    ImGui::EndPopup();
  }
  ImGui::Dummy(ImVec2(0, px(6)));

  if (a.missing) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRect(p, ImVec2(p.x + w, p.y + px(62)), col::bg4, px(6));
    drawIcon(dl, Icon::Warning, ImVec2(p.x + px(18), p.y + px(20)), px(14), col::text2);
    dl->AddText(fonts.sans, textSize(), ImVec2(p.x + px(34), p.y + px(12)), col::text, "File missing");
    dl->AddText(fonts.sans, px(13), ImVec2(p.x + px(34), p.y + px(33)), col::text3, "Moved or deleted outside the app");
    ImGui::Dummy(ImVec2(w, px(70)));
  } else if (!a.error.empty()) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRect(p, ImVec2(p.x + w, p.y + px(62)), col::bg4, px(6));
    drawIcon(dl, Icon::Warning, ImVec2(p.x + px(18), p.y + px(20)), px(14), col::text2);
    dl->AddText(fonts.sans, textSize(), ImVec2(p.x + px(34), p.y + px(12)), col::text, "Couldn't read this file");
    textClipped(dl, fonts.sans, px(13), ImVec2(p.x + px(34), p.y + px(33)), col::text3, a.error.c_str(), w - px(44));
    ImGui::Dummy(ImVec2(w, px(70)));
  }

  // Numbers.
  if (section("GEOMETRY", 1)) {
    const float cw = (w - px(8)) * 0.5f;
    auto count = [](int64_t n) { return n >= 0 ? formatCount(n) : std::string("—"); };
    statCard("TRIANGLES", count(a.triangles), cw);
    ImGui::SameLine(0, px(8));
    statCard("VERTICES", count(a.vertices), cw);
    statCard("MESHES", count(a.parts), cw);
    ImGui::SameLine(0, px(8));
    statCard("MATERIALS", count(a.materials), cw);
    if (a.triangles >= 0) {
      const std::string dims = formatLength(a.dims.x, a.metric) + "  \xC3\x97  " + formatLength(a.dims.y, a.metric) +
                               "  \xC3\x97  " + formatLength(a.dims.z, a.metric);
      statCard(a.metric ? "SIZE  (X \xC3\x97 Y \xC3\x97 Z)" : "SIZE  (X \xC3\x97 Y \xC3\x97 Z, FILE UNITS)", dims, w);
    }
  }

  ImGui::Dummy(ImVec2(0, px(6)));
  if (section("FILE", 2)) {
    auto row = [&](const char* label, const std::string& value) {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(w, px(24)));
      dl->AddText(fonts.sans, px(13), ImVec2(p.x + px(4), p.y + px(4)), col::text3, label);
      const float vw = fonts.mono->CalcTextSizeA(px(12.5f), FLT_MAX, 0, value.c_str()).x;
      const float room = w - px(100);
      textClipped(dl, fonts.mono, px(12.5f), ImVec2(p.x + w - std::min(vw, room) - px(4), p.y + px(4)), col::text2,
                  value.c_str(), room);
    };
    row("Size on disk", formatBytes(a.size));
    row("Added", formatDate(a.added));
    row("Modified", formatDate(a.mtime));
    if (const WatchedFolder* f = lib_.folder(a.folderId)) {
      const size_t s = f->path.find_last_of('\\');
      row("Watched folder", s == std::string::npos ? f->path : f->path.substr(s + 1));
    }
  }

  // Collections.
  ImGui::Dummy(ImVec2(0, px(6)));
  bool addToCol = false;
  const bool colsOpen = section("COLLECTIONS", 4, &addToCol, "Add to a collection");
  if (addToCol) ImGui::OpenPopup("##addToCol");
  if (ImGui::BeginPopup("##addToCol")) {
    collectionMenuItems({a.id});
    ImGui::EndPopup();
  }
  const std::vector<int64_t> cols = lib_.collectionsOf(a.id);
  if (!colsOpen) {
  } else if (cols.empty()) {
    dimText("  In no collection");
  } else {
    float x = 0.0f;
    for (int64_t cid : cols) {
      std::string name;
      for (const Collection& c : lib_.collections()) {
        if (c.id == cid) name = c.name;
      }
      const float cwid = fonts.sans->CalcTextSizeA(smallSize(), FLT_MAX, 0, name.c_str()).x + px(36);
      if (x > 0.0f && x + cwid < w) {
        ImGui::SameLine(0, px(6));
      } else {
        x = 0.0f;
      }
      x += cwid + px(6);
      ImGui::PushID(static_cast<int>(cid));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##chip", ImVec2(cwid - px(22), px(26)));
      const bool hov = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked()) source_ = Source{SourceKind::Collection, cid, ""};
      if (hov) tooltip("Show this collection");
      dl->AddRectFilled(p, ImVec2(p.x + cwid, p.y + px(26)), hov ? col::bg3 : col::bg2, px(13));
      dl->AddRect(p, ImVec2(p.x + cwid, p.y + px(26)), col::line, px(13));
      dl->AddText(fonts.sans, smallSize(), ImVec2(p.x + px(12), p.y + px(6)), col::text, name.c_str());
      ImGui::SameLine(0, 0);
      if (iconButton("##rm", Icon::Close, px(22), false, "Remove from this collection")) {
        lib_.removeFromCollection(cid, {a.id});
      }
      ImGui::PopID();
    }
  }

  ImGui::Dummy(ImVec2(0, px(10)));
}

void App::drawMultiInfo(const std::vector<int64_t>& ids) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = ImGui::GetContentRegionAvail().x;
  int64_t tris = 0, bytes = 0;
  int known = 0;
  for (int64_t id : ids) {
    if (const Asset* a = lib_.find(id)) {
      bytes += a->size;
      if (a->triangles >= 0) tris += a->triangles, ++known;
    }
  }
  ImGui::PushFont(nullptr, px(16));
  ImGui::Text("%s models selected", formatCount(static_cast<int64_t>(ids.size())).c_str());
  ImGui::PopFont();
  dimText("The view shows the last one clicked");
  ImGui::Dummy(ImVec2(0, px(6)));
  const float cw = (w - px(8)) * 0.5f;
  statCard("TOTAL TRIANGLES", known ? formatCount(tris) : std::string("—"), cw);
  ImGui::SameLine(0, px(8));
  statCard("TOTAL SIZE", formatBytes(bytes), cw);
  ImGui::Dummy(ImVec2(0, px(6)));
  if (button("Add to Collection", false, w, Icon::Collection, true)) ImGui::OpenPopup("##multiCol");
  if (ImGui::BeginPopup("##multiCol")) {
    collectionMenuItems(ids);
    ImGui::EndPopup();
  }
  ImGui::Dummy(ImVec2(0, px(2)));
  if (button("Remove from Library", false, w, Icon::Close, true)) removeFromLibrary(ids);
  ImGui::Dummy(ImVec2(0, px(2)));
  if (button("Delete Files...", false, w, Icon::Trash, true)) confirmDelete_ = ids;
  (void)dl;
}
