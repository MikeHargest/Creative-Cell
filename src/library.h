#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model_loader.h"

struct sqlite3;

// A model file the library knows. Files are indexed where they lie: the
// library never moves them (Import with copying puts a copy in the Library
// folder first).
struct Asset {
  int64_t id = 0;
  std::string path;  // absolute, UTF-8
  std::string name;  // file name without extension
  std::string ext;   // lower case, no dot: "fbx"
  int64_t folderId = 0;  // the watched folder it was found in; 0 — added by itself
  int64_t size = 0, mtime = 0, added = 0;
  // From the last analysis (load + thumbnail); -1 — not analyzed yet.
  int64_t triangles = -1, vertices = -1;
  int parts = -1, materials = -1;
  glm::vec3 dims{0.0f};
  bool metric = false;
  int64_t analyzedMtime = 0;  // the mtime the analysis and thumbnail are of
  std::string error;          // the analysis failed (cleared when the file changes)
  bool missing = false;       // not found on the last scan
  bool hidden = false;        // removed from the library (its file stays)

  bool needsAnalysis() const { return !missing && analyzedMtime != mtime; }
};

struct WatchedFolder {
  int64_t id = 0;
  std::string path;
};

struct Collection {
  int64_t id = 0;
  std::string name;
};

struct FileEntry {
  std::string path;
  int64_t size = 0, mtime = 0;
};

class Library {
 public:
  ~Library();
  bool open(const std::filesystem::path& dbFile, std::string& error);
  void close();

  // Everything, in memory (hidden rows included — check `hidden`).
  const std::vector<Asset>& assets() const { return assets_; }
  const Asset* find(int64_t id) const;
  const Asset* findByPath(const std::string& normalizedPath) const;
  const std::vector<WatchedFolder>& folders() const { return folders_; }
  const WatchedFolder* folder(int64_t id) const;
  const std::vector<Collection>& collections() const { return collections_; }
  // Bumped by every change: views rebuild what they show when it moves.
  uint64_t revision() const { return revision_; }

  // A folder to watch. Inside one already watched: that one's id. Holding
  // watched ones: they're folded into it.
  int64_t addFolder(const std::string& path);
  // Stops watching; its assets leave the library (files stay).
  void removeFolder(int64_t id);
  struct SyncResult {
    int added = 0, changed = 0, missing = 0;
  };
  // A folder's scan: new files in, changed ones marked for analysis, gone
  // ones marked missing (found again — back).
  SyncResult syncFolder(int64_t folderId, const std::vector<FileEntry>& files);
  // One file by itself (dropped, Add Files). Already known: shown again.
  int64_t addFile(const FileEntry& file);

  void setAnalysis(int64_t id, const ModelStats& s, int64_t analyzedMtime);
  void setAnalysisError(int64_t id, const std::string& error, int64_t analyzedMtime);
  // Every asset analyzed again (thumbnails rebuilt).
  void resetAnalysis();

  void setHidden(const std::vector<int64_t>& ids, bool hidden);
  void setMissing(int64_t id, bool missing);
  // Rows gone for good (their files deleted, or missing ones cleaned up).
  void forget(const std::vector<int64_t>& ids);

  int64_t createCollection(const std::string& name);
  void renameCollection(int64_t id, const std::string& name);
  void deleteCollection(int64_t id);
  void moveCollection(int64_t id, int toIndex);
  void addToCollection(int64_t collectionId, const std::vector<int64_t>& assetIds);
  void removeFromCollection(int64_t collectionId, const std::vector<int64_t>& assetIds);
  bool inCollection(int64_t collectionId, int64_t assetId) const;
  int collectionSize(int64_t collectionId) const;
  std::vector<int64_t> collectionsOf(int64_t assetId) const;

  std::string setting(const std::string& key, const std::string& fallback = "") const;
  void setSetting(const std::string& key, const std::string& value);
  int settingInt(const std::string& key, int fallback) const;
  float settingFloat(const std::string& key, float fallback) const;

 private:
  bool exec(const char* sql);
  void load();
  Asset* findMut(int64_t id);
  int64_t insertAsset(const FileEntry& f, int64_t folderId);
  void writeAsset(const Asset& a);
  void touch() { ++revision_; }

  sqlite3* db_ = nullptr;
  std::vector<Asset> assets_;
  std::unordered_map<int64_t, size_t> byId_;
  std::unordered_map<std::string, int64_t> byPath_;
  std::vector<WatchedFolder> folders_;
  std::vector<Collection> collections_;
  std::unordered_map<int64_t, std::unordered_set<int64_t>> members_;  // collection -> assets
  std::unordered_map<std::string, std::string> settings_;
  uint64_t revision_ = 1;
};

// "E:\Models\a.fbx" — absolute, normalized, backslashes.
std::string normalizePath(const std::string& utf8Path);
// `path` lies inside `folder` (or is it).
bool pathInside(const std::string& path, const std::string& folder);
