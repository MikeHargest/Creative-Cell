#include "app.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

#include "cgltf/cgltf.h"
#include "file_dialog.h"
#include "ole_drag.h"
#include "scanner.h"
#include "util.h"

namespace {
constexpr int64_t kRecentSeconds = 7 * 24 * 3600;

// The files a model needs next to it: a glTF's buffers and pictures, an
// OBJ's material libraries (copied along when importing with copying).
std::vector<std::filesystem::path> companions(const std::filesystem::path& model) {
  std::vector<std::filesystem::path> out;
  const std::string ext = lower(pathU8(model.extension()));
  const std::filesystem::path dir = model.parent_path();
  auto relative = [&](const std::string& uri) {
    if (uri.empty() || uri.rfind("data:", 0) == 0 || uri.find("://") != std::string::npos) return;
    out.push_back(dir / u8(uri));
  };
  if (ext == ".gltf") {
    std::ifstream f(model, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    cgltf_options o = {};
    cgltf_data* d = nullptr;
    if (cgltf_parse(&o, json.data(), json.size(), &d) == cgltf_result_success) {
      for (cgltf_size i = 0; i < d->buffers_count; ++i) {
        if (d->buffers[i].uri) {
          std::string u = d->buffers[i].uri;
          cgltf_decode_uri(u.data());
          relative(u.c_str());
        }
      }
      for (cgltf_size i = 0; i < d->images_count; ++i) {
        if (d->images[i].uri) {
          std::string u = d->images[i].uri;
          cgltf_decode_uri(u.data());
          relative(u.c_str());
        }
      }
      cgltf_free(d);
    }
  } else if (ext == ".obj") {
    std::ifstream f(model, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("mtllib", 0) == 0 && line.size() > 7) {
        std::string name = line.substr(7);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        relative(name);
      }
    }
  }
  return out;
}

// `name` in `dir`, or "name (2)", "name (3)"... when that's taken.
std::filesystem::path freeName(const std::filesystem::path& dir, const std::filesystem::path& name) {
  std::error_code ec;
  std::filesystem::path p = dir / name;
  for (int i = 2; std::filesystem::exists(p, ec); ++i) {
    std::filesystem::path n = name.stem();
    n += L" (" + std::to_wstring(i) + L")";
    n += name.extension();
    p = dir / n;
  }
  return p;
}

std::string defaultLibraryDir() {
  wchar_t* docs = nullptr;
  std::filesystem::path p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) p = std::filesystem::path(docs);
  CoTaskMemFree(docs);
  if (p.empty()) p = dataDir();
  return pathU8(p / L"Creative Cell Library");
}
}  // namespace

bool App::init(GLFWwindow* window, const LaunchOptions& opts) {
  window_ = window;
  opts_ = opts;
  std::string error;
  if (!lib_.open(dataDir() / L"library.db", error)) {
    MessageBoxW(nullptr, toWide("Couldn't open the library:\n" + error).c_str(), L"Creative Cell", MB_ICONERROR);
    return false;
  }
  if (!renderer_.init()) return false;
  jobs_ = std::make_unique<Jobs>();
  jobs_->onPost = [] { glfwPostEmptyEvent(); };  // a result wakes the main loop
  thumbs_ = std::make_unique<ThumbCache>(*jobs_, dataDir() / L"thumbs");
  // A thumbnail file gone (cache cleared by hand): made again.
  thumbs_->onMissing = [this](int64_t id) {
    const Asset* a = lib_.find(id);
    if (a && !a->needsAnalysis() && a->error.empty()) {
      lib_.setAnalysisError(id, "", 0);  // analyzed_mtime 0: analyzed again
      thumbs_->forget(id);
    }
  };

  sidebarW_ = lib_.settingFloat("ui.sidebarW", sidebarW_);
  sidebarOpen_ = lib_.settingInt("ui.sidebarOpen", 1) != 0;
  inspectorOpen_ = lib_.settingInt("ui.inspectorOpen", 1) != 0;
  inspectorW_ = lib_.settingFloat("ui.inspectorW", inspectorW_);
  viewerFrac_ = lib_.settingFloat("ui.viewerFrac", viewerFrac_);
  infoFolded_ = lib_.settingInt("ui.infoFolded", 0);
  infoHidden_ = lib_.settingInt("ui.infoHidden", 0) != 0;
  tileSize_ = lib_.settingFloat("ui.tile", tileSize_);
  viewMode_ = static_cast<ViewMode>(lib_.settingInt("ui.view", 0));
  sort_ = static_cast<SortBy>(lib_.settingInt("ui.sort", 0));
  sortDesc_ = lib_.settingInt("ui.sortDesc", 1) != 0;
  viewer_.shading = static_cast<Shading>(lib_.settingInt("viewer.shading", 0));
  viewer_.grid = lib_.settingInt("viewer.grid", 1) != 0;
  viewer_.ao = lib_.settingInt("viewer.ao", 0) != 0;
  copyOnImport_ = lib_.settingInt("import.copy", 0) != 0;
  libraryDir_ = lib_.setting("import.dir", defaultLibraryDir());
  viewer_.camera.resetToIsometricView();
  viewer_.camera.update(10.0f);

  if (!opts_.openPaths.empty()) {
    // Opened from Explorer: the files in tabs; the library isn't scanned
    // until its tab is shown.
    openTabs(opts_.openPaths);
    return true;
  }
  libraryStarted_ = true;
  scanAll();
  if (!opts_.importPaths.empty()) importPaths(opts_.importPaths);
  return true;
}

