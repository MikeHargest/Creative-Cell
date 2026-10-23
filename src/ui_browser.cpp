// The middle: the assets of the picked source as tiles or a table.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app.h"
#include "file_dialog.h"
#include "util.h"

using namespace ui;

namespace {
std::string shortCount(int64_t n) {
  if (n < 0) return "—";
  char buf[32];
  if (n < 1000) {
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n));
  } else if (n < 1000000) {
    std::snprintf(buf, sizeof buf, "%.1fK", n / 1000.0);
  } else {
    std::snprintf(buf, sizeof buf, "%.1fM", n / 1000000.0);
  }
  return buf;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

const char* sortName(SortBy s) {
  switch (s) {
    case SortBy::Name:
      return "Name";
    case SortBy::Size:
      return "File size";
    case SortBy::Triangles:
      return "Triangles";
    default:
      return "Date added";
  }
}
}  // namespace

void App::beginDragAssets(int64_t id) {
  if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;
  // A selected tile carries the whole selection; another one only itself.
  std::vector<int64_t> ids = isSelected(id) ? selectedIds() : std::vector<int64_t>{id};
  ImGui::SetDragDropPayload("CC_ASSETS", ids.data(), ids.size() * sizeof(int64_t));
  const Asset* a = lib_.find(id);
  if (ids.size() == 1 && a) {
    ImGui::Text("%s", a->name.c_str());
  } else {
    ImGui::Text("%zu models", ids.size());
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
  ImGui::TextUnformatted("Drop on a collection, or drag out of the window");
  ImGui::TextUnformatted("into Blender, Unreal, Unity, Explorer");
  ImGui::PopStyleColor();
  ImGui::EndDragDropSource();
}

void App::collectionMenuItems(const std::vector<int64_t>& ids) {
  for (const Collection& c : lib_.collections()) {
    const bool all = std::all_of(ids.begin(), ids.end(), [&](int64_t id) { return lib_.inCollection(c.id, id); });
    if (ImGui::MenuItem(c.name.c_str(), nullptr, all)) {
      if (all) lib_.removeFromCollection(c.id, ids);
      else lib_.addToCollection(c.id, ids);
    }
  }
  if (!lib_.collections().empty()) ImGui::Separator();
  if (ImGui::MenuItem("New Collection...")) {
    newCollectionAssets_ = ids;
    newCollectionOpen_ = true;
  }
}

void App::assetContextMenu(int64_t clickedId) {
  if (!ImGui::BeginPopupContextItem("##assetMenu")) return;
  if (ImGui::IsWindowAppearing() && !isSelected(clickedId)) selectOnly(clickedId);
  const std::vector<int64_t> ids = isSelected(clickedId) ? selectedIds() : std::vector<int64_t>{clickedId};
  const Asset* a = lib_.find(clickedId);
  if (a) {
    if (ImGui::MenuItem("Open", "Enter")) openWithDefaultApp(a->path);
    if (ImGui::MenuItem("Show in Explorer")) revealInExplorer(a->path);
    if (ImGui::MenuItem(ids.size() > 1 ? "Copy Paths" : "Copy Path", "Ctrl+C")) {
      std::string text;
      for (int64_t id : ids) {
        if (const Asset* b = lib_.find(id)) text += (text.empty() ? "" : "\r\n") + b->path;
      }
      ImGui::SetClipboardText(text.c_str());
    }
    ImGui::Separator();
  }
  if (ImGui::BeginMenu("Add to Collection")) {
    collectionMenuItems(ids);
    ImGui::EndMenu();
  }
  if (source_.kind == SourceKind::Collection && ImGui::MenuItem("Remove from This Collection")) {
    lib_.removeFromCollection(source_.id, ids);
  }
  if (a && !a->error.empty() && ImGui::MenuItem("Try Again")) {
    lib_.setAnalysisError(a->id, "", 0);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Remove from Library", "Del")) removeFromLibrary(ids);
  if (ImGui::MenuItem("Delete File...", "Shift+Del")) confirmDelete_ = ids;
  ImGui::EndPopup();
}

void App::drawBrowser(ImVec2 pos, ImVec2 size) {
  beginPanel("##browser", pos, size, col::bg0);
  const float barH = px(52);
  ImGui::SetCursorScreenPos(pos);
  ImGui::BeginChild("##toolbar", ImVec2(size.x, barH), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  drawBrowserToolbar(size.x);
  ImGui::EndChild();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + barH - 1), ImVec2(pos.x + size.x, pos.y + barH - 1),
                                      col::line);

  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + barH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(16)));
  ImGui::BeginChild("##content", ImVec2(size.x, size.y - barH), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  static std::string lastSource;
  const std::string srcSig = std::to_string(static_cast<int>(source_.kind)) + "|" + std::to_string(source_.id) + "|" + source_.sub;
  if (srcSig != lastSource) {
    lastSource = srcSig;
    ImGui::SetScrollY(0.0f);
  }
  if (visible_.empty()) {
    drawEmptyState(countAll_ == 0);
  } else if (viewMode_ == ViewMode::Grid) {
    drawGrid();
  } else {
    drawList();
  }
  ImGui::EndChild();
  endPanel();
}

