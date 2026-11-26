#include "thread_pool.h"

#include <algorithm>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace rasty {

ThreadPool::ThreadPool(unsigned threads) {
  if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
  workers_.reserve(threads - 1);
  for (unsigned i = 1; i < threads; ++i) workers_.emplace_back([this] { worker_loop(); });
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  for (std::thread& t : workers_) t.join();
}

void ThreadPool::parallel_for(std::size_t count, std::size_t grain, const Body& body) {
  if (count == 0) return;
  grain = std::max<std::size_t>(grain, 1);
  if (workers_.empty() || count <= grain) {
    for (std::size_t begin = 0; begin < count; begin += grain) body(begin, std::min(begin + grain, count));
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    body_ = &body;
    count_ = count;
    grain_ = grain;
    next_.store(0, std::memory_order_relaxed);
    busy_ = static_cast<unsigned>(workers_.size());
    ++generation_;
  }
  wake_.notify_all();
  run_pieces();
  std::unique_lock<std::mutex> lock(mutex_);
  done_.wait(lock, [this] { return busy_ == 0; });
  body_ = nullptr;
}

void ThreadPool::worker_loop() {
#if defined(__APPLE__)
  // Ask for the performance cores first, like the thread that renders the frame.
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
  std::uint64_t seen = 0;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
      if (stopping_) return;
      seen = generation_;
    }
    run_pieces();
    std::lock_guard<std::mutex> lock(mutex_);
    if (--busy_ == 0) done_.notify_one();
  }
}

void ThreadPool::run_pieces() {
  for (;;) {
    const std::size_t begin = next_.fetch_add(grain_, std::memory_order_relaxed);
    if (begin >= count_) return;
    (*body_)(begin, std::min(begin + grain_, count_));
  }
}

}  // namespace rasty
