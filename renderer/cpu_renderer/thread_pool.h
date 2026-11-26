// A small fixed-size thread pool with one operation: run a loop body over a range, in pieces, on every
// thread at once. The CPU renderer uses it for each of its per-vertex, per-triangle and per-pixel loops.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rasty {

class ThreadPool {
 public:
  using Body = std::function<void(std::size_t begin, std::size_t end)>;

  // `threads` counts the calling thread too, so 1 means no extra threads; 0 means one per CPU core.
  explicit ThreadPool(unsigned threads);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  unsigned size() const { return static_cast<unsigned>(workers_.size()) + 1; }

  // Calls body(begin, end) for consecutive pieces of [0, count), each at most `grain` long, and returns
  // once every piece has finished. Pieces are handed out from a shared counter, so a thread that
  // finishes early (or runs on a faster core) takes more of them. The caller works on pieces too.
  void parallel_for(std::size_t count, std::size_t grain, const Body& body);

 private:
  void worker_loop();
  void run_pieces();

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_;  // a new loop is ready, or the pool is stopping
  std::condition_variable done_;  // the last worker finished its pieces
  std::uint64_t generation_ = 0;  // counts loops, so a worker can tell a new one from the last one
  unsigned busy_ = 0;             // workers still working on the current loop
  bool stopping_ = false;

  // The current loop; written under mutex_ before workers are woken.
  const Body* body_ = nullptr;
  std::size_t count_ = 0;
  std::size_t grain_ = 1;
  std::atomic<std::size_t> next_{0};  // start of the next piece to hand out
};

}  // namespace rasty
