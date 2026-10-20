// The window's frame: top bar, status bar, notes, dialogs and settings.
#include "app.h"  // (first: windows.h before GLFW)

#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

#include "file_dialog.h"
#include "shell_integration.h"
#include "util.h"

using namespace ui;

void App::drawBrand(ImDrawList* dl, ImVec2 pos, float height) {
  const float cy = pos.y + height * 0.5f;
  const float mark = px(22);
  const ImVec2 m0(pos.x, cy - mark * 0.5f);
  dl->AddRectFilled(m0, ImVec2(m0.x + mark, m0.y + mark), IM_COL32(236, 236, 236, 255), px(5));
  const ImU32 ink = IM_COL32(12, 12, 12, 255);
  const float t = std::max(1.5f, px(1.8f));
  dl->AddLine(ImVec2(m0.x + px(5), m0.y + px(7)), ImVec2(m0.x + px(10), m0.y + px(11)), ink, t);
  dl->AddLine(ImVec2(m0.x + px(10), m0.y + px(11)), ImVec2(m0.x + px(5), m0.y + px(15)), ink, t);
  dl->AddLine(ImVec2(m0.x + px(12), m0.y + px(15.5f)), ImVec2(m0.x + px(17), m0.y + px(15.5f)), ink, t);
  const float titleSize = px(13.5f);
  dl->AddText(fonts.sans, titleSize, ImVec2(m0.x + mark + px(10), cy - titleSize * 0.55f), col::text, "CREATIVE CELL");
}

float App::drawAppActions(float rx, float cy) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float btn = px(28);
  // Account: a placeholder circle until accounts exist.
  rx -= btn;
  ImGui::SetCursorScreenPos(ImVec2(rx, cy - btn * 0.5f));
  {
    const ImVec2 c(rx + btn * 0.5f, cy);
    ImGui::InvisibleButton("##account", ImVec2(btn, btn));
    const bool hov = ImGui::IsItemHovered();
    dl->AddCircle(c, btn * 0.42f, hov ? col::bg4 : col::line, 0, 1.0f);
    drawIcon(dl, Icon::User, c, px(13), col::text3);
    if (hov) tooltip("Account: coming in a later version");
  }
  rx -= btn + px(4);
  ImGui::SetCursorScreenPos(ImVec2(rx, cy - btn * 0.5f));
  if (iconButton("##settings", Icon::Gear, btn, settingsOpen_, "Settings  (Ctrl+,)")) settingsOpen_ = true;

  const char* importLabel = "Import";
  const float iw = fonts.sans->CalcTextSizeA(textSize(), FLT_MAX, 0, importLabel).x + px(20) + px(24);
  rx -= iw + px(8);
  ImGui::SetCursorScreenPos(ImVec2(rx, cy - px(15)));
  if (button(importLabel, true, iw, Icon::Import, true)) ImGui::OpenPopup("##import");
  ImGui::SetNextWindowPos(ImVec2(rx + iw, cy + px(20)), ImGuiCond_Always, ImVec2(1, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(6)));
  if (ImGui::BeginPopup("##import")) {
    if (ImGui::MenuItem("Add files...", "Ctrl+I")) {
      std::vector<std::string> files = openModelFilesDialog(window_);
      if (!files.empty()) importPaths(files);
    }
    if (ImGui::MenuItem("Add folder...")) {
      const std::string dir = pickFolderDialog(window_, "Add Folder");
      if (!dir.empty()) importPaths({dir});
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy into library folder", nullptr, copyOnImport_)) {
      copyOnImport_ = !copyOnImport_;
      uiStateDirtyAt_ = ImGui::GetTime();
    }
    if (ImGui::IsItemHovered()) {
      tooltip(copyOnImport_ ? "Imported files are copied into the library folder first"
                            : "Files are indexed where they are (nothing is copied)");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::TextUnformatted("Or drop files and folders on the window");
    ImGui::PopStyleColor();
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
  return rx;
}

void App::drawStatusBar(ImVec2 pos, ImVec2 size) {
  beginPanel("##status", pos, size, col::bg1);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddLine(pos, ImVec2(pos.x + size.x, pos.y), col::line);
  const float fs = px(11.5f);
  const float ty = pos.y + (size.y - fs) * 0.5f;
  char buf[160];
  std::snprintf(buf, sizeof buf, "%s models", formatCount(countAll_).c_str());
  float x = pos.x + px(14);
  dl->AddText(fonts.mono, fs, ImVec2(x, ty), col::text3, buf);
  x += fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, buf).x + px(16);
  if (!selection_.empty()) {
    std::snprintf(buf, sizeof buf, "%s selected", formatCount(static_cast<int64_t>(selection_.size())).c_str());
    dl->AddText(fonts.mono, fs, ImVec2(x, ty), col::text2, buf);
    x += fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, buf).x + px(16);
  }
  // Background work.
  std::string activity;
  if (scansInFlight_ > 0) {
    activity = "Scanning folders";
  } else if (countPending_ > 0 || !analyzing_.empty()) {
    activity = "Building thumbnails  " + formatCount(std::max<int64_t>(countPending_, 1)) + " left";
  }
  if (!activity.empty()) {
    const float r = px(3);
    const float phase = static_cast<float>(ImGui::GetTime() * 3.0);
    const ImVec2 c(x + r, pos.y + size.y * 0.5f);
    dl->AddCircleFilled(c, r, IM_COL32(220, 220, 220, static_cast<int>(120 + 120 * std::sin(phase))));
    dl->AddText(fonts.mono, fs, ImVec2(x + px(12), ty), col::text2, activity.c_str());
  }
  std::snprintf(buf, sizeof buf, "v%s", kAppVersion);
  const float vw = fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, buf).x;
  dl->AddText(fonts.mono, fs, ImVec2(pos.x + size.x - vw - px(14), ty), col::text3, buf);
  endPanel();
}

