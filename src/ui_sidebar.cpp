// The left panel: where the grid's assets come from.
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "app.h"
#include "file_dialog.h"
#include "util.h"

using namespace ui;

namespace {
enum class RowState { None, Clicked, Toggled };

// A sidebar row: icon, label, count on the right; `chevron` 0 none, 1
// closed, 2 open (a click on it toggles).
RowState sideRow(const char* id, Icon icon, const char* label, int count, bool selected, float indent, int chevron,
                 bool dim = false) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = px(30);
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered();
  const ImVec2 e(p.x + w, p.y + h);
  if (selected) {
    dl->AddRectFilled(p, e, col::bg3, px(6));
  } else if (hov) {
    dl->AddRectFilled(p, e, IM_COL32(22, 22, 22, 255), px(6));
  }
  float x = p.x + px(8) + indent;
  const float cy = p.y + h * 0.5f;
  bool toggled = false;
  if (chevron) {
    const ImVec2 cc(x + px(6), cy);
    const bool overChevron = hov && ImGui::GetIO().MousePos.x < x + px(14);
    drawIcon(dl, chevron == 2 ? Icon::ChevronDown : Icon::ChevronRight, cc, px(10),
             overChevron ? col::text : col::text3);
    toggled = clicked && overChevron;
  }
  x += px(16);
  const ImU32 fg = selected ? col::text : dim ? col::text3 : col::text2;
  drawIcon(dl, icon, ImVec2(x + px(7), cy), px(14), selected ? col::text : col::text2);
  x += px(22);
  float countW = 0.0f;
  char buf[32] = "";
  if (count >= 0) {
    std::snprintf(buf, sizeof buf, "%s", formatCount(count).c_str());
    countW = fonts.mono->CalcTextSizeA(px(12), FLT_MAX, 0, buf).x;
    dl->AddText(fonts.mono, px(12), ImVec2(e.x - px(10) - countW, cy - px(6.5f)), selected ? col::text2 : col::text3, buf);
  }
  textClipped(dl, fonts.sans, textSize(), ImVec2(x, cy - textSize() * 0.5f), selected || hov ? col::text : fg, label,
              e.x - x - countW - px(18));
  if (toggled) return RowState::Toggled;
  return clicked ? RowState::Clicked : RowState::None;
}

std::string lastPart(const std::string& path) {
  const size_t s = path.find_last_of('\\');
  return s == std::string::npos ? path : path.substr(s + 1);
}
}  // namespace

