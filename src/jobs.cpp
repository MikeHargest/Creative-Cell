#include "jobs.h"

#include <algorithm>
#include <chrono>

Jobs::Jobs(int threads) {
  if (threads <= 0) {
    // Leave a core for the UI; loading big files is mostly memory-bound
    // past a few threads anyway.
    threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 6);
  }
  for (int i = 0; i < threads; ++i) threads_.emplace_back([this] { worker(); });
}

Jobs::~Jobs() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    urgent_.clear();
    normal_.clear();
  }
  wake_.notify_all();
  for (std::thread& t : threads_) t.join();
}

void Jobs::run(std::function<void()> job, bool urgent) {
  ++busy_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    (urgent ? urgent_ : normal_).push_back(std::move(job));
  }
  wake_.notify_one();
}

void Jobs::post(std::function<void()> onMain) {
  {
    std::lock_guard<std::mutex> lock(mainMutex_);
    main_.push_back(std::move(onMain));
  }
  if (onPost) onPost();
}

bool Jobs::hasPosted() {
  std::lock_guard<std::mutex> lock(mainMutex_);
  return !main_.empty();
}

void Jobs::drainMain(double budgetMs) {
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    std::function<void()> f;
    {
      std::lock_guard<std::mutex> lock(mainMutex_);
      if (main_.empty()) return;
      f = std::move(main_.front());
      main_.pop_front();
    }
    f();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (ms > budgetMs) return;
  }
}

void Jobs::worker() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stop_ || !urgent_.empty() || !normal_.empty(); });
      if (stop_) return;
      std::deque<std::function<void()>>& q = urgent_.empty() ? normal_ : urgent_;
      job = std::move(q.front());
      q.pop_front();
    }
    job();
    --busy_;
  }
}