void App::shutdown() {
  saveUiState();
  saveWindowPlace();
  stopping_ = true;  // (a big file still loading gives up)
  jobs_.reset();  // workers stop first: their results point into the app
  viewer_.mesh.release();
  viewer_.target.release();
  thumbs_.reset();
  renderer_.shutdown();
  lib_.close();
}

void App::saveUiState() {
  lib_.setSetting("ui.sidebarW", std::to_string(sidebarW_));
  lib_.setSetting("ui.sidebarOpen", sidebarOpen_ ? "1" : "0");
  lib_.setSetting("ui.inspectorOpen", inspectorOpen_ ? "1" : "0");
  lib_.setSetting("ui.inspectorW", std::to_string(inspectorW_));
  lib_.setSetting("ui.viewerFrac", std::to_string(viewerFrac_));
  lib_.setSetting("ui.infoFolded", std::to_string(infoFolded_));
  lib_.setSetting("ui.infoHidden", infoHidden_ ? "1" : "0");
  lib_.setSetting("ui.tile", std::to_string(tileSize_));
  lib_.setSetting("ui.view", std::to_string(static_cast<int>(viewMode_)));
  lib_.setSetting("ui.sort", std::to_string(static_cast<int>(sort_)));
  lib_.setSetting("ui.sortDesc", sortDesc_ ? "1" : "0");
  lib_.setSetting("viewer.shading", std::to_string(static_cast<int>(viewer_.shading)));
  lib_.setSetting("viewer.grid", viewer_.grid ? "1" : "0");
  lib_.setSetting("viewer.ao", viewer_.ao ? "1" : "0");
  lib_.setSetting("import.copy", copyOnImport_ ? "1" : "0");
  lib_.setSetting("import.dir", libraryDir_);
  uiStateDirtyAt_ = 0.0;
}

void App::saveWindowPlace() {
  if (!window_) return;
  const bool maxed = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;
  lib_.setSetting("win.max", maxed ? "1" : "0");
  if (!maxed && !glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
    int x, y, w, h;
    glfwGetWindowPos(window_, &x, &y);
    glfwGetWindowSize(window_, &w, &h);
    lib_.setSetting("win.rect", std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                                    std::to_string(h));
  }
}

void App::restoreWindowPlace() {
  int x, y, w, h;
  const std::string rect = lib_.setting("win.rect");
  if (std::sscanf(rect.c_str(), "%d %d %d %d", &x, &y, &w, &h) == 4 && w > 200 && h > 150) {
    // Only onto a screen that's still there.
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int m = 0; m < count; ++m) {
      int mx, my, mw, mh;
      glfwGetMonitorWorkarea(monitors[m], &mx, &my, &mw, &mh);
      if (x + 40 >= mx && x + 40 < mx + mw && y + 10 >= my && y + 10 < my + mh) {
        glfwSetWindowPos(window_, x, y);
        glfwSetWindowSize(window_, w, h);
        break;
      }
    }
  }
  if (lib_.setting("win.max", "1") == "1") glfwMaximizeWindow(window_);
}

// --- import and scanning ---