void App::drawBrowserToolbar(float width) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetWindowPos();
  const float h = ImGui::GetWindowHeight();
  const float cy = p0.y + h * 0.5f;

  // Title: what's shown.
  std::string title = "All Models";
  switch (source_.kind) {
    case SourceKind::Recent:
      title = "Recently Added";
      break;
    case SourceKind::Missing:
      title = "Missing Files";
      break;
    case SourceKind::Folder: {
      const WatchedFolder* f = lib_.folder(source_.id);
      const std::string& p = source_.sub.empty() ? (f ? f->path : std::string()) : source_.sub;
      const size_t s = p.find_last_of('\\');
      title = s == std::string::npos ? p : p.substr(s + 1);
      break;
    }
    case SourceKind::Collection:
      for (const Collection& c : lib_.collections()) {
        if (c.id == source_.id) title = c.name;
      }
      break;
    default:
      break;
  }

  // Right side first (it decides how much room the title gets).
  const float chipH = px(28);
  float rx = p0.x + width - px(12);
  auto place = [&](float w) {
    rx -= w;
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - chipH * 0.5f));
    rx -= px(8);
  };
  // Fold / unfold the side panels: at the edges they belong to.
  ImGui::SetCursorScreenPos(ImVec2(p0.x + px(12), cy - chipH * 0.5f));
  if (iconButton("##sidebarToggle", Icon::PanelLeft, chipH, sidebarOpen_,
                 sidebarOpen_ ? "Hide the sidebar  (Ctrl+[)" : "Show the sidebar  (Ctrl+[)")) {
    sidebarOpen_ = !sidebarOpen_;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  place(chipH);
  if (iconButton("##inspectorToggle", Icon::PanelRight, chipH, inspectorOpen_,
                 inspectorOpen_ ? "Hide the inspector  (Ctrl+])" : "Show the inspector  (Ctrl+])")) {
    inspectorOpen_ = !inspectorOpen_;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  dl->AddLine(ImVec2(rx + px(2), cy - px(9)), ImVec2(rx + px(2), cy + px(9)), col::line);
  rx -= px(4);
  // The app's own buttons (there's no top bar).
  rx = drawAppActions(rx, cy) - px(10);
  dl->AddLine(ImVec2(rx + px(2), cy - px(9)), ImVec2(rx + px(2), cy + px(9)), col::line);
  rx -= px(6);
  const float titleX = p0.x + px(12) + chipH + px(10);
  // View: grid / list.
  place(chipH);
  if (iconButton("##list", Icon::List, chipH, viewMode_ == ViewMode::List, "List")) {
    viewMode_ = ViewMode::List;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  rx += px(6);
  place(chipH);
  if (iconButton("##grid", Icon::Grid, chipH, viewMode_ == ViewMode::Grid, "Tiles")) {
    viewMode_ = ViewMode::Grid;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  // Tile size.
  if (viewMode_ == ViewMode::Grid && width > px(860)) {
    place(px(84));
    ImGui::SetNextItemWidth(px(84));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, px(2)));
    ImGui::SetCursorScreenPos(ImVec2(rx + px(8), cy - px(7)));
    if (ImGui::SliderFloat("##tile", &tileSize_, 110.0f, 300.0f, "")) uiStateDirtyAt_ = ImGui::GetTime();
    if (ImGui::IsItemHovered()) tooltip("Tile size");
    ImGui::PopStyleVar();
  }
  // Sort.
  {
    const std::string label = std::string(sortName(sort_)) + (sortDesc_ ? "  \xE2\x86\x93" : "  \xE2\x86\x91");
    const float w = fonts.sans->CalcTextSizeA(smallSize(), FLT_MAX, 0, label.c_str()).x + px(16);
    place(w);
    if (chip((label + "##sort").c_str(), ImGui::IsPopupOpen("##sortMenu"), chipH)) ImGui::OpenPopup("##sortMenu");
    if (ImGui::IsItemHovered()) tooltip("Sort");
    ImGui::SetNextWindowPos(ImVec2(rx + px(8) + w, cy + chipH * 0.5f + px(4)), ImGuiCond_Always, ImVec2(1, 0));
    if (ImGui::BeginPopup("##sortMenu")) {
      for (SortBy s : {SortBy::Added, SortBy::Name, SortBy::Size, SortBy::Triangles}) {
        if (ImGui::MenuItem(sortName(s), nullptr, sort_ == s)) {
          if (sort_ != s) sortDesc_ = s != SortBy::Name;
          sort_ = s;
          uiStateDirtyAt_ = ImGui::GetTime();
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Ascending", nullptr, !sortDesc_)) sortDesc_ = false, uiStateDirtyAt_ = ImGui::GetTime();
      if (ImGui::MenuItem("Descending", nullptr, sortDesc_)) sortDesc_ = true, uiStateDirtyAt_ = ImGui::GetTime();
      ImGui::EndPopup();
    }
  }
  // Format.
  {
    const char* labels[] = {"All formats", "FBX", "OBJ", "glTF / GLB", "STL"};
    const char* keys[] = {"", "fbx", "obj", "gltf", "stl"};
    const char* current = labels[0];
    for (int i = 0; i < 5; ++i) {
      if (format_ == keys[i]) current = labels[i];
    }
    const std::string label = std::string(current) + "     ";  // room for the chevron
    const float w = fonts.sans->CalcTextSizeA(smallSize(), FLT_MAX, 0, label.c_str()).x + px(16);
    place(w);
    if (chip((label + "##format").c_str(), ImGui::IsPopupOpen("##formatMenu") || !format_.empty(), chipH)) {
      ImGui::OpenPopup("##formatMenu");
    }
    drawIcon(dl, Icon::ChevronDown, ImVec2(ImGui::GetItemRectMax().x - px(13), cy), px(9), col::text3);
    if (ImGui::IsItemHovered()) tooltip("Format");
    ImGui::SetNextWindowPos(ImVec2(rx + px(8) + w, cy + chipH * 0.5f + px(4)), ImGuiCond_Always, ImVec2(1, 0));
    if (ImGui::BeginPopup("##formatMenu")) {
      for (int i = 0; i < 5; ++i) {
        if (i == 1) ImGui::Separator();
        if (ImGui::MenuItem(labels[i], nullptr, format_ == keys[i])) format_ = keys[i];
      }
      ImGui::EndPopup();
    }
  }
  // Search.
  {
    const float leftLimit = titleX + std::min(px(200), fonts.sans->CalcTextSizeA(px(15), FLT_MAX, 0, title.c_str()).x + px(70));
    const float w = std::clamp(rx - px(10) - leftLimit, px(120), px(280));
    place(w);
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const ImVec2 se(sp.x + w, sp.y + chipH);
    const bool focused = ImGui::GetID("##search") == ImGui::GetActiveID();
    dl->AddRectFilled(sp, se, col::bg2, px(6));
    dl->AddRect(sp, se, focused ? col::bg4 : col::line, px(6));
    drawIcon(dl, Icon::Search, ImVec2(sp.x + px(15), sp.y + chipH * 0.5f), px(12), col::text3);
    ImGui::SetCursorScreenPos(ImVec2(sp.x + px(28), sp.y + (chipH - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::SetNextItemWidth(w - px(28) - (search_[0] ? px(26) : px(4)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), ImGui::GetStyle().FramePadding.y));
    if (focusSearch_) {
      ImGui::SetKeyboardFocusHere();
      focusSearch_ = false;
    }
    ImGui::InputTextWithHint("##search", "Search  (Ctrl+F)", search_, sizeof search_);
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) search_[0] = 0;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (search_[0]) {
      ImGui::SetCursorScreenPos(ImVec2(se.x - px(25), sp.y + (chipH - px(22)) * 0.5f));
      if (iconButton("##clearSearch", Icon::Close, px(22), false, "Clear")) search_[0] = 0;
    }
  }

  // Title and count.
  const float ts = px(15);
  const float titleRoom = rx - titleX - px(8);
  const std::string count = formatCount(static_cast<int64_t>(visible_.size()));
  const float countW = fonts.mono->CalcTextSizeA(px(12.5f), FLT_MAX, 0, count.c_str()).x;
  textClipped(dl, fonts.sans, ts, ImVec2(titleX, cy - ts * 0.55f), col::text, title.c_str(),
              std::max(0.0f, titleRoom - countW - px(10)));
  const float tw = std::min(fonts.sans->CalcTextSizeA(ts, FLT_MAX, 0, title.c_str()).x, std::max(0.0f, titleRoom - countW - px(10)));
  if (titleRoom > countW + px(30)) {
    dl->AddText(fonts.mono, px(12.5f), ImVec2(titleX + tw + px(10), cy - px(6)), col::text3, count.c_str());
  }
}

void App::drawGrid() {
  const ImGuiStyle& st = ImGui::GetStyle();
  (void)st;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float gap = px(12);
  const float want = px(tileSize_);
  const int cols = std::max(1, static_cast<int>((avail + gap) / (want + gap)));
  gridColumns_ = cols;
  const float tileW = std::floor((avail - gap * (cols - 1)) / cols);
  const float infoH = px(52);
  const float tileH = tileW + infoH;
  const int rows = (static_cast<int>(visible_.size()) + cols - 1) / cols;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  if (scrollToFocus_) {
    scrollToFocus_ = false;
    auto it = std::find(visible_.begin(), visible_.end(), focused_);
    if (it != visible_.end()) {
      const int row = static_cast<int>(it - visible_.begin()) / cols;
      const float top = row * (tileH + gap);
      const float scroll = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight() - px(32);
      if (top < scroll) ImGui::SetScrollY(top);
      else if (top + tileH > scroll + viewH) ImGui::SetScrollY(top + tileH - viewH);
    }
  }

  ImGuiListClipper clipper;
  clipper.Begin(rows, tileH + gap);
  bool anyTileHovered = false;
  const double now = ImGui::GetTime();
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      for (int c = 0; c < cols; ++c) {
        const int index = row * cols + c;
        if (index >= static_cast<int>(visible_.size())) break;
        const int64_t id = visible_[index];
        const Asset* a = lib_.find(id);
        if (!a) continue;
        const ImVec2 p(origin.x + c * (tileW + gap), origin.y + row * (tileH + gap));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(static_cast<int>(id));
        ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
        const bool hov = ImGui::IsItemHovered();
        anyTileHovered |= hov;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) clickSelect(id, index);
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openWithDefaultApp(a->path);
        beginDragAssets(id);
        assetContextMenu(id);
        const bool sel = isSelected(id);
        const ImVec2 e(p.x + tileW, p.y + tileH);
        // Card.
        dl->AddRectFilled(p, e, sel ? IM_COL32(28, 28, 28, 255) : hov ? IM_COL32(24, 24, 24, 255) : col::bg2, px(8));
        if (sel) {
          dl->AddRect(p, e, id == focused_ ? col::accent : IM_COL32(170, 170, 170, 255), px(8), px(1.5f));
        } else if (hov) {
          dl->AddRect(p, e, col::bg4, px(8));
        }
        // Picture.
        const float pad = px(8);
        const ImVec2 t0(p.x + pad, p.y + pad), t1(p.x + tileW - pad, p.y + tileW - pad);
        dl->AddRectFilled(t0, t1, IM_COL32(17, 17, 17, 255), px(6));
        const ImVec2 tc((t0.x + t1.x) * 0.5f, (t0.y + t1.y) * 0.5f);
        GLuint tex = 0;
        if (!a->needsAnalysis() && a->error.empty()) tex = thumbs_->get(id);
        if (tex) {
          dl->AddImageRounded(static_cast<ImTextureID>(tex), t0, t1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(6));
        } else if (!a->error.empty()) {
          drawIcon(dl, Icon::Warning, tc, px(22), col::text3);
          const char* msg = "Couldn't read";
          const ImVec2 ms = fonts.sans->CalcTextSizeA(px(12), FLT_MAX, 0, msg);
          dl->AddText(fonts.sans, px(12), ImVec2(tc.x - ms.x * 0.5f, tc.y + px(18)), col::text3, msg);
        } else {
          // Waiting for its thumbnail: a quiet pulse.
          const int alpha = a->needsAnalysis() ? 70 + static_cast<int>(50 * std::sin(now * 3.0 + index * 0.4)) : 90;
          drawIcon(dl, Icon::Box, tc, px(26), IM_COL32(200, 200, 200, alpha));
        }
        // Format badge.
        {
          const std::string fmt = upper(a->ext);
          const float fs = px(10.5f);
          const ImVec2 bs = fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, fmt.c_str());
          const ImVec2 b0(t0.x + px(6), t0.y + px(6));
          dl->AddRectFilled(b0, ImVec2(b0.x + bs.x + px(10), b0.y + bs.y + px(6)), IM_COL32(10, 10, 10, 200), px(4));
          dl->AddText(fonts.mono, fs, ImVec2(b0.x + px(5), b0.y + px(3)), col::text2, fmt.c_str());
        }
        if (a->missing) {
          const char* m = "MISSING";
          const float fs = px(10.5f);
          const ImVec2 bs = fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, m);
          const ImVec2 b0(t1.x - bs.x - px(16), t0.y + px(6));
          dl->AddRectFilled(b0, ImVec2(t1.x - px(6), b0.y + bs.y + px(6)), IM_COL32(230, 230, 230, 230), px(4));
          dl->AddText(fonts.mono, fs, ImVec2(b0.x + px(5), b0.y + px(3)), IM_COL32(10, 10, 10, 255), m);
        }
        // Name and numbers.
        const float nameY = p.y + tileW + px(2);
        textClipped(dl, fonts.sans, px(13), ImVec2(p.x + pad + px(2), nameY), sel || hov ? col::text : IM_COL32(215, 215, 215, 255),
                    a->name.c_str(), tileW - pad * 2);
        std::string meta = a->triangles >= 0 ? shortCount(a->triangles) + " tris" : std::string("...");
        const std::string sizeText = formatBytes(a->size);
        const float ms = px(12);
        dl->AddText(fonts.mono, ms, ImVec2(p.x + pad + px(2), nameY + px(20)), col::text3, meta.c_str());
        const float sw = fonts.mono->CalcTextSizeA(ms, FLT_MAX, 0, sizeText.c_str()).x;
        const float mw = fonts.mono->CalcTextSizeA(ms, FLT_MAX, 0, meta.c_str()).x;
        if (mw + sw + px(16) < tileW - pad * 2) {
          dl->AddText(fonts.mono, ms, ImVec2(e.x - pad - px(2) - sw, nameY + px(20)), col::text3, sizeText.c_str());
        }
        if (hov && !ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetCurrentContext()->HoveredIdTimer > 0.6f) {
          tooltip((a->name + "." + a->ext + "\n" + a->path).c_str());
        }
        ImGui::PopID();
      }
    }
  }
  clipper.End();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (tileH + gap)));
  ImGui::Dummy(ImVec2(1, px(8)));
  // A click on the empty space clears the selection.
  if (!anyTileHovered && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
    clearSelection();
  }
}