void App::drawToasts() {
  const double now = ImGui::GetTime();
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.until < now; }),
                toasts_.end());
  const ImGuiIO& io = ImGui::GetIO();
  float y = io.DisplaySize.y - px(26) - px(14);
  for (int i = static_cast<int>(toasts_.size()) - 1; i >= 0; --i) {
    Toast& t = toasts_[i];
    const bool undo = !t.undoUnhide.empty();
    const float fs = textSize();
    const float tw = fonts.sans->CalcTextSizeA(fs, FLT_MAX, 0, t.text.c_str()).x;
    const float w = tw + px(32) + (undo ? px(64) : 0.0f), h = px(38);
    const ImVec2 p((io.DisplaySize.x - w) * 0.5f, y - h);
    char id[32];
    std::snprintf(id, sizeof id, "##toast%d", i);
    ImGui::SetNextWindowPos(p);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 24, 24, 250));
    ImGui::PushStyleColor(ImGuiCol_Border, col::bg4);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(fonts.sans, fs, ImVec2(p.x + px(16), p.y + (h - fs) * 0.5f), col::text, t.text.c_str());
    if (undo) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(68), p.y + px(4)));
      if (chip("Undo", false, h - px(8))) {
        lib_.setHidden(t.undoUnhide, false);
        t.until = 0.0;
      }
    }
    ImGui::End();
    y -= h + px(8);
  }
}