void App::importPaths(const std::vector<std::string>& paths) {
  std::vector<std::string> folders, files;
  int skipped = 0;
  for (const std::string& raw : paths) {
    const std::string p = normalizePath(raw);
    std::error_code ec;
    const auto st = std::filesystem::status(u8(p), ec);
    if (std::filesystem::is_directory(st)) {
      folders.push_back(p);
    } else if (std::filesystem::is_regular_file(st) && isModelExtension(lower(pathU8(u8(p).extension())))) {
      files.push_back(p);
    } else {
      ++skipped;
    }
  }
  if (folders.empty() && files.empty()) {
    if (skipped) toast("Nothing to import: only FBX, OBJ, glTF, GLB and STL are read");
    return;
  }
  const int64_t intoCollection = source_.kind == SourceKind::Collection ? source_.id : 0;

  auto finish = [this, intoCollection](const std::vector<int64_t>& folderIds, const std::vector<std::string>& filePaths) {
    std::vector<int64_t> added;
    for (const std::string& f : filePaths) {
      FileEntry e;
      if (statFile(f, e)) added.push_back(lib_.addFile(e));
    }
    for (int64_t id : folderIds) scanFolder(id);
    if (intoCollection && !added.empty()) lib_.addToCollection(intoCollection, added);
    if (!added.empty()) {
      refreshVisible();
      selection_.clear();
      for (int64_t id : added) selection_.insert(id);
      focused_ = anchor_ = added.back();
      scrollToFocus_ = true;
    }
    std::string msg;
    if (!added.empty()) msg = "Added " + std::to_string(added.size()) + (added.size() == 1 ? " model" : " models");
    if (!folderIds.empty()) {
      if (!msg.empty()) msg += ", ";
      msg += "watching " + std::to_string(folderIds.size()) + (folderIds.size() == 1 ? " folder" : " folders");
    }
    toast(msg);
  };

  if (!copyOnImport_) {
    std::vector<int64_t> folderIds;
    for (const std::string& f : folders) folderIds.push_back(lib_.addFolder(f));
    finish(folderIds, files);
    return;
  }
  // Copying: into the Library folder (watched), then indexed from there.
  const std::string dest = libraryDir_;
  toast("Copying " + std::to_string(folders.size() + files.size()) + " item(s) into the library folder...");
  jobs_->run([this, dest, folders, files, finish] {
    std::error_code ec;
    const std::filesystem::path d = u8(dest);
    std::filesystem::create_directories(d, ec);
    std::vector<std::string> copied;
    std::string failed;
    for (const std::string& f : folders) {
      const std::filesystem::path target = freeName(d, u8(f).filename());
      std::filesystem::copy(u8(f), target, std::filesystem::copy_options::recursive, ec);
      if (ec) failed = pathU8(u8(f).filename());
    }
    for (const std::string& f : files) {
      const std::filesystem::path src = u8(f);
      const std::filesystem::path target = freeName(d, src.filename());
      if (std::filesystem::copy_file(src, target, ec)) {
        copied.push_back(pathU8(target));
        for (const std::filesystem::path& c : companions(src)) {
          std::filesystem::path rel = c.lexically_relative(src.parent_path());
          std::filesystem::create_directories((d / rel).parent_path(), ec);
          std::filesystem::copy_file(c, d / rel, std::filesystem::copy_options::skip_existing, ec);
        }
      } else {
        failed = pathU8(src.filename());
      }
    }
    jobs_->post([this, dest, copied, failed, finish] {
      const int64_t folderId = lib_.addFolder(dest);
      finish({folderId}, copied);
      if (!failed.empty()) toast("Couldn't copy " + failed);
    });
  });
}

void App::scanFolder(int64_t folderId) {
  const WatchedFolder* f = lib_.folder(folderId);
  if (!f) return;
  ++scansInFlight_;
  const std::string path = f->path;
  jobs_->run([this, folderId, path] {
    std::vector<FileEntry> files = ::scanFolder(path);
    jobs_->post([this, folderId, files = std::move(files)] {
      --scansInFlight_;
      lib_.syncFolder(folderId, files);
    });
  });
}

void App::scanAll() {
  for (const WatchedFolder& f : lib_.folders()) scanFolder(f.id);
  // Files added by themselves: still there?
  std::vector<std::pair<int64_t, std::string>> loose;
  for (const Asset& a : lib_.assets()) {
    if (a.folderId == 0 && !a.hidden) loose.emplace_back(a.id, a.path);
  }
  if (loose.empty()) return;
  jobs_->run([this, loose] {
    std::vector<std::pair<int64_t, FileEntry>> found;
    std::vector<int64_t> gone;
    for (const auto& [id, path] : loose) {
      FileEntry e;
      if (statFile(path, e)) found.emplace_back(id, e);
      else gone.push_back(id);
    }
    jobs_->post([this, found, gone] {
      for (const auto& [id, e] : found) {
        const Asset* a = lib_.find(id);
        if (a && (a->size != e.size || a->mtime != e.mtime || a->missing)) lib_.addFile(e);
      }
      // (Marked missing, not forgotten: a drive may just be unplugged.)
      for (int64_t id : gone) lib_.setMissing(id, true);
    });
  });
}