void App::drawList() {
  const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(px(8), px(4)));
  if (!ImGui::BeginTable("##list", 6, flags)) {
    ImGui::PopStyleVar();
    return;
  }
  const ImGuiTableColumnFlags none = ImGuiTableColumnFlags_NoSort;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch | (sort_ == SortBy::Name ? ImGuiTableColumnFlags_DefaultSort : 0), 3.0f, 1);
  ImGui::TableSetupColumn("FORMAT", none, 0.7f, 0);
  ImGui::TableSetupColumn("TRIANGLES", sort_ == SortBy::Triangles ? ImGuiTableColumnFlags_DefaultSort : 0, 1.0f, 3);
  ImGui::TableSetupColumn("SIZE", sort_ == SortBy::Size ? ImGuiTableColumnFlags_DefaultSort : 0, 0.9f, 2);
  ImGui::TableSetupColumn("ADDED", sort_ == SortBy::Added ? ImGuiTableColumnFlags_DefaultSort : 0, 1.3f, 0);
  ImGui::TableSetupColumn("LOCATION", none, 2.4f, 0);
  // Header: small grey capitals.
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  for (int c = 0; c < 6; ++c) {
    ImGui::TableSetColumnIndex(c);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::PushFont(nullptr, px(12));
    ImGui::TableHeader(ImGui::TableGetColumnName(c));
    ImGui::PopFont();
    ImGui::PopStyleColor();
  }
  if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
    if (specs->SpecsDirty && specs->SpecsCount > 0) {
      const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
      switch (s.ColumnIndex) {
        case 0:
          sort_ = SortBy::Name;
          break;
        case 2:
          sort_ = SortBy::Triangles;
          break;
        case 3:
          sort_ = SortBy::Size;
          break;
        case 4:
          sort_ = SortBy::Added;
          break;
        default:
          break;
      }
      sortDesc_ = s.SortDirection == ImGuiSortDirection_Descending;
      uiStateDirtyAt_ = ImGui::GetTime();
      specs->SpecsDirty = false;
    }
  }

  const float rowH = px(36);
  if (scrollToFocus_) {
    scrollToFocus_ = false;
    auto it = std::find(visible_.begin(), visible_.end(), focused_);
    if (it != visible_.end()) {
      const float top = static_cast<float>(it - visible_.begin()) * rowH;
      const float scroll = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight() - rowH * 2;
      if (top < scroll) ImGui::SetScrollY(top);
      else if (top + rowH > scroll + viewH) ImGui::SetScrollY(top + rowH - viewH);
    }
  }
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible_.size()), rowH);
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const int64_t id = visible_[i];
      const Asset* a = lib_.find(id);
      if (!a) continue;
      ImGui::TableNextRow(0, rowH);
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(static_cast<int>(id));
      const bool sel = isSelected(id);
      const ImVec2 rp = ImGui::GetCursorScreenPos();
      ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(34, 34, 34, 255));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(26, 26, 26, 255));
      if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0, rowH - px(8)))) {
        clickSelect(id, i);
      }
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openWithDefaultApp(a->path);
      beginDragAssets(id);
      assetContextMenu(id);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      // Small picture, then the name.
      const float th = rowH - px(10);
      const ImVec2 t0(rp.x, rp.y + (rowH - px(8) - th) * 0.5f);
      dl->AddRectFilled(t0, ImVec2(t0.x + th, t0.y + th), IM_COL32(17, 17, 17, 255), px(4));
      GLuint tex = (!a->needsAnalysis() && a->error.empty()) ? thumbs_->get(id) : 0;
      if (tex) {
        dl->AddImageRounded(static_cast<ImTextureID>(tex), t0, ImVec2(t0.x + th, t0.y + th), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, px(4));
      } else {
        drawIcon(dl, a->error.empty() ? Icon::Box : Icon::Warning, ImVec2(t0.x + th * 0.5f, t0.y + th * 0.5f), px(12),
                 col::text3);
      }
      const float nameX = t0.x + th + px(10);
      const float colW = ImGui::GetContentRegionAvail().x;
      textClipped(dl, fonts.sans, textSize(), ImVec2(nameX, rp.y + (rowH - px(8) - textSize()) * 0.5f),
                  a->missing ? col::text3 : col::text, a->name.c_str(), colW - (nameX - rp.x));
      auto monoCell = [&](int column, const std::string& text, ImU32 color = col::text2) {
        ImGui::TableSetColumnIndex(column);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        textClipped(dl, fonts.mono, px(12.5f), ImVec2(cp.x, rp.y + (rowH - px(8) - px(12.5f)) * 0.5f), color,
                    text.c_str(), ImGui::GetContentRegionAvail().x);
      };
      monoCell(1, upper(a->ext));
      monoCell(2, a->triangles >= 0 ? formatCount(a->triangles) : (a->error.empty() ? "..." : "error"));
      monoCell(3, formatBytes(a->size));
      monoCell(4, formatDate(a->added), col::text3);
      const size_t slash = a->path.find_last_of('\\');
      monoCell(5, a->missing ? "missing: " + a->path : (slash == std::string::npos ? a->path : a->path.substr(0, slash)),
               col::text3);
      ImGui::PopID();
    }
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();
}

