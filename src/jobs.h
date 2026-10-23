#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Background work: a few threads take jobs (urgent ones first: what the
// user is looking at right now). A job hands its result back with post();
// the main thread runs those in drainMain(), where the library and OpenGL
// live — nothing else is touched from two threads.
class Jobs {
 public:
  explicit Jobs(int threads = 0);
  ~Jobs();

  void run(std::function<void()> job, bool urgent = false);
  void post(std::function<void()> onMain);
  // Called (on the posting thread) after each post(): wakes the main loop.
  std::function<void()> onPost;
  // Results waiting for drainMain().
  bool hasPosted();
  // Runs what the jobs posted, for up to `budgetMs` (the rest next frame).
  void drainMain(double budgetMs);

  // Jobs queued or running.
  int busy() const { return busy_.load(); }
  int threads() const { return static_cast<int>(threads_.size()); }

 private:
  void worker();

  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> urgent_, normal_;
  std::mutex mainMutex_;
  std::deque<std::function<void()>> main_;
  std::atomic<int> busy_{0};
  bool stop_ = false;
};
