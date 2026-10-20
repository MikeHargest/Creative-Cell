#include "library.h"

#include <algorithm>
#include <cstdlib>

#include "sqlite/sqlite3.h"
#include "util.h"

namespace {
// A prepared statement, finalized when it goes out of scope.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s_, nullptr); }
  ~Stmt() { sqlite3_finalize(s_); }
  Stmt& bind(int i, int64_t v) {
    sqlite3_bind_int64(s_, i, v);
    return *this;
  }
  Stmt& bind(int i, double v) {
    sqlite3_bind_double(s_, i, v);
    return *this;
  }
  Stmt& bind(int i, const std::string& v) {
    sqlite3_bind_text(s_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
  }
  bool step() { return sqlite3_step(s_) == SQLITE_ROW; }
  void run() {
    sqlite3_step(s_);
    sqlite3_reset(s_);
    sqlite3_clear_bindings(s_);
  }
  int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
  double f(int c) const { return sqlite3_column_double(s_, c); }
  std::string str(int c) const {
    const unsigned char* t = sqlite3_column_text(s_, c);
    return t ? reinterpret_cast<const char*>(t) : "";
  }

 private:
  sqlite3_stmt* s_ = nullptr;
};

// The schema; `user_version` says which step a file is at.
const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS folders(
  id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, added INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS assets(
  id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, name TEXT NOT NULL, ext TEXT NOT NULL,
  folder_id INTEGER NOT NULL DEFAULT 0, size INTEGER NOT NULL DEFAULT 0, mtime INTEGER NOT NULL DEFAULT 0,
  added INTEGER NOT NULL DEFAULT 0,
  triangles INTEGER NOT NULL DEFAULT -1, vertices INTEGER NOT NULL DEFAULT -1,
  parts INTEGER NOT NULL DEFAULT -1, materials INTEGER NOT NULL DEFAULT -1,
  dx REAL NOT NULL DEFAULT 0, dy REAL NOT NULL DEFAULT 0, dz REAL NOT NULL DEFAULT 0,
  metric INTEGER NOT NULL DEFAULT 0, analyzed_mtime INTEGER NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT '',
  missing INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS collections(
  id INTEGER PRIMARY KEY, name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS collection_assets(
  collection_id INTEGER NOT NULL, asset_id INTEGER NOT NULL, PRIMARY KEY(collection_id, asset_id));
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
PRAGMA user_version = 1;
)SQL";

std::string stemOf(const std::string& path) { return pathU8(u8(path).stem()); }
std::string extOf(const std::string& path) {
  std::string e = lower(pathU8(u8(path).extension()));
  return e.empty() ? e : e.substr(1);
}
}  // namespace

std::string normalizePath(const std::string& p) {
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(u8(p), ec);
  if (ec) abs = u8(p);
  std::string s = pathU8(abs.lexically_normal().make_preferred());
  while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
  return s;
}

bool pathInside(const std::string& path, const std::string& folder) {
  if (path.size() < folder.size()) return false;
  if (lower(path.substr(0, folder.size())) != lower(folder)) return false;
  return path.size() == folder.size() || path[folder.size()] == '\\' || folder.back() == '\\';
}

Library::~Library() { close(); }

bool Library::open(const std::filesystem::path& dbFile, std::string& error) {
  close();
  if (sqlite3_open_v2(pathU8(dbFile).c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    error = db_ ? sqlite3_errmsg(db_) : "Couldn't open the library.";
    close();
    return false;
  }
  exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=OFF;");
  if (!exec(kSchema)) {
    error = sqlite3_errmsg(db_);
    close();
    return false;
  }
  load();
  return true;
}

void Library::close() {
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

bool Library::exec(const char* sql) {
  if (!db_) return false;
  char* msg = nullptr;
  const bool ok = sqlite3_exec(db_, sql, nullptr, nullptr, &msg) == SQLITE_OK;
  sqlite3_free(msg);
  return ok;
}

void Library::load() {
  assets_.clear();
  byId_.clear();
  byPath_.clear();
  folders_.clear();
  collections_.clear();
  members_.clear();
  settings_.clear();
  {
    Stmt s(db_,
           "SELECT id,path,name,ext,folder_id,size,mtime,added,triangles,vertices,parts,materials,dx,dy,dz,metric,"
           "analyzed_mtime,error,missing,hidden FROM assets ORDER BY id");
    while (s.step()) {
      Asset a;
      a.id = s.i64(0);
      a.path = s.str(1);
      a.name = s.str(2);
      a.ext = s.str(3);
      a.folderId = s.i64(4);
      a.size = s.i64(5);
      a.mtime = s.i64(6);
      a.added = s.i64(7);
      a.triangles = s.i64(8);
      a.vertices = s.i64(9);
      a.parts = static_cast<int>(s.i64(10));
      a.materials = static_cast<int>(s.i64(11));
      a.dims = glm::vec3(static_cast<float>(s.f(12)), static_cast<float>(s.f(13)), static_cast<float>(s.f(14)));
      a.metric = s.i64(15) != 0;
      a.analyzedMtime = s.i64(16);
      a.error = s.str(17);
      a.missing = s.i64(18) != 0;
      a.hidden = s.i64(19) != 0;
      byId_[a.id] = assets_.size();
      byPath_[lower(a.path)] = a.id;
      assets_.push_back(std::move(a));
    }
  }
  {
    Stmt s(db_, "SELECT id,path FROM folders ORDER BY path");
    while (s.step()) folders_.push_back({s.i64(0), s.str(1)});
  }
  {
    Stmt s(db_, "SELECT id,name FROM collections ORDER BY position,id");
    while (s.step()) collections_.push_back({s.i64(0), s.str(1)});
  }
  {
    Stmt s(db_, "SELECT collection_id,asset_id FROM collection_assets");
    while (s.step()) members_[s.i64(0)].insert(s.i64(1));
  }
  {
    Stmt s(db_, "SELECT key,value FROM settings");
    while (s.step()) settings_[s.str(0)] = s.str(1);
  }
  touch();
}

const Asset* Library::find(int64_t id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &assets_[it->second];
}

const Asset* Library::findByPath(const std::string& path) const {
  auto it = byPath_.find(lower(path));
  return it == byPath_.end() ? nullptr : find(it->second);
}

Asset* Library::findMut(int64_t id) {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &assets_[it->second];
}

const WatchedFolder* Library::folder(int64_t id) const {
  for (const WatchedFolder& f : folders_) {
    if (f.id == id) return &f;
  }
  return nullptr;
}

void Library::writeAsset(const Asset& a) {
  Stmt s(db_,
         "UPDATE assets SET folder_id=?2,size=?3,mtime=?4,triangles=?5,vertices=?6,parts=?7,materials=?8,dx=?9,"
         "dy=?10,dz=?11,metric=?12,analyzed_mtime=?13,error=?14,missing=?15,hidden=?16,name=?17 WHERE id=?1");
  s.bind(1, a.id)
      .bind(2, a.folderId)
      .bind(3, a.size)
      .bind(4, a.mtime)
      .bind(5, a.triangles)
      .bind(6, a.vertices)
      .bind(7, static_cast<int64_t>(a.parts))
      .bind(8, static_cast<int64_t>(a.materials))
      .bind(9, static_cast<double>(a.dims.x))
      .bind(10, static_cast<double>(a.dims.y))
      .bind(11, static_cast<double>(a.dims.z))
      .bind(12, static_cast<int64_t>(a.metric))
      .bind(13, a.analyzedMtime)
      .bind(14, a.error)
      .bind(15, static_cast<int64_t>(a.missing))
      .bind(16, static_cast<int64_t>(a.hidden))
      .bind(17, a.name);
  s.run();
}

int64_t Library::insertAsset(const FileEntry& f, int64_t folderId) {
  Asset a;
  a.path = f.path;
  a.name = stemOf(f.path);
  a.ext = extOf(f.path);
  a.folderId = folderId;
  a.size = f.size;
  a.mtime = f.mtime;
  a.added = nowSeconds();
  Stmt s(db_, "INSERT INTO assets(path,name,ext,folder_id,size,mtime,added) VALUES(?1,?2,?3,?4,?5,?6,?7)");
  s.bind(1, a.path).bind(2, a.name).bind(3, a.ext).bind(4, a.folderId).bind(5, a.size).bind(6, a.mtime).bind(7, a.added);
  s.run();
  a.id = sqlite3_last_insert_rowid(db_);
  byId_[a.id] = assets_.size();
  byPath_[lower(a.path)] = a.id;
  assets_.push_back(std::move(a));
  return assets_.back().id;
}

int64_t Library::addFolder(const std::string& rawPath) {
  const std::string path = normalizePath(rawPath);
  for (const WatchedFolder& f : folders_) {
    if (pathInside(path, f.path)) return f.id;
  }
  exec("BEGIN");
  {
    Stmt s(db_, "INSERT INTO folders(path,added) VALUES(?1,?2)");
    s.bind(1, path).bind(2, nowSeconds());
    s.run();
  }
  const int64_t id = sqlite3_last_insert_rowid(db_);
  // Watched folders inside the new one fold into it; loose files in it join it.
  std::vector<int64_t> inner;
  for (const WatchedFolder& f : folders_) {
    if (pathInside(f.path, path)) inner.push_back(f.id);
  }
  for (int64_t fid : inner) {
    Stmt s(db_, "DELETE FROM folders WHERE id=?1");
    s.bind(1, fid);
    s.run();
  }
  for (Asset& a : assets_) {
    const bool claim = std::find(inner.begin(), inner.end(), a.folderId) != inner.end() ||
                       (a.folderId == 0 && pathInside(a.path, path));
    if (claim) {
      a.folderId = id;
      writeAsset(a);
    }
  }
  exec("COMMIT");
  folders_.erase(std::remove_if(folders_.begin(), folders_.end(),
                                [&](const WatchedFolder& f) { return std::find(inner.begin(), inner.end(), f.id) != inner.end(); }),
                 folders_.end());
  folders_.push_back({id, path});
  std::sort(folders_.begin(), folders_.end(), [](const WatchedFolder& a, const WatchedFolder& b) { return lower(a.path) < lower(b.path); });
  touch();
  return id;
}

void Library::removeFolder(int64_t id) {
  std::vector<int64_t> gone;
  for (const Asset& a : assets_) {
    if (a.folderId == id) gone.push_back(a.id);
  }
  forget(gone);
  Stmt s(db_, "DELETE FROM folders WHERE id=?1");
  s.bind(1, id);
  s.run();
  folders_.erase(std::remove_if(folders_.begin(), folders_.end(), [&](const WatchedFolder& f) { return f.id == id; }),
                 folders_.end());
  touch();
}

Library::SyncResult Library::syncFolder(int64_t folderId, const std::vector<FileEntry>& files) {
  SyncResult r;
  if (!folder(folderId)) return r;
  exec("BEGIN");
  std::unordered_set<int64_t> seen;
  for (const FileEntry& f : files) {
    auto it = byPath_.find(lower(f.path));
    if (it == byPath_.end()) {
      seen.insert(insertAsset(f, folderId));
      ++r.added;
      continue;
    }
    Asset& a = *findMut(it->second);
    seen.insert(a.id);
    const bool changed = a.size != f.size || a.mtime != f.mtime;
    if (changed || a.missing || a.folderId != folderId) {
      if (changed) {
        a.size = f.size;
        a.mtime = f.mtime;
        a.error.clear();
        ++r.changed;
      }
      a.missing = false;
      a.folderId = folderId;
      writeAsset(a);
    }
  }
  for (Asset& a : assets_) {
    if (a.folderId == folderId && !a.missing && !seen.count(a.id)) {
      a.missing = true;
      writeAsset(a);
      ++r.missing;
    }
  }
  exec("COMMIT");
  if (r.added || r.changed || r.missing) touch();
  return r;
}

int64_t Library::addFile(const FileEntry& raw) {
  FileEntry f = raw;
  f.path = normalizePath(f.path);
  int64_t folderId = 0;
  for (const WatchedFolder& fo : folders_) {
    if (pathInside(f.path, fo.path)) folderId = fo.id;
  }
  auto it = byPath_.find(lower(f.path));
  if (it != byPath_.end()) {
    Asset& a = *findMut(it->second);
    if (a.size != f.size || a.mtime != f.mtime) a.error.clear();
    a.size = f.size;
    a.mtime = f.mtime;
    a.hidden = false;
    a.missing = false;
    if (folderId) a.folderId = folderId;
    writeAsset(a);
    touch();
    return a.id;
  }
  const int64_t id = insertAsset(f, folderId);
  touch();
  return id;
}

void Library::setAnalysis(int64_t id, const ModelStats& s, int64_t analyzedMtime) {
  Asset* a = findMut(id);
  if (!a) return;
  a->triangles = s.triangles;
  a->vertices = s.vertices;
  a->parts = s.parts;
  a->materials = s.materials;
  a->dims = s.size;
  a->metric = s.metric;
  a->analyzedMtime = analyzedMtime;
  a->error.clear();
  writeAsset(*a);
  touch();
}

void Library::setAnalysisError(int64_t id, const std::string& error, int64_t analyzedMtime) {
  Asset* a = findMut(id);
  if (!a) return;
  a->error = error;
  a->analyzedMtime = analyzedMtime;
  writeAsset(*a);
  touch();
}

void Library::resetAnalysis() {
  exec("UPDATE assets SET analyzed_mtime=0, error=''");
  for (Asset& a : assets_) {
    a.analyzedMtime = 0;
    a.error.clear();
  }
  touch();
}

void Library::setHidden(const std::vector<int64_t>& ids, bool hidden) {
  exec("BEGIN");
  for (int64_t id : ids) {
    if (Asset* a = findMut(id)) {
      a->hidden = hidden;
      writeAsset(*a);
    }
  }
  exec("COMMIT");
  touch();
}

void Library::setMissing(int64_t id, bool missing) {
  Asset* a = findMut(id);
  if (!a || a->missing == missing) return;
  a->missing = missing;
  writeAsset(*a);
  touch();
}

void Library::forget(const std::vector<int64_t>& ids) {
  if (ids.empty()) return;
  exec("BEGIN");
  for (int64_t id : ids) {
    Stmt s(db_, "DELETE FROM assets WHERE id=?1");
    s.bind(1, id);
    s.run();
    Stmt c(db_, "DELETE FROM collection_assets WHERE asset_id=?1");
    c.bind(1, id);
    c.run();
    for (auto& [cid, set] : members_) set.erase(id);
  }
  exec("COMMIT");
  std::unordered_set<int64_t> gone(ids.begin(), ids.end());
  assets_.erase(std::remove_if(assets_.begin(), assets_.end(), [&](const Asset& a) { return gone.count(a.id) != 0; }),
                assets_.end());
  byId_.clear();
  byPath_.clear();
  for (size_t i = 0; i < assets_.size(); ++i) {
    byId_[assets_[i].id] = i;
    byPath_[lower(assets_[i].path)] = assets_[i].id;
  }
  touch();
}

int64_t Library::createCollection(const std::string& name) {
  Stmt s(db_, "INSERT INTO collections(name,position) VALUES(?1,?2)");
  s.bind(1, name).bind(2, static_cast<int64_t>(collections_.size()));
  s.run();
  const int64_t id = sqlite3_last_insert_rowid(db_);
  collections_.push_back({id, name});
  touch();
  return id;
}

void Library::renameCollection(int64_t id, const std::string& name) {
  Stmt s(db_, "UPDATE collections SET name=?2 WHERE id=?1");
  s.bind(1, id).bind(2, name);
  s.run();
  for (Collection& c : collections_) {
    if (c.id == id) c.name = name;
  }
  touch();
}

void Library::deleteCollection(int64_t id) {
  {
    Stmt s(db_, "DELETE FROM collections WHERE id=?1");
    s.bind(1, id);
    s.run();
  }
  {
    Stmt s(db_, "DELETE FROM collection_assets WHERE collection_id=?1");
    s.bind(1, id);
    s.run();
  }
  collections_.erase(std::remove_if(collections_.begin(), collections_.end(), [&](const Collection& c) { return c.id == id; }),
                     collections_.end());
  members_.erase(id);
  touch();
}

void Library::moveCollection(int64_t id, int toIndex) {
  auto it = std::find_if(collections_.begin(), collections_.end(), [&](const Collection& c) { return c.id == id; });
  if (it == collections_.end()) return;
  Collection c = *it;
  collections_.erase(it);
  toIndex = std::clamp(toIndex, 0, static_cast<int>(collections_.size()));
  collections_.insert(collections_.begin() + toIndex, c);
  exec("BEGIN");
  for (size_t i = 0; i < collections_.size(); ++i) {
    Stmt s(db_, "UPDATE collections SET position=?2 WHERE id=?1");
    s.bind(1, collections_[i].id).bind(2, static_cast<int64_t>(i));
    s.run();
  }
  exec("COMMIT");
  touch();
}

void Library::addToCollection(int64_t cid, const std::vector<int64_t>& ids) {
  exec("BEGIN");
  for (int64_t id : ids) {
    Stmt s(db_, "INSERT OR IGNORE INTO collection_assets(collection_id,asset_id) VALUES(?1,?2)");
    s.bind(1, cid).bind(2, id);
    s.run();
    members_[cid].insert(id);
  }
  exec("COMMIT");
  touch();
}

void Library::removeFromCollection(int64_t cid, const std::vector<int64_t>& ids) {
  exec("BEGIN");
  for (int64_t id : ids) {
    Stmt s(db_, "DELETE FROM collection_assets WHERE collection_id=?1 AND asset_id=?2");
    s.bind(1, cid).bind(2, id);
    s.run();
    members_[cid].erase(id);
  }
  exec("COMMIT");
  touch();
}

bool Library::inCollection(int64_t cid, int64_t aid) const {
  auto it = members_.find(cid);
  return it != members_.end() && it->second.count(aid) != 0;
}

int Library::collectionSize(int64_t cid) const {
  auto it = members_.find(cid);
  if (it == members_.end()) return 0;
  int n = 0;
  for (int64_t id : it->second) {
    const Asset* a = find(id);
    n += a && !a->hidden;
  }
  return n;
}

std::vector<int64_t> Library::collectionsOf(int64_t aid) const {
  std::vector<int64_t> out;
  for (const Collection& c : collections_) {
    if (inCollection(c.id, aid)) out.push_back(c.id);
  }
  return out;
}

std::string Library::setting(const std::string& key, const std::string& fallback) const {
  auto it = settings_.find(key);
  return it == settings_.end() ? fallback : it->second;
}

void Library::setSetting(const std::string& key, const std::string& value) {
  auto it = settings_.find(key);
  if (it != settings_.end() && it->second == value) return;
  settings_[key] = value;
  Stmt s(db_, "INSERT INTO settings(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  s.bind(1, key).bind(2, value);
  s.run();
}

int Library::settingInt(const std::string& key, int fallback) const {
  const std::string v = setting(key);
  return v.empty() ? fallback : std::atoi(v.c_str());
}

float Library::settingFloat(const std::string& key, float fallback) const {
  const std::string v = setting(key);
  return v.empty() ? fallback : static_cast<float>(std::atof(v.c_str()));
}