void App::drawEmptyState(bool libraryEmpty) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (libraryEmpty) {
    // A big drop area: the first thing to do is bring models in.
    const ImVec2 b0(p0.x, p0.y), b1(p0.x + avail.x, p0.y + avail.y - px(4));
    const float dash = px(8);
    for (float x = b0.x + px(10); x < b1.x - px(10); x += dash * 2) {
      dl->AddLine(ImVec2(x, b0.y), ImVec2(std::min(x + dash, b1.x - px(10)), b0.y), col::bg4);
      dl->AddLine(ImVec2(x, b1.y), ImVec2(std::min(x + dash, b1.x - px(10)), b1.y), col::bg4);
    }
    for (float y = b0.y + px(10); y < b1.y - px(10); y += dash * 2) {
      dl->AddLine(ImVec2(b0.x, y), ImVec2(b0.x, std::min(y + dash, b1.y - px(10))), col::bg4);
      dl->AddLine(ImVec2(b1.x, y), ImVec2(b1.x, std::min(y + dash, b1.y - px(10))), col::bg4);
    }
    const ImVec2 c((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f - px(40));
    dl->AddRectFilled(ImVec2(c.x - px(30), c.y - px(30)), ImVec2(c.x + px(30), c.y + px(30)), col::bg2, px(12));
    dl->AddRect(ImVec2(c.x - px(30), c.y - px(30)), ImVec2(c.x + px(30), c.y + px(30)), col::line, px(12));
    drawIcon(dl, Icon::Box, c, px(28), col::text2);
    auto centered = [&](ImFont* f, float size, float y, ImU32 color, const char* text) {
      const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, text);
      dl->AddText(f, size, ImVec2(c.x - ts.x * 0.5f, y), color, text);
    };
    centered(fonts.sans, px(17), c.y + px(48), col::text, "Your library is empty");
    centered(fonts.sans, textSize(), c.y + px(76), col::text2, "Drop 3D files or whole folders anywhere in this window");
    const float bw = px(132);
    ImGui::SetCursorScreenPos(ImVec2(c.x - bw - px(5), c.y + px(110)));
    if (button("Add Folder", true, bw, Icon::FolderPlus, true)) {
      const std::string dir = pickFolderDialog(window_, "Add Folder");
      if (!dir.empty()) importPaths({dir});
    }
    ImGui::SetCursorScreenPos(ImVec2(c.x + px(5), c.y + px(110)));
    if (button("Add Files", false, bw, Icon::Import, true)) {
      std::vector<std::string> files = openModelFilesDialog(window_);
      if (!files.empty()) importPaths(files);
    }
    centered(fonts.mono, px(12), c.y + px(156), col::text3, "FBX  OBJ  GLTF  GLB  STL");
    return;
  }
  const ImVec2 c(p0.x + avail.x * 0.5f, p0.y + avail.y * 0.4f);
  const char* title = "Nothing here";
  const char* line = "";
  if (search_[0] || !format_.empty()) {
    title = "No models match";
    line = "Try another search or format";
  } else if (source_.kind == SourceKind::Collection) {
    title = "This collection is empty";
    line = "Drag models onto it in the sidebar, or drop files here while it's open";
  } else if (source_.kind == SourceKind::Recent) {
    line = "Nothing was added in the last 7 days";
  }
  drawIcon(dl, Icon::Search, ImVec2(c.x, c.y - px(30)), px(24), col::text3);
  const ImVec2 ts = fonts.sans->CalcTextSizeA(px(15), FLT_MAX, 0, title);
  dl->AddText(fonts.sans, px(15), ImVec2(c.x - ts.x * 0.5f, c.y), col::text, title);
  const ImVec2 ls = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, line);
  dl->AddText(fonts.sans, textSize(), ImVec2(c.x - ls.x * 0.5f, c.y + px(26)), col::text3, line);
  if (search_[0] || !format_.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(c.x - px(60), c.y + px(60)));
    if (button("Clear Filters", false, px(120))) {
      search_[0] = 0;
      format_.clear();
    }
  }
}
