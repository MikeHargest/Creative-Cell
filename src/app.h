#pragma once

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "camera.h"
#include "jobs.h"
#include "library.h"
#include "renderer.h"
#include "thumbs.h"
#include "ui_style.h"

struct GLFWwindow;

constexpr const char* kAppVersion = CC_VERSION;

// What the grid shows: the whole library, a part of it, or a collection.
enum class SourceKind { All, Recent, Missing, Folder, Collection };
struct Source {
  SourceKind kind = SourceKind::All;
  int64_t id = 0;   // folder / collection
  std::string sub;  // folder: a subfolder (absolute) or "" for all of it
  bool operator==(const Source& o) const { return kind == o.kind && id == o.id && sub == o.sub; }
  bool operator!=(const Source& o) const { return !(*this == o); }
};

enum class SortBy { Added, Name, Size, Triangles };
enum class ViewMode { Grid, List };

// The subfolders of a watched folder that hold models (for the sidebar).
struct FolderNode {
  std::string name, path;
  int count = 0;  // models in it and below
  std::vector<FolderNode> children;
};

// A note at the bottom of the window for a few seconds; with Undo for removals.
struct Toast {
  std::string text;
  double until = 0.0;
  std::vector<int64_t> undoUnhide;
};

struct LaunchOptions {
  std::string shotPath;  // screenshot to this PNG and quit (checks)
  int shotFrames = 120;
  std::vector<std::string> importPaths;
  std::vector<std::string> openPaths;  // files opened from Explorer: viewer only, not imported
  int selectIndex = -1;  // select the n-th visible asset at start
};

class App {
 public:
  bool init(GLFWwindow* window, const LaunchOptions& opts);
  void shutdown();
  void frame();
  void onDrop(const std::vector<std::string>& paths) { dropped_.insert(dropped_.end(), paths.begin(), paths.end()); }
  // From another launch (Explorer's Open): files to show in tabs, folders /
  // files to import.
  void openFiles(const std::vector<std::string>& paths) { pendingOpen_.insert(pendingOpen_.end(), paths.begin(), paths.end()); }
  void importLater(const std::vector<std::string>& paths) {
    pendingImport_.insert(pendingImport_.end(), paths.begin(), paths.end());
  }
  bool wantsQuit() const { return quit_; }
  // Window place / size, kept between runs.
  void saveWindowPlace();
  void restoreWindowPlace();

 private:
  // --- work ---
  void importPaths(const std::vector<std::string>& paths);
  void scanFolder(int64_t folderId);
  void scanAll();
  void pumpAnalysis();
  void renderPendingThumbnails();
  void loadViewer(int64_t assetId);
  // A file outside the library in the viewer (the viewer-only window).
  void openInViewer(const std::string& path);
  void startViewerLoad(const std::string& path, int64_t assetId, int64_t mtime);
  void viewerModeFrame(float top);
  void drawViewerModeBar(ImVec2 pos, ImVec2 size);
  void stepSibling(int delta);
  // Tabs: the library and the open files.
  void drawTabBar(ImVec2 pos, ImVec2 size);
  void openTabs(const std::vector<std::string>& paths);
  void activateTab(int index);  // -1: the library
  void closeTab(int index);
  void tabShortcuts();  // Ctrl+W, Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+1 (library)
  // The camera back on the whole model (its bounding sphere fills the view).
  void frameViewer();
  void removeFromLibrary(const std::vector<int64_t>& ids);
  void deleteFiles(const std::vector<int64_t>& ids);
  void refreshVisible();
  const std::vector<FolderNode>& folderTree(int64_t folderId);
  bool inSource(const Asset& a, const Source& s) const;
  void toast(const std::string& text, std::vector<int64_t> undoUnhide = {});
  void saveUiState();

  // --- selection ---
  void selectOnly(int64_t id);
  void clickSelect(int64_t id, int visibleIndex);
  void selectAll();
  void clearSelection();
  std::vector<int64_t> selectedIds() const;  // in visible order
  bool isSelected(int64_t id) const { return selection_.count(id) != 0; }

  // --- UI (ui_*.cpp) ---
  // The mark and name (top of the sidebar).
  void drawBrand(ImDrawList* dl, ImVec2 pos, float height);
  // Import, settings, account, placed leftwards from `rx` in the current
  // window (the browser's toolbar); returns where they end.
  float drawAppActions(float rx, float cy);
  void drawSidebar(ImVec2 pos, ImVec2 size);
  void drawBrowser(ImVec2 pos, ImVec2 size);
  void drawBrowserToolbar(float width);
  void drawGrid();
  void drawList();
  void drawEmptyState(bool libraryEmpty);
  void drawInspector(ImVec2 pos, ImVec2 size);
  void drawViewer(ImVec2 pos, ImVec2 size);
  void drawAssetInfo(const Asset& a);
  void drawMultiInfo(const std::vector<int64_t>& ids);
  void drawStatusBar(ImVec2 pos, ImVec2 size);
  void drawSettings();
  void drawToasts();
  void drawModals();
  void assetContextMenu(int64_t clickedId);
  void collectionMenuItems(const std::vector<int64_t>& ids);
  bool splitter(const char* id, ImVec2 pos, ImVec2 size, bool vertical, float& value, float sign, float lo, float hi);
  void handleShortcuts();
  void beginDragAssets(int64_t id);
  // Models dragged past the window's edge go on as files to other programs.
  void dragOutIfLeaving();