void App::pumpAnalysis() {
  // A few files at a time, and only so many bytes of them: a big FBX takes
  // several times its size in memory while it loads. One always goes,
  // however big.
  constexpr int64_t kBytesInFlight = 768ll << 20;
  const int limit = std::max(2, jobs_->threads() / 2);
  if (static_cast<int>(analyzing_.size()) >= limit || thumbQueue_.size() >= 3) return;
  int64_t inFlight = 0;
  for (const auto& [id, bytes] : analyzing_) inFlight += bytes;
  auto start = [this, &inFlight](const Asset& a) {
    analyzing_[a.id] = a.size;
    inFlight += a.size;
    const int64_t id = a.id, mtime = a.mtime;
    const std::string path = a.path;
    jobs_->run([this, id, mtime, path] {
      Model model;
      std::string error;
      auto mesh = std::make_shared<RenderMesh>();
      const bool ok = loadModel(path, model, error, [this] { return stopping_.load(); });
      ModelStats stats;
      if (ok) {
        *mesh = buildRenderMesh(model);
        stats = modelStats(model, *mesh);
      }
      jobs_->post([this, id, mtime, ok, error, stats, mesh] {
        analyzing_.erase(id);
        if (!lib_.find(id) || error == kCancelled) return;
        if (!ok) {
          lib_.setAnalysisError(id, error, mtime);
          thumbs_->forget(id);
          return;
        }
        lib_.setAnalysis(id, stats, mtime);
        thumbQueue_.push_back({id, mesh});
      });
    });
  };
  auto eligible = [&](const Asset* a) {
    return a && !a->hidden && a->needsAnalysis() && !analyzing_.count(a->id) &&
           (analyzing_.empty() || inFlight + a->size <= kBytesInFlight);
  };
  // What's on screen first, then the rest.
  for (int64_t id : visible_) {
    if (static_cast<int>(analyzing_.size()) >= limit) return;
    const Asset* a = lib_.find(id);
    if (eligible(a)) start(*a);
  }
  for (const Asset& a : lib_.assets()) {
    if (static_cast<int>(analyzing_.size()) >= limit) return;
    if (eligible(&a)) start(a);
  }
}

void App::renderPendingThumbnails() {
  const double t0 = glfwGetTime();
  while (!thumbQueue_.empty() && glfwGetTime() - t0 < 0.010) {
    PendingThumb p = std::move(thumbQueue_.front());
    thumbQueue_.erase(thumbQueue_.begin());
    GpuMesh g;
    uploadMesh(*p.mesh, g);
    std::vector<uint8_t> rgba;
    if (renderer_.thumbnail(g, ThumbCache::kSize, rgba)) thumbs_->put(p.id, std::move(rgba));
    g.release();
  }
}

void App::loadViewer(int64_t id) {
  if (id == viewer_.assetId) return;
  viewer_.assetId = id;
  ++viewer_.token;
  viewer_.mesh.release();
  viewer_.error.clear();
  viewer_.hoverPart = viewer_.soloPart = -1;
  viewer_.loading = false;
  viewer_.statsValid = false;
  const Asset* a = lib_.find(id);
  if (!a) return;
  if (a->missing) {
    viewer_.error = "The file isn't there any more.";
    return;
  }
  viewer_.path = a->path;
  startViewerLoad(a->path, id, a->mtime);
}

void App::openInViewer(const std::string& path) {
  viewer_.assetId = -1;  // (a file outside the library)
  ++viewer_.token;
  viewer_.mesh.release();
  viewer_.error.clear();
  viewer_.hoverPart = viewer_.soloPart = -1;
  viewer_.statsValid = false;
  viewer_.path = normalizePath(path);
  FileEntry e;
  viewer_.fileSize = statFile(viewer_.path, e) ? e.size : 0;
  const std::string name = pathU8(u8(viewer_.path).filename());
  glfwSetWindowTitle(window_, (name + "  -  Creative Cell").c_str());
  // The models next to it, for the arrows.
  if (siblingsDir_ != pathU8(u8(viewer_.path).parent_path())) {
    siblingsDir_ = pathU8(u8(viewer_.path).parent_path());
    siblings_.clear();
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(u8(siblingsDir_), ec)) {
      if (f.is_regular_file(ec) && isModelExtension(lower(pathU8(f.path().extension())))) {
        siblings_.push_back(normalizePath(pathU8(f.path())));
      }
    }
    std::sort(siblings_.begin(), siblings_.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
  }
  startViewerLoad(viewer_.path, -1, 0);
}

void App::startViewerLoad(const std::string& path, int64_t id, int64_t mtime) {
  viewer_.loading = true;
  viewer_.loadStarted = glfwGetTime();
  const uint64_t token = viewer_.token;
  jobs_->run(
      [this, token, id, path, mtime] {
        Model model;
        std::string error;
        auto mesh = std::make_shared<RenderMesh>();
        // Another model picked meanwhile: this one gives up halfway.
        const bool ok = loadModel(path, model, error, [this, token] { return stopping_ || viewer_.token != token; });
        ModelStats stats;
        if (ok) {
          *mesh = buildRenderMesh(model);
          stats = modelStats(model, *mesh);
        }
        jobs_->post([this, token, id, mtime, ok, error, stats, mesh] {
          if (token != viewer_.token) return;
          viewer_.loading = false;
          if (!ok) {
            viewer_.error = error;
            return;
          }
          uploadMesh(*mesh, viewer_.mesh);
          viewer_.stats = stats;
          viewer_.statsValid = true;
          const float r = std::max(viewer_.mesh.radius(), 1e-4f);
          frameViewer();
          viewer_.floorY = viewer_.mesh.min.y;
          viewer_.gridStep = niceGridStep(r);
          // The same load serves the library when it wasn't analyzed yet.
          const Asset* a = id > 0 ? lib_.find(id) : nullptr;
          if (a && a->needsAnalysis() && a->mtime == mtime && !analyzing_.count(id)) {
            lib_.setAnalysis(id, stats, mtime);
            std::vector<uint8_t> rgba;
            if (renderer_.thumbnail(viewer_.mesh, ThumbCache::kSize, rgba)) thumbs_->put(id, std::move(rgba));
          }
        });
      },
      true);
}

