// The viewer-only window: a file opened from Explorer, looked at without
// going into the library.
#include "app.h"  // (first: windows.h before GLFW)

#include <GLFW/glfw3.h>

#include <algorithm>

#include "scanner.h"
#include "util.h"

using namespace ui;

void App::stepSibling(int delta) {
  if (siblings_.empty()) return;
  const std::string cur = lower(viewer_.path);
  int i = 0;
  for (size_t k = 0; k < siblings_.size(); ++k) {
    if (lower(siblings_[k]) == cur) i = static_cast<int>(k);
  }
  const int n = static_cast<int>(siblings_.size());
  const std::string next = siblings_[((i + delta) % n + n) % n];
  if (activeTab_ >= 0) tabs_[activeTab_] = next;  // the tab walks with it
  openInViewer(next);
}

void App::openTabs(const std::vector<std::string>& paths) {
  int last = -1;
  for (const std::string& raw : paths) {
    const std::string p = normalizePath(raw);
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const std::string& t) { return lower(t) == lower(p); });
    if (it == tabs_.end()) {
      tabs_.push_back(p);
      it = tabs_.end() - 1;
    }
    last = static_cast<int>(it - tabs_.begin());
  }
  if (last >= 0) activateTab(last);
}

void App::activateTab(int index) {
  if (index >= static_cast<int>(tabs_.size())) index = -1;
  activeTab_ = index;
  viewerMax_ = false;
  if (index < 0) {
    glfwSetWindowTitle(window_, "Creative Cell");
    if (!libraryStarted_) {
      libraryStarted_ = true;
      scanAll();
    }
    return;
  }
  openInViewer(tabs_[index]);
}

void App::closeTab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
  tabs_.erase(tabs_.begin() + index);
  if (activeTab_ == index) {
    // As a browser: the tab on the right takes over, else the one on the
    // left; none left — the library.
    const int n = static_cast<int>(tabs_.size());
    activateTab(n == 0 ? -1 : std::min(index, n - 1));
  } else if (activeTab_ > index) {
    --activeTab_;
  }
}

void App::tabShortcuts() {
  if (tabs_.empty()) return;
  const int n = static_cast<int>(tabs_.size());
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W) && activeTab_ >= 0) closeTab(activeTab_);
  // Ctrl+Tab goes round: library, files, library...
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Tab)) activateTab(activeTab_ + 1 >= n ? -1 : activeTab_ + 1);
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab)) {
    activateTab(activeTab_ < 0 ? n - 1 : activeTab_ - 1);
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1)) activateTab(-1);
}

void App::drawTabBar(ImVec2 pos, ImVec2 size) {
  beginPanel("##tabs", pos, size, col::bg0);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = size.y;
  dl->AddLine(ImVec2(pos.x, pos.y + h - 1), ImVec2(pos.x + size.x, pos.y + h - 1), col::line);
  float x = pos.x + px(6);
  int closeIndex = -1, activate = -2;
  // One tab: the library first (no close), then the files.
  auto tab = [&](int index, const char* label, Icon icon, float width, const char* tip) {
    const bool active = activeTab_ == index;
    const ImVec2 p0(x, pos.y + px(5)), p1(x + width, pos.y + h);
    ImGui::PushID(index);
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##tab", ImVec2(width, p1.y - p0.y));
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) activate = index;
    if (index >= 0 && hov && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) closeIndex = index;
    if (hov && tip) tooltip(tip);
    if (active || hov) {
      dl->AddRectFilled(p0, p1, active ? col::bg1 : IM_COL32(18, 18, 18, 255), px(6), ImDrawFlags_RoundCornersTop);
    }
    if (active) {
      dl->AddLine(ImVec2(p0.x, p1.y - 1), ImVec2(p1.x, p1.y - 1), col::bg1, 2.0f);  // joins the page below
      dl->AddLine(ImVec2(p0.x + px(6), p0.y), ImVec2(p1.x - px(6), p0.y), col::accent, std::max(1.0f, px(1.5f)));
    }
    const float cy = (p0.y + p1.y) * 0.5f;
    const ImU32 fg = active ? col::text : hov ? col::text : col::text2;
    drawIcon(dl, icon, ImVec2(p0.x + px(16), cy), px(13), fg);
    const float closeW = index >= 0 ? px(22) : 0.0f;
    textClipped(dl, fonts.sans, px(13), ImVec2(p0.x + px(30), cy - px(6.5f)), fg, label, width - px(36) - closeW);
    if (index >= 0 && (active || hov)) {
      ImGui::SetCursorScreenPos(ImVec2(p1.x - px(24), cy - px(10)));
      if (iconButton("##close", Icon::Close, px(20), false, "Close  (Ctrl+W)")) closeIndex = index;
    }
    ImGui::PopID();
    x += width + px(2);
  };
  tab(-1, "Library", Icon::Layers, px(112), nullptr);
  const float room = pos.x + size.x - x - px(8);
  const float fileW = tabs_.empty() ? 0.0f : std::clamp(room / tabs_.size() - px(2), px(90), px(220));
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const std::string name = pathU8(u8(tabs_[i]).filename());
    tab(static_cast<int>(i), name.c_str(), Icon::Box, fileW, tabs_[i].c_str());
  }
  endPanel();
  if (closeIndex >= 0) {
    closeTab(closeIndex);
  } else if (activate != -2 && activate != activeTab_) {
    activateTab(activate);
  }
}

