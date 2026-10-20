#include "thumbs.h"

#include <algorithm>
#include <fstream>
#include <string>

#include "jobs.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace {
constexpr size_t kMaxTextures = 600;
constexpr int kMaxLoadsInFlight = 8;

void appendBytes(void* ctx, void* data, int size) {
  auto* out = static_cast<std::string*>(ctx);
  out->append(static_cast<const char*>(data), static_cast<size_t>(size));
}
}  // namespace

ThumbCache::ThumbCache(Jobs& jobs, std::filesystem::path dir)
    : jobs_(jobs), dir_(std::move(dir)), generation_(std::make_shared<int>(0)) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
}

ThumbCache::~ThumbCache() {
  for (auto& [id, e] : entries_) {
    if (e.tex) glDeleteTextures(1, &e.tex);
  }
}

std::filesystem::path ThumbCache::file(int64_t id) const { return dir_ / (std::to_string(id) + ".png"); }

GLuint ThumbCache::upload(const uint8_t* rgba, int w, int h) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glx::GenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

GLuint ThumbCache::get(int64_t id) {
  Entry& e = entries_[id];
  e.lastUsed = frame_;
  if (e.tex || e.loading || e.failed) return e.tex;
  if (loadsInFlight_ >= kMaxLoadsInFlight) return 0;  // next frame
  e.loading = true;
  ++loadsInFlight_;
  const std::filesystem::path path = file(id);
  std::weak_ptr<int> gen = generation_;
  jobs_.run(
      [this, id, path, gen] {
        std::string bytes;
        {
          std::ifstream f(path, std::ios::binary);
          if (f) bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        int w = 0, h = 0, n = 0;
        unsigned char* px = bytes.empty() ? nullptr
                                          : stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                                  static_cast<int>(bytes.size()), &w, &h, &n, 4);
        std::vector<uint8_t> rgba;
        if (px) rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
        stbi_image_free(px);
        jobs_.post([this, id, gen, w, h, rgba = std::move(rgba)] {
          if (gen.expired()) return;
          --loadsInFlight_;
          auto it = entries_.find(id);
          if (it == entries_.end()) return;
          it->second.loading = false;
          if (rgba.empty()) {
            it->second.failed = true;
            if (onMissing) onMissing(id);
            return;
          }
          it->second.tex = upload(rgba.data(), w, h);
        });
      },
      true);  // what's on screen goes before analysis
  return 0;
}

void ThumbCache::put(int64_t id, std::vector<uint8_t> rgba) {
  Entry& e = entries_[id];
  if (e.tex) glDeleteTextures(1, &e.tex);
  e.tex = upload(rgba.data(), kSize, kSize);
  e.failed = false;
  e.lastUsed = frame_;
  const std::filesystem::path path = file(id);
  jobs_.run([path, rgba = std::move(rgba)] {
    std::string png;
    stbi_write_png_to_func(appendBytes, &png, kSize, kSize, 4, rgba.data(), kSize * 4);
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    {
      std::ofstream f(tmp, std::ios::binary);
      f.write(png.data(), static_cast<std::streamsize>(png.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
  });
}

void ThumbCache::forget(int64_t id) {
  auto it = entries_.find(id);
  if (it != entries_.end()) {
    if (it->second.tex) glDeleteTextures(1, &it->second.tex);
    if (!it->second.loading) entries_.erase(it);
    else it->second = Entry{0, 0, true, false};
  }
  std::error_code ec;
  std::filesystem::remove(file(id), ec);
}

void ThumbCache::clearAll() {
  for (auto& [id, e] : entries_) {
    if (e.tex) glDeleteTextures(1, &e.tex);
  }
  entries_.clear();
  loadsInFlight_ = 0;
  generation_ = std::make_shared<int>(0);
  std::error_code ec;
  for (const auto& f : std::filesystem::directory_iterator(dir_, ec)) {
    if (f.path().extension() == L".png") std::filesystem::remove(f.path(), ec);
  }
}

void ThumbCache::endFrame() {
  ++frame_;
  if (entries_.size() <= kMaxTextures) return;
  std::vector<std::pair<uint64_t, int64_t>> order;
  order.reserve(entries_.size());
  for (const auto& [id, e] : entries_) {
    if (!e.loading) order.emplace_back(e.lastUsed, id);
  }
  std::sort(order.begin(), order.end());
  size_t excess = entries_.size() - kMaxTextures;
  for (size_t i = 0; i < order.size() && excess; ++i, --excess) {
    if (order[i].first + 2 >= frame_) break;  // on screen right now
    auto it = entries_.find(order[i].second);
    if (it->second.tex) glDeleteTextures(1, &it->second.tex);
    entries_.erase(it);
  }
}