void App::frameViewer() {
  if (!viewer_.mesh.valid()) return;
  // The bounding sphere fits the narrower of the two view angles (a tall
  // panel is narrow sideways), with a little air. Camera::frame stands
  // 2.2 x the radius it's given off the center.
  const float r = std::max(viewer_.mesh.radius(), 1e-4f);
  const float halfV = glm::radians(35.0f) * 0.5f;
  const float halfH = std::atan(std::tan(halfV) * std::max(viewer_.aspect, 0.1f));
  const float dist = r / std::sin(std::min(halfV, halfH)) * 1.08f;
  viewer_.camera.frame(viewer_.mesh.center(), dist / 2.2f);
}

void App::removeFromLibrary(const std::vector<int64_t>& ids) {
  if (ids.empty()) return;
  lib_.setHidden(ids, true);
  for (int64_t id : ids) selection_.erase(id);
  if (!isSelected(focused_)) focused_ = selection_.empty() ? 0 : *selection_.begin();
  toast("Removed " + std::to_string(ids.size()) + (ids.size() == 1 ? " model" : " models") +
            " from the library (files stay)",
        ids);
}

void App::deleteFiles(const std::vector<int64_t>& ids) {
  std::vector<std::string> paths;
  std::vector<int64_t> found;
  for (int64_t id : ids) {
    if (const Asset* a = lib_.find(id)) {
      paths.push_back(a->path);
      found.push_back(id);
    }
  }
  if (!moveToRecycleBin(paths)) {
    toast("Couldn't move the files to the Recycle Bin");
    return;
  }
  for (int64_t id : found) {
    thumbs_->forget(id);
    selection_.erase(id);
    if (viewer_.assetId == id) loadViewer(0);
  }
  lib_.forget(found);
  if (!isSelected(focused_)) focused_ = 0;
  toast("Moved " + std::to_string(found.size()) + (found.size() == 1 ? " file" : " files") + " to the Recycle Bin");
}

void App::toast(const std::string& text, std::vector<int64_t> undoUnhide) {
  if (text.empty()) return;
  toasts_.push_back({text, ImGui::GetTime() + (undoUnhide.empty() ? 3.5 : 7.0), std::move(undoUnhide)});
  if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
}

// --- what's shown ---

bool App::inSource(const Asset& a, const Source& s) const {
  if (a.hidden) return false;
  switch (s.kind) {
    case SourceKind::All:
      return true;
    case SourceKind::Recent:
      return a.added >= nowSeconds() - kRecentSeconds;
    case SourceKind::Missing:
      return a.missing;
    case SourceKind::Folder:
      return a.folderId == s.id && (s.sub.empty() || pathInside(a.path, s.sub));
    case SourceKind::Collection:
      return lib_.inCollection(s.id, a.id);
  }
  return false;
}

void App::refreshVisible() {
  // Counts for the sidebar.
  if (countsRev_ != lib_.revision()) {
    countsRev_ = lib_.revision();
    countAll_ = countRecent_ = countMissing_ = countPending_ = 0;
    std::map<int64_t, int> perFolder;
    const int64_t recent = nowSeconds() - kRecentSeconds;
    for (const Asset& a : lib_.assets()) {
      if (a.hidden) continue;
      ++countAll_;
      countRecent_ += a.added >= recent;
      countMissing_ += a.missing;
      countPending_ += a.needsAnalysis();
      if (a.folderId) ++perFolder[a.folderId];
    }
    folderCounts_.assign(perFolder.begin(), perFolder.end());
    if (source_.kind == SourceKind::Missing && countMissing_ == 0) source_ = Source();
    if (source_.kind == SourceKind::Folder && !lib_.folder(source_.id)) source_ = Source();
    if (source_.kind == SourceKind::Collection) {
      const auto& cs = lib_.collections();
      if (std::none_of(cs.begin(), cs.end(), [&](const Collection& c) { return c.id == source_.id; })) source_ = Source();
    }
  }
  std::string sig = std::to_string(lib_.revision()) + "|" + std::to_string(static_cast<int>(source_.kind)) + "|" +
                    std::to_string(source_.id) + "|" + source_.sub + "|" + search_ + "|" + format_ + "|" +
                    std::to_string(static_cast<int>(sort_)) + (sortDesc_ ? "d" : "a");
  if (sig == visibleSig_) return;
  visibleSig_ = std::move(sig);
  const std::string needle = search_;
  visible_.clear();
  for (const Asset& a : lib_.assets()) {
    if (!inSource(a, source_)) continue;
    if (!format_.empty()) {
      const bool match = format_ == "gltf" ? (a.ext == "gltf" || a.ext == "glb") : a.ext == format_;
      if (!match) continue;
    }
    if (!needle.empty() && !containsNoCase(a.name, needle) && !containsNoCase(a.path, needle)) continue;
    visible_.push_back(a.id);
  }
  auto key = [this](int64_t id) -> const Asset& { return *lib_.find(id); };
  std::stable_sort(visible_.begin(), visible_.end(), [&](int64_t x, int64_t y) {
    const Asset &a = key(x), &b = key(y);
    bool less;
    switch (sort_) {
      case SortBy::Name: {
        const std::string la = lower(a.name), lb = lower(b.name);
        if (la == lb) return a.id < b.id;
        less = la < lb;
        return sortDesc_ ? !less : less;
      }
      case SortBy::Size:
        if (a.size == b.size) return a.id < b.id;
        less = a.size < b.size;
        break;
      case SortBy::Triangles:
        if (a.triangles == b.triangles) return a.id < b.id;
        less = a.triangles < b.triangles;
        break;
      default:
        if (a.added == b.added) return sortDesc_ ? a.id > b.id : a.id < b.id;
        less = a.added < b.added;
        break;
    }
    return sortDesc_ ? !less : less;
  });
}