void App::drawSidebar(ImVec2 pos, ImVec2 size) {
  beginPanel("##sidebar", pos, size, col::bg1, ImVec2(px(10), px(8)));
  ImDrawList* panelDl = ImGui::GetWindowDrawList();
  panelDl->AddLine(ImVec2(pos.x + size.x - 1, pos.y), ImVec2(pos.x + size.x - 1, pos.y + size.y), col::line);

  // Workspaces: the library; the marketplace keeps its place for later.
  {
    const float w = ImGui::GetContentRegionAvail().x, h = px(34);
    auto workspace = [&](const char* id, Icon icon, const char* label, bool active, bool enabled, const char* badge) {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::BeginDisabled(!enabled);
      ImGui::InvisibleButton(id, ImVec2(w, h));
      ImGui::EndDisabled();
      const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
      const ImVec2 e(p.x + w, p.y + h);
      if (active) {
        panelDl->AddRectFilled(p, e, col::bg3, px(6));
        panelDl->AddRectFilled(ImVec2(p.x, p.y + px(9)), ImVec2(p.x + px(2.5f), e.y - px(9)), col::accent, px(2));
      } else if (hov && enabled) {
        panelDl->AddRectFilled(p, e, IM_COL32(22, 22, 22, 255), px(6));
      }
      const float cy = p.y + h * 0.5f;
      const ImU32 fg = active ? col::text : enabled ? col::text2 : col::text3;
      drawIcon(panelDl, icon, ImVec2(p.x + px(19), cy), px(15), fg);
      panelDl->AddText(fonts.sans, px(14), ImVec2(p.x + px(36), cy - px(7.5f)), fg, label);
      if (badge) {
        const float bw = fonts.mono->CalcTextSizeA(px(10.5f), FLT_MAX, 0, badge).x + px(10);
        const ImVec2 b0(e.x - bw - px(10), cy - px(8));
        panelDl->AddRect(b0, ImVec2(b0.x + bw, b0.y + px(16)), col::bg4, px(3));
        panelDl->AddText(fonts.mono, px(10.5f), ImVec2(b0.x + px(5), b0.y + px(3)), col::text3, badge);
      }
      if (!enabled && hov) tooltip("Marketplace is planned for a later version");
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));
    workspace("##wsLibrary", Icon::Layers, "Library", true, true, nullptr);
    workspace("##wsMarket", Icon::Store, "Marketplace", false, false, "SOON");
    ImGui::PopStyleVar();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, px(8)));
    panelDl->AddLine(ImVec2(pos.x, sp.y + px(4)), ImVec2(pos.x + size.x - 1, sp.y + px(4)), col::line);
  }

  ImGui::BeginChild("##sideScroll", ImVec2(0, pos.y + size.y - ImGui::GetCursorScreenPos().y - px(8)), 0);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));

  // --- the models ---
  sectionHeader("MODELS");
  auto pick = [&](const Source& s) {
    if (source_ != s) {
      source_ = s;
      scrollToFocus_ = false;
    }
  };
  if (sideRow("##all", Icon::Box, "All Models", countAll_, source_.kind == SourceKind::All, 0, 0) == RowState::Clicked) {
    pick(Source{});
  }
  if (sideRow("##recent", Icon::Clock, "Recently Added", countRecent_, source_.kind == SourceKind::Recent, 0, 0) ==
      RowState::Clicked) {
    pick(Source{SourceKind::Recent});
  }
  if (ImGui::IsItemHovered()) tooltip("Added in the last 7 days");
  if (countMissing_ > 0) {
    if (sideRow("##missing", Icon::Warning, "Missing Files", countMissing_, source_.kind == SourceKind::Missing, 0, 0) ==
        RowState::Clicked) {
      pick(Source{SourceKind::Missing});
    }
    if (ImGui::IsItemHovered()) tooltip("Files that were moved or deleted outside the app");
  }

  // --- folders ---
  ImGui::Dummy(ImVec2(0, px(10)));
  if (sectionHeader("FOLDERS", Icon::Plus, true, "Watch a folder")) {
    const std::string dir = pickFolderDialog(window_, "Add Folder");
    if (!dir.empty()) {
      const bool keep = copyOnImport_;
      copyOnImport_ = false;
      importPaths({dir});
      copyOnImport_ = keep;
    }
  }
  if (lib_.folders().empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(6));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Add a folder to keep it in sync");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  int64_t removeFolder = 0;
  for (const WatchedFolder& f : lib_.folders()) {
    int count = 0;
    for (const auto& [fid, c] : folderCounts_) {
      if (fid == f.id) count = c;
    }
    ImGui::PushID(static_cast<int>(f.id));
    const std::vector<FolderNode>& tree = folderTree(f.id);
    const std::string key = f.path;
    const bool open = expanded_.count(key) != 0;
    const bool sel = source_.kind == SourceKind::Folder && source_.id == f.id && source_.sub.empty();
    const RowState r = sideRow("##folder", Icon::Folder, lastPart(f.path).c_str(), count, sel, 0,
                               tree.empty() ? 0 : open ? 2 : 1);
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) tooltip(f.path.c_str());
    if (r == RowState::Toggled) {
      if (open) expanded_.erase(key);
      else expanded_.insert(key);
    } else if (r == RowState::Clicked) {
      pick(Source{SourceKind::Folder, f.id, ""});
    }
    if (ImGui::BeginPopupContextItem("##folderMenu")) {
      if (ImGui::MenuItem("Rescan")) scanFolder(f.id);
      if (ImGui::MenuItem("Show in Explorer")) revealInExplorer(f.path);
      ImGui::Separator();
      if (ImGui::MenuItem("Stop Watching")) removeFolder = f.id;
      if (ImGui::IsItemHovered()) tooltip("Its models leave the library; the files stay on disk");
      ImGui::EndPopup();
    }
    // Subfolders.
    std::function<void(const std::vector<FolderNode>&, float)> drawNodes = [&](const std::vector<FolderNode>& nodes,
                                                                                 float indent) {
      for (const FolderNode& n : nodes) {
        ImGui::PushID(n.path.c_str());
        const bool nOpen = expanded_.count(n.path) != 0;
        const bool nSel = source_.kind == SourceKind::Folder && source_.id == f.id && source_.sub == n.path;
        const RowState nr = sideRow("##sub", Icon::Folder, n.name.c_str(), n.count, nSel, indent,
                                    n.children.empty() ? 0 : nOpen ? 2 : 1);
        if (nr == RowState::Toggled) {
          if (nOpen) expanded_.erase(n.path);
          else expanded_.insert(n.path);
        } else if (nr == RowState::Clicked) {
          pick(Source{SourceKind::Folder, f.id, n.path});
        }
        if (ImGui::BeginPopupContextItem("##subMenu")) {
          if (ImGui::MenuItem("Show in Explorer")) revealInExplorer(n.path);
          ImGui::EndPopup();
        }
        if (nOpen) drawNodes(n.children, indent + px(14));
        ImGui::PopID();
      }
    };
    if (open) drawNodes(tree, px(14));
    ImGui::PopID();
  }
  if (removeFolder) lib_.removeFolder(removeFolder);

  // --- collections ---
  ImGui::Dummy(ImVec2(0, px(10)));
  if (sectionHeader("COLLECTIONS", Icon::Plus, true, "New collection")) {
    newCollectionAssets_.clear();
    newCollectionOpen_ = true;
  }
  if (lib_.collections().empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(6));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Group models across folders: make a collection, then drag models onto it");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  int64_t deleteCollection = 0;
  int64_t moveId = 0;
  int moveTo = -1;
  const std::vector<Collection> collections = lib_.collections();  // (a copy: rows may change it)
  for (size_t i = 0; i < collections.size(); ++i) {
    const Collection& c = collections[i];
    ImGui::PushID(static_cast<int>(c.id));
    const bool sel = source_.kind == SourceKind::Collection && source_.id == c.id;
    if (sideRow("##col", Icon::Collection, c.name.c_str(), lib_.collectionSize(c.id), sel, 0, 0) == RowState::Clicked) {
      pick(Source{SourceKind::Collection, c.id, ""});
    }
    // Models dropped on it join it; collections dragged reorder.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
      ImGui::SetDragDropPayload("CC_COLLECTION", &c.id, sizeof c.id);
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CC_ASSETS")) {
        const int64_t* ids = static_cast<const int64_t*>(pl->Data);
        std::vector<int64_t> v(ids, ids + pl->DataSize / sizeof(int64_t));
        lib_.addToCollection(c.id, v);
        toast("Added " + std::to_string(v.size()) + " to " + c.name);
      }
      if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CC_COLLECTION")) {
        moveId = *static_cast<const int64_t*>(pl->Data);
        moveTo = static_cast<int>(i);
      }
      ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##colMenu")) {
      if (ImGui::MenuItem("Rename...")) renameCollection_ = c.id;
      if (ImGui::MenuItem("Delete Collection")) deleteCollection = c.id;
      if (ImGui::IsItemHovered()) tooltip("Only the collection; its models stay in the library");
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (deleteCollection) lib_.deleteCollection(deleteCollection);
  if (moveId) lib_.moveCollection(moveId, moveTo);

  ImGui::PopStyleVar();
  ImGui::EndChild();

  endPanel();
}