void App::drawModals() {
  // New collection (optionally with assets going into it).
  if (newCollectionOpen_) {
    ImGui::OpenPopup("New Collection");
    newCollectionOpen_ = false;
  }
  if (renameCollection_ && !ImGui::IsPopupOpen("Rename Collection")) ImGui::OpenPopup("Rename Collection");
  if (!confirmDelete_.empty() && !ImGui::IsPopupOpen("Delete Files")) ImGui::OpenPopup("Delete Files");

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  auto modal = [&](const char* name) {
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(380), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(16)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(18, 18, 18, 255));
    const bool open = ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
      ImGui::PushFont(nullptr, px(15));
      ImGui::TextUnformatted(name);
      ImGui::PopFont();
      ImGui::Spacing();
    }
    return open;
  };
  auto nameField = [&](bool appearing) {
    if (appearing) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    return ImGui::InputTextWithHint("##name", "Name", nameBuf_, sizeof nameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
  };
  auto buttons = [&](const char* ok, bool primaryOk) {
    ImGui::Spacing();
    const float bw = px(96);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw - px(18) - px(8));
    bool cancel = button("Cancel", false, bw);
    ImGui::SameLine(0, px(8));
    bool accept = button(ok, primaryOk, bw);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel = true;
    return accept ? 1 : cancel ? -1 : 0;
  };

  if (modal("New Collection")) {
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing) nameBuf_[0] = 0;
    const bool enter = nameField(appearing);
    if (!newCollectionAssets_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
      ImGui::Text("%zu selected model(s) will be added to it", newCollectionAssets_.size());
      ImGui::PopStyleColor();
    }
    const int r = buttons("Create", true);
    if ((r > 0 || enter) && nameBuf_[0]) {
      const int64_t id = lib_.createCollection(nameBuf_);
      if (!newCollectionAssets_.empty()) lib_.addToCollection(id, newCollectionAssets_);
      newCollectionAssets_.clear();
      ImGui::CloseCurrentPopup();
    } else if (r < 0) {
      newCollectionAssets_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (modal("Rename Collection")) {
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing) {
      nameBuf_[0] = 0;
      for (const Collection& c : lib_.collections()) {
        if (c.id == renameCollection_) std::snprintf(nameBuf_, sizeof nameBuf_, "%s", c.name.c_str());
      }
    }
    const bool enter = nameField(appearing);
    const int r = buttons("Rename", true);
    if ((r > 0 || enter) && nameBuf_[0]) {
      lib_.renameCollection(renameCollection_, nameBuf_);
      renameCollection_ = 0;
      ImGui::CloseCurrentPopup();
    } else if (r < 0) {
      renameCollection_ = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (modal("Delete Files")) {
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
    if (confirmDelete_.size() == 1) {
      const Asset* a = lib_.find(confirmDelete_[0]);
      ImGui::Text("Move \"%s.%s\" to the Recycle Bin?", a ? a->name.c_str() : "", a ? a->ext.c_str() : "");
    } else {
      ImGui::Text("Move %zu files to the Recycle Bin?", confirmDelete_.size());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::TextUnformatted("The files are deleted from disk; they can be restored from the Recycle Bin.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    const int r = buttons("Delete", true);
    if (r > 0) {
      deleteFiles(confirmDelete_);
      confirmDelete_.clear();
      ImGui::CloseCurrentPopup();
    } else if (r < 0) {
      confirmDelete_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::drawSettings() {
  if (!settingsOpen_) return;
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size(std::min(px(640), io.DisplaySize.x - px(40)), std::min(px(600), io.DisplaySize.y - px(80)));
  // The rest of the window dimmed; a click there closes.
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 150));
  ImGui::Begin("##settingsDim", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoNav);
  ImGui::PopStyleColor();
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
  ImGui::InvisibleButton("##dimClick", io.DisplaySize);
  if (ImGui::IsItemClicked()) settingsOpen_ = false;
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) * 0.5f, (io.DisplaySize.y - size.y) * 0.5f));
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(22), px(18)));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 16, 16, 255));
  ImGui::PushStyleColor(ImGuiCol_Border, col::bg4);
  ImGui::Begin("##settings", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
  const ImVec2 wp = ImGui::GetWindowPos();
  ImGui::PushFont(nullptr, px(17));
  ImGui::TextUnformatted("Settings");
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(wp.x + size.x - px(22) - px(26), wp.y + px(14)));
  if (iconButton("##closeSettings", Icon::Close, px(26), false, "Close  (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    settingsOpen_ = false;
  }
  ImGui::SetCursorScreenPos(ImVec2(wp.x + px(22), wp.y + px(52)));
  ImGui::BeginChild("##settingsBody", ImVec2(0, 0), 0);

  auto label = [](const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text3));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
  };
  auto gapAndHeader = [&](const char* title) {
    ImGui::Dummy(ImVec2(0, px(10)));
    sectionHeader(title);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, px(4)));
  };

  gapAndHeader("WATCHED FOLDERS");
  label("Folders are indexed where they are; new and changed files are picked up on rescan and at start.");
  ImGui::Dummy(ImVec2(0, px(4)));
  int64_t removeId = 0;
  for (const WatchedFolder& f : lib_.folders()) {
    ImGui::PushID(static_cast<int>(f.id));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + px(36)), col::bg2, px(6));
    drawIcon(dl, Icon::Folder, ImVec2(p.x + px(18), p.y + px(18)), px(14), col::text2);
    int n = 0;
    for (const auto& [fid, c] : folderCounts_) {
      if (fid == f.id) n = c;
    }
    const std::string count = formatCount(n);
    textClipped(dl, fonts.sans, textSize(), ImVec2(p.x + px(34), p.y + px(10)), col::text, f.path.c_str(),
                w - px(34) - px(150));
    dl->AddText(fonts.mono, monoSize(), ImVec2(p.x + w - px(140), p.y + px(11)), col::text3, count.c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(96), p.y + px(4)));
    if (iconButton("##rescan", Icon::Refresh, px(28), false, "Rescan")) scanFolder(f.id);
    ImGui::SameLine(0, px(2));
    if (iconButton("##reveal", Icon::Reveal, px(28), false, "Show in Explorer")) revealInExplorer(f.path);
    ImGui::SameLine(0, px(2));
    if (iconButton("##remove", Icon::Close, px(28), false, "Stop watching (files stay)")) removeId = f.id;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(40)));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
  }
  if (removeId) lib_.removeFolder(removeId);
  if (button("Add Folder...", false, 0, Icon::FolderPlus, true)) {
    const std::string dir = pickFolderDialog(window_, "Add Folder");
    if (!dir.empty()) {
      const bool keep = copyOnImport_;
      copyOnImport_ = false;  // a watched folder is always indexed in place
      importPaths({dir});
      copyOnImport_ = keep;
    }
  }
  ImGui::SameLine();
  if (button("Rescan All", false, 0, Icon::Refresh, true)) scanAll();

  gapAndHeader("IMPORT");
  if (ImGui::Checkbox("Copy imported files into the library folder", &copyOnImport_)) uiStateDirtyAt_ = ImGui::GetTime();
  label("Off: dropped files stay where they are and are indexed in place.");
  label("On: they're copied into the library folder first (a glTF's .bin and textures, an OBJ's .mtl go along).");
  ImGui::Dummy(ImVec2(0, px(4)));
  label("Library folder");
  {
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w - px(100) - px(8));
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", libraryDir_.c_str());
    ImGui::InputText("##libdir", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine(0, px(8));
    if (button("Change...", false, px(100))) {
      const std::string dir = pickFolderDialog(window_, "Library Folder");
      if (!dir.empty()) {
        libraryDir_ = normalizePath(dir);
        uiStateDirtyAt_ = ImGui::GetTime();
      }
    }
  }

  gapAndHeader("THUMBNAILS & CACHE");
  label(("Data and thumbnails: " + pathU8(dataDir())).c_str());
  ImGui::Dummy(ImVec2(0, px(2)));
  if (button("Rebuild Thumbnails", false, 0, Icon::Refresh, true)) {
    thumbs_->clearAll();
    lib_.resetAnalysis();
    toast("Rebuilding every thumbnail");
  }
  ImGui::SameLine();
  if (button("Open Data Folder", false, 0, Icon::Reveal, true)) openWithDefaultApp(pathU8(dataDir()));

  gapAndHeader("SHORTCUTS");
  const char* keys[][2] = {
      {"Ctrl+F", "Search"},
      {"Ctrl+I", "Add files"},
      {"Ctrl+A / Esc", "Select all / clear"},
      {"Arrows, Shift+Arrows", "Move / extend the selection"},
      {"Enter", "Open in the default app"},
      {"Ctrl+C", "Copy path(s)"},
      {"Delete", "Remove from library (files stay)"},
      {"Shift+Delete", "Delete files (Recycle Bin)"},
      {"F", "Frame the model"},
      {"1 2 3 4", "Solid / Solid + edges / Wire / Points"},
      {"G", "Floor grid"},
      {"F11 / Esc", "3D view over the whole window / back"},
      {"Left / Right", "File tab: previous / next model in the folder"},
      {"Ctrl+Tab / Ctrl+W", "Next tab / close the file tab (Ctrl+1: Library)"},
      {"Ctrl+[ / Ctrl+]", "Hide / show the sidebar / the inspector"},
      {"Viewer: LMB / RMB / wheel", "Orbit / pan / zoom; double-click frames"},
  };
  if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingStretchProp)) {
    for (const auto& k : keys) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      mono("%s", k[0]);
      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::text2));
      ImGui::TextUnformatted(k[1]);
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }

  gapAndHeader("WINDOWS");
  {
    const bool on = shellIntegrationRegistered();
    label(on ? "Creative Cell opens 3D files from Explorer and draws their thumbnails there."
             : "Open FBX / OBJ / glTF / GLB / STL from Explorer in Creative Cell's viewer, with thumbnails.");
    label("Files opened from Explorer are only viewed: they don't go into the library.");
    ImGui::Dummy(ImVec2(0, px(2)));
    if (on && shellIntegrationByInstaller()) {
      label("Set up by the installer for every user (uninstalling removes it).");
    } else if (!on) {
      if (button("Use in Explorer", true, 0, Icon::Check, true)) {
        std::string error;
        if (registerShellIntegration(error)) toast("Registered: now pick Creative Cell in \"Open with\" once");
        else toast(error);
      }
    } else {
      if (button("Remove from Explorer", false, 0, Icon::Close, true)) {
        unregisterShellIntegration();
        toast("Removed from Explorer");
      }
    }
    ImGui::SameLine();
    if (button("Default Apps...", false, 0, Icon::External, true)) openDefaultAppsSettings();
    ImGui::Dummy(ImVec2(0, px(2)));
    label("To open on double-click: right-click a model > Open with > Choose another app >");
    label("Creative Cell > Always (once per format). Windows doesn't let programs set this themselves.");
    label("Thumbnails Explorer already cached change when the file does (or: Disk Cleanup > Thumbnails).");
  }

  gapAndHeader("LATER");
  label("Quick export to Blender / Unreal / Unity, the marketplace and accounts have their places reserved");
  label("and come in later versions.");

  gapAndHeader("ABOUT");
  label(("Creative Cell " + std::string(kAppVersion) + "  -  C++ / OpenGL / Dear ImGui / SQLite").c_str());
  ImGui::Dummy(ImVec2(0, px(10)));
  ImGui::EndChild();
  ImGui::End();
}