  GLFWwindow* window_ = nullptr;
  LaunchOptions opts_;
  int frameCount_ = 0;
  bool quit_ = false;

  Library lib_;
  std::unique_ptr<Jobs> jobs_;
  Renderer renderer_;
  std::unique_ptr<ThumbCache> thumbs_;

  // Browsing.
  Source source_;
  char search_[256] = "";
  std::string format_;  // "" all, "fbx", "obj", "gltf" (glTF + GLB), "stl"
  SortBy sort_ = SortBy::Added;
  bool sortDesc_ = true;
  ViewMode viewMode_ = ViewMode::Grid;
  float tileSize_ = 168.0f;  // px at 100 %
  std::vector<int64_t> visible_;
  uint64_t visibleKey_ = 0;
  std::string visibleSig_;
  int gridColumns_ = 1;
  bool scrollToFocus_ = false;
  bool focusSearch_ = false;

  // Counts for the sidebar (updated with the library).
  int countAll_ = 0, countRecent_ = 0, countMissing_ = 0, countPending_ = 0;
  std::vector<std::pair<int64_t, int>> folderCounts_;
  uint64_t countsRev_ = 0;
  std::vector<std::pair<int64_t, std::vector<FolderNode>>> folderTrees_;
  uint64_t treesRev_ = 0;
  std::unordered_set<std::string> expanded_;  // sidebar folder nodes open

  // Selection: a set, the focused one (inspector, keyboard) and the anchor
  // for Shift ranges.
  std::unordered_set<int64_t> selection_;
  int64_t focused_ = 0;
  int64_t anchor_ = 0;

  // Analysis (load + stats + thumbnail) in the background.
  std::unordered_set<int64_t> analyzing_;
  struct PendingThumb {
    int64_t id;
    std::shared_ptr<RenderMesh> mesh;
  };
  std::vector<PendingThumb> thumbQueue_;
  int scansInFlight_ = 0;

  // The 3D view of the focused asset.
  struct Viewer {
    int64_t assetId = 0;
    uint64_t token = 0;
    bool loading = false;
    std::string error;
    GpuMesh mesh;
    Camera camera;
    RenderTarget target;
    Shading shading = Shading::Solid;
    bool grid = true;
    bool turntable = false;
    int hoverPart = -1, soloPart = -1;
    float floorY = 0.0f, gridStep = 1.0f;
    double loadStarted = 0.0;
    float aspect = 1.0f;  // width / height of the view, last frame
    std::string path;     // the file shown
    ModelStats stats;     // from its load
    bool statsValid = false;
    int64_t fileSize = 0;
  } viewer_;

  // Layout (px at 100 %), kept between runs.
  float sidebarW_ = 248.0f;
  float inspectorW_ = 400.0f;
  float viewerFrac_ = 0.52f;
  bool sidebarOpen_ = true, inspectorOpen_ = true;
  bool viewerMax_ = false;  // the 3D view over the whole window (F11)
  // Files opened from Explorer, each a tab next to the Library tab (one
  // window: a second launch hands its files over). A file tab is just the
  // viewer — nothing goes into the library unless asked ("Add to Library");
  // the arrows walk the models in the file's folder.
  std::vector<std::string> tabs_;
  int activeTab_ = -1;            // -1: the library
  bool viewerOnly_ = false;       // a file tab is shown (this frame)
  bool libraryStarted_ = false;   // folders scanned (once the Library tab is first shown)
  std::vector<std::string> pendingOpen_, pendingImport_;
  std::string siblingsDir_;
  std::vector<std::string> siblings_;

  // Import.
  bool copyOnImport_ = false;
  std::string libraryDir_;
  std::vector<std::string> dropped_;
  bool dropHover_ = false;

  // Windows and dialogs.
  bool settingsOpen_ = false;
  bool newCollectionOpen_ = false;
  std::vector<int64_t> newCollectionAssets_;  // added to it once made
  char nameBuf_[128] = "";
  int64_t renameCollection_ = 0;
  std::vector<int64_t> confirmDelete_;
  std::vector<Toast> toasts_;
  std::string marketplaceNote_;
  double uiStateDirtyAt_ = 0.0;
};