const std::vector<FolderNode>& App::folderTree(int64_t folderId) {
  if (treesRev_ != lib_.revision()) {
    treesRev_ = lib_.revision();
    folderTrees_.clear();
  }
  for (const auto& [id, tree] : folderTrees_) {
    if (id == folderId) return tree;
  }
  FolderNode root;
  const WatchedFolder* f = lib_.folder(folderId);
  if (f) {
    root.path = f->path;
    for (const Asset& a : lib_.assets()) {
      if (a.hidden || a.folderId != folderId) continue;
      // The directories between the folder and the file.
      std::string rel = a.path.size() > f->path.size() + 1 ? a.path.substr(f->path.size() + 1) : "";
      FolderNode* node = &root;
      ++node->count;
      size_t start = 0;
      for (size_t slash; (slash = rel.find('\\', start)) != std::string::npos; start = slash + 1) {
        const std::string part = rel.substr(start, slash - start);
        auto it = std::find_if(node->children.begin(), node->children.end(),
                               [&](const FolderNode& n) { return n.name == part; });
        if (it == node->children.end()) {
          FolderNode n;
          n.name = part;
          n.path = node->path + "\\" + part;
          node->children.push_back(std::move(n));
          it = node->children.end() - 1;
        }
        node = &*it;
        ++node->count;
      }
    }
    std::function<void(FolderNode&)> sortTree = [&](FolderNode& n) {
      std::sort(n.children.begin(), n.children.end(),
                [](const FolderNode& a, const FolderNode& b) { return lower(a.name) < lower(b.name); });
      for (FolderNode& c : n.children) sortTree(c);
    };
    sortTree(root);
  }
  folderTrees_.emplace_back(folderId, std::move(root.children));
  return folderTrees_.back().second;
}

// --- selection ---

void App::selectOnly(int64_t id) {
  selection_.clear();
  if (id) selection_.insert(id);
  focused_ = anchor_ = id;
}

void App::clickSelect(int64_t id, int index) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyShift && anchor_) {
    auto it = std::find(visible_.begin(), visible_.end(), anchor_);
    if (it != visible_.end()) {
      const int a = static_cast<int>(it - visible_.begin());
      if (!io.KeyCtrl) selection_.clear();
      for (int i = std::min(a, index); i <= std::max(a, index); ++i) selection_.insert(visible_[i]);
      focused_ = id;
      return;
    }
  }
  if (io.KeyCtrl) {
    if (isSelected(id)) {
      selection_.erase(id);
      focused_ = selection_.empty() ? 0 : (isSelected(focused_) ? focused_ : *selection_.begin());
    } else {
      selection_.insert(id);
      focused_ = id;
    }
    anchor_ = id;
    return;
  }
  selectOnly(id);
}

void App::selectAll() {
  selection_.clear();
  selection_.insert(visible_.begin(), visible_.end());
  if (!isSelected(focused_)) focused_ = visible_.empty() ? 0 : visible_.front();
}

void App::clearSelection() {
  selection_.clear();
  focused_ = 0;
}

std::vector<int64_t> App::selectedIds() const {
  std::vector<int64_t> out;
  for (int64_t id : visible_) {
    if (isSelected(id)) out.push_back(id);
  }
  // Selected but filtered out of view: still selected, at the end.
  for (int64_t id : selection_) {
    if (std::find(visible_.begin(), visible_.end(), id) == visible_.end()) out.push_back(id);
  }
  return out;
}

// --- the frame ---

double App::frameInterval() {
  // The 3D view moving by itself: smooth.
  if (viewer_.turntable || viewer_.camera.animating() || viewerMax_ && viewer_.loading) return 1.0 / 60.0;
  // Work going on that shows (pulses, the status dot, thumbnails arriving).
  if (viewer_.loading || !toasts_.empty() || scansInFlight_ > 0 || countPending_ > 0 || !analyzing_.empty() ||
      !thumbQueue_.empty() || jobs_->busy() > 0 || jobs_->hasPosted()) {
    return 1.0 / 30.0;
  }
  return 0.0;
}