void App::viewerModeFrame(float top) {
  const ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  if (W <= 0 || H <= 0) return;
  const float barH = px(52);
  drawViewerModeBar(ImVec2(0, top), ImVec2(W, barH));
  beginPanel("##viewerOnly", ImVec2(0, top + barH), ImVec2(W, H - top - barH), col::bg1);
  drawViewer(ImVec2(0, top + barH), ImVec2(W, H - top - barH));
  endPanel();
  drawSettings();
  drawModals();
  drawToasts();

  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) settingsOpen_ = !settingsOpen_;
  tabShortcuts();
  if (!io.WantTextInput && !settingsOpen_ && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) stepSibling(1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_PageUp)) stepSibling(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameViewer();
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) viewer_.grid = !viewer_.grid, uiStateDirtyAt_ = ImGui::GetTime();
    const ImGuiKey shadeKeys[4] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4};
    for (int i = 0; i < 4; ++i) {
      if (ImGui::IsKeyPressed(shadeKeys[i], false) && !ImGui::GetIO().KeyCtrl) {
        viewer_.shading = static_cast<Shading>(i);
        uiStateDirtyAt_ = ImGui::GetTime();
      }
    }
  }
  thumbs_->endFrame();
  if (uiStateDirtyAt_ > 0.0 && ImGui::GetTime() - uiStateDirtyAt_ > 1.0) saveUiState();
}

void App::drawViewerModeBar(ImVec2 pos, ImVec2 size) {
  beginPanel("##viewerBar", pos, size, col::bg1);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddLine(ImVec2(pos.x, pos.y + size.y - 1), ImVec2(pos.x + size.x, pos.y + size.y - 1), col::line);
  const float cy = pos.y + size.y * 0.5f;
  const float b = px(28);

  // Previous / next model in the folder.
  float x = pos.x + px(12);
  const bool many = siblings_.size() > 1;
  ImGui::SetCursorScreenPos(ImVec2(x, cy - b * 0.5f));
  if (iconButton("##prev", Icon::ChevronLeft, b, false, "Previous in the folder  (Left)", many)) stepSibling(-1);
  x += b + px(2);
  ImGui::SetCursorScreenPos(ImVec2(x, cy - b * 0.5f));
  if (iconButton("##next", Icon::ChevronRight, b, false, "Next in the folder  (Right)", many)) stepSibling(1);
  x += b + px(8);
  if (many) {
    int i = 0;
    const std::string cur = lower(viewer_.path);
    for (size_t k = 0; k < siblings_.size(); ++k) {
      if (lower(siblings_[k]) == cur) i = static_cast<int>(k);
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d / %zu", i + 1, siblings_.size());
    dl->AddText(fonts.mono, px(11.5f), ImVec2(x, cy - px(6)), col::text3, buf);
    x += fonts.mono->CalcTextSizeA(px(11.5f), FLT_MAX, 0, buf).x + px(16);
  }

  // Right side: what can be done with it.
  float rx = pos.x + size.x - px(12);
  rx -= b;
  ImGui::SetCursorScreenPos(ImVec2(rx, cy - b * 0.5f));
  if (iconButton("##settings", Icon::Gear, b, settingsOpen_, "Settings  (Ctrl+,)")) settingsOpen_ = true;
  auto action = [&](const char* label, Icon icon, bool primary) {
    const float w = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, label).x + px(44);
    rx -= w + px(8);
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - px(15)));
    return button(label, primary, w, icon, true);
  };
  const Asset* inLib = lib_.findByPath(viewer_.path);
  const bool inLibrary = inLib && !inLib->hidden;
  if (inLibrary) {
    const char* label = "In Library";
    const float w = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, label).x + px(44);
    rx -= w + px(8);
    const ImVec2 p(rx, cy - px(15));
    dl->AddRect(p, ImVec2(p.x + w, p.y + px(30)), col::line, px(5));
    drawIcon(dl, Icon::Check, ImVec2(p.x + px(18), cy), px(12), col::text2);
    dl->AddText(fonts.sans, textSize(), ImVec2(p.x + px(30), cy - textSize() * 0.5f), col::text2, label);
  } else if (action("Add to Library", Icon::Plus, true)) {
    FileEntry e;
    if (statFile(viewer_.path, e)) {
      lib_.addFile(e);
      toast("Added to the library");
    }
  }
  if (action("Show in Folder", Icon::Reveal, false)) revealInExplorer(viewer_.path);

  // Name and numbers, in what's left between.
  const float room = rx - x - px(16);
  const std::string name = pathU8(u8(viewer_.path).filename());
  textClipped(dl, fonts.sans, px(15), ImVec2(x, cy - px(17)), col::text, name.c_str(), room);
  std::string meta = formatBytes(viewer_.fileSize);
  if (viewer_.statsValid) {
    const ModelStats& s = viewer_.stats;
    meta = formatCount(s.triangles) + " tris   " + formatCount(s.vertices) + " verts   " + formatCount(s.parts) +
           (s.parts == 1 ? " mesh   " : " meshes   ") + formatLength(s.size.x, s.metric) + " x " +
           formatLength(s.size.y, s.metric) + " x " + formatLength(s.size.z, s.metric) + "   " + meta;
  }
  textClipped(dl, fonts.mono, px(11), ImVec2(x, cy + px(3)), col::text3, meta.c_str(), room);
  endPanel();
}
