#pragma once

#include "gl_loader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class Jobs;

// Thumbnails: PNG files in the data folder (one per asset, "<id>.png"),
// loaded into textures only for the tiles on screen; the least recently
// shown ones are let go past a limit.
class ThumbCache {
 public:
  static constexpr int kSize = 256;  // px, square

  ThumbCache(Jobs& jobs, std::filesystem::path dir);
  ~ThumbCache();

  // The asset's texture, or 0 while it loads. `onMissing` is called (on the
  // main thread) when its file isn't there or can't be read.
  GLuint get(int64_t id);
  // A picture just drawn: shown at once, written to disk in the background.
  void put(int64_t id, std::vector<uint8_t> rgba);
  void forget(int64_t id);
  // Drops every texture and file.
  void clearAll();
  // Lets go of textures past the limit; call once per frame.
  void endFrame();

  std::function<void(int64_t)> onMissing;
  size_t loadedCount() const { return entries_.size(); }

 private:
  struct Entry {
    GLuint tex = 0;
    uint64_t lastUsed = 0;
    bool loading = false;
    bool failed = false;
  };
  std::filesystem::path file(int64_t id) const;
  GLuint upload(const uint8_t* rgba, int w, int h);

  Jobs& jobs_;
  std::filesystem::path dir_;
  std::unordered_map<int64_t, Entry> entries_;
  uint64_t frame_ = 0;
  int loadsInFlight_ = 0;
  // Generation: results of loads started before clearAll() are dropped.
  std::shared_ptr<int> generation_;
};