void App::frame() {
  ++frameCount_;
  jobs_->drainMain(6.0);
  if (!pendingOpen_.empty()) {
    std::vector<std::string> paths;
    paths.swap(pendingOpen_);
    openTabs(paths);
  }
  if (!pendingImport_.empty()) {
    std::vector<std::string> paths;
    paths.swap(pendingImport_);
    activateTab(-1);
    importPaths(paths);
  }
  if (!dropped_.empty()) {
    std::vector<std::string> paths;
    paths.swap(dropped_);
    if (activeTab_ >= 0) {
      // Dropped on a file tab: looked at (new tabs), not imported.
      std::vector<std::string> models;
      for (const std::string& p : paths) {
        if (isModelExtension(lower(pathU8(u8(p).extension())))) models.push_back(p);
      }
      openTabs(models);
    } else {
      importPaths(paths);
    }
  }
  viewerOnly_ = activeTab_ >= 0;
  const float tabH = tabs_.empty() ? 0.0f : ui::px(36);
  if (!tabs_.empty()) drawTabBar(ImVec2(0, 0), ImVec2(ImGui::GetIO().DisplaySize.x, tabH));
  if (viewerOnly_) {
    viewerModeFrame(tabH);
    return;
  }
  pumpAnalysis();
  renderPendingThumbnails();
  refreshVisible();
  if (frameCount_ == 3 && opts_.selectIndex >= 0 && opts_.selectIndex < static_cast<int>(visible_.size())) {
    selectOnly(visible_[opts_.selectIndex]);
  }
  // The inspector follows the focused asset.
  const Asset* fa = lib_.find(focused_);
  if (!fa || fa->hidden) focused_ = 0;
  loadViewer((inspectorOpen_ || viewerMax_) && isSelected(focused_) ? focused_ : 0);

  const ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y;
  if (W <= 0 || H <= 0) return;
  const float topH = tabH, statusH = ui::px(26), gap = ui::px(1);
  const float bodyY = topH, bodyH = std::max(1.0f, H - topH - statusH);
  const float minSide = ui::px(180), minCenter = ui::px(320);
  // A folded side panel takes no room (its button in the browser's toolbar
  // brings it back).
  const float sw = sidebarOpen_ ? std::clamp(ui::px(sidebarW_), minSide, std::max(minSide, W * 0.35f)) : 0.0f;
  const float iw =
      inspectorOpen_ ? std::clamp(ui::px(inspectorW_), ui::px(260), std::max(ui::px(260), W - sw - minCenter)) : 0.0f;
  const float leftGap = sidebarOpen_ ? gap : 0.0f, rightGap = inspectorOpen_ ? gap : 0.0f;

  if (viewerMax_) {
    // The 3D view over the whole window (its button, Esc or F11 go back).
    ui::beginPanel("##viewerMax", ImVec2(0, topH), ImVec2(W, H - topH), ui::col::bg1);
    drawViewer(ImVec2(0, topH), ImVec2(W, H - topH));
    ui::endPanel();
  } else {
    if (sidebarOpen_) drawSidebar(ImVec2(0, bodyY), ImVec2(sw, bodyH));
    drawBrowser(ImVec2(sw + leftGap, bodyY), ImVec2(W - sw - iw - leftGap - rightGap, bodyH));
    if (inspectorOpen_) drawInspector(ImVec2(W - iw, bodyY), ImVec2(iw, bodyH));
    drawStatusBar(ImVec2(0, H - statusH), ImVec2(W, statusH));
  }

  float swRaw = sidebarW_, iwRaw = inspectorW_;
  if (!viewerMax_ && sidebarOpen_ && splitter("##split_side", ImVec2(sw - ui::px(2), bodyY), ImVec2(ui::px(5), bodyH), true, swRaw,
                               1.0f / ui::s(), 180.0f, 520.0f)) {
    sidebarW_ = swRaw;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  if (!viewerMax_ && inspectorOpen_ && splitter("##split_insp", ImVec2(W - iw - ui::px(3), bodyY), ImVec2(ui::px(5), bodyH), true,
                                 iwRaw, -1.0f / ui::s(), 260.0f, 900.0f)) {
    inspectorW_ = iwRaw;
    uiStateDirtyAt_ = ImGui::GetTime();
  }

  drawSettings();
  drawModals();
  drawToasts();
  handleShortcuts();
  dragOutIfLeaving();
  thumbs_->endFrame();
  if (uiStateDirtyAt_ > 0.0 && ImGui::GetTime() - uiStateDirtyAt_ > 1.0) saveUiState();
}

bool App::splitter(const char* id, ImVec2 pos, ImVec2 size, bool vertical, float& value, float sign, float lo, float hi) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::PopStyleVar(2);
  ImGui::InvisibleButton("##drag", size);
  const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hot) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
  bool changed = false;
  if (ImGui::IsItemActive()) {
    const float d = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
    if (d != 0.0f) {
      value = std::clamp(value + d * sign, lo, std::max(lo, hi));
      changed = true;
    }
  }
  if (hot) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = vertical ? ImVec2(pos.x + size.x * 0.5f, pos.y) : ImVec2(pos.x, pos.y + size.y * 0.5f);
    const ImVec2 b = vertical ? ImVec2(a.x, pos.y + size.y) : ImVec2(pos.x + size.x, a.y);
    dl->AddLine(a, b, IM_COL32(120, 120, 120, 255), 1.0f);
  }
  ImGui::End();
  return changed;
}

void App::dragOutIfLeaving() {
  const ImGuiPayload* pl = ImGui::GetDragDropPayload();
  if (!pl || !pl->IsDataType("CC_ASSETS")) return;
  // Where the mouse is now, from Windows (the window doesn't hear about
  // the mouse once it's outside).
  HWND hwnd = glfwGetWin32Window(window_);
  POINT pt;
  RECT rc;
  if (!GetCursorPos(&pt) || !GetClientRect(hwnd, &rc)) return;
  ScreenToClient(hwnd, &pt);
  if (PtInRect(&rc, pt)) return;
  if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
  const int64_t* ids = static_cast<const int64_t*>(pl->Data);
  std::vector<std::string> paths;
  for (size_t i = 0; i < pl->DataSize / sizeof(int64_t); ++i) {
    const Asset* a = lib_.find(ids[i]);
    if (a && !a->missing) paths.push_back(a->path);
  }
  ImGui::ClearDragDrop();
  // Out to Windows: Blender, Unreal, Unity, Explorer take it as files.
  dragFilesOut(paths);
  // The button went up inside Windows' loop: ImGui never saw it.
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
}

void App::handleShortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  const bool typing = io.WantTextInput;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) focusSearch_ = true;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) settingsOpen_ = !settingsOpen_;
  tabShortcuts();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_LeftBracket)) {
    sidebarOpen_ = !sidebarOpen_;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket)) {
    inspectorOpen_ = !inspectorOpen_;
    uiStateDirtyAt_ = ImGui::GetTime();
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_I)) {
    std::vector<std::string> files = openModelFilesDialog(window_);
    if (!files.empty()) importPaths(files);
  }
  if (typing || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || settingsOpen_) return;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) selectAll();
  if (ImGui::IsKeyPressed(ImGuiKey_F11, false) && (viewerMax_ || viewer_.mesh.valid())) viewerMax_ = !viewerMax_;
  if (viewerMax_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    viewerMax_ = false;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (search_[0]) search_[0] = 0;
    else clearSelection();
  }
  const std::vector<int64_t> sel = selectedIds();
  if (ImGui::IsKeyChordPressed(ImGuiKey_Delete) && !sel.empty()) removeFromLibrary(sel);
  if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Delete) && !sel.empty()) confirmDelete_ = sel;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) && !sel.empty()) {
    std::string text;
    for (int64_t id : sel) {
      if (const Asset* a = lib_.find(id)) text += (text.empty() ? "" : "\r\n") + a->path;
    }
    ImGui::SetClipboardText(text.c_str());
    toast(sel.size() == 1 ? "Path copied" : std::to_string(sel.size()) + " paths copied");
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
    if (const Asset* a = lib_.find(focused_)) openWithDefaultApp(a->path);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F, false) && viewer_.mesh.valid()) {
    frameViewer();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_G, false)) viewer_.grid = !viewer_.grid, uiStateDirtyAt_ = ImGui::GetTime();
  const ImGuiKey shadeKeys[5] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5};
  for (int i = 0; i < 5; ++i) {
    if (ImGui::IsKeyPressed(shadeKeys[i], false) && !ImGui::GetIO().KeyCtrl) {
      viewer_.shading = static_cast<Shading>(i);
      uiStateDirtyAt_ = ImGui::GetTime();
    }
  }
  // Arrows walk the grid (rows of `gridColumns_`) or the list.
  int step = 0;
  const int rowStep = viewMode_ == ViewMode::Grid ? std::max(1, gridColumns_) : 1;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step = -1;
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step = 1;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) step = -rowStep;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) step = rowStep;
  if (step && !visible_.empty()) {
    auto it = std::find(visible_.begin(), visible_.end(), focused_);
    int i = it == visible_.end() ? (step > 0 ? -1 : static_cast<int>(visible_.size())) : static_cast<int>(it - visible_.begin());
    i = std::clamp(i + step, 0, static_cast<int>(visible_.size()) - 1);
    const int64_t id = visible_[i];
    if (io.KeyShift) {
      clickSelect(id, i);
    } else {
      selectOnly(id);
    }
    scrollToFocus_ = true;
  }
}
