module;

#include "../prelude.hpp"
#include <pthread.h>
#include <type_traits>

export module execution;

// A synchronous pass owns its callable until every worker has crossed the
// barrier. Workers sleep between passes; only disjoint row ranges are written.
export class RenderWorkers {
  std::vector<pthread_t> threads;
  std::mutex mutex;
  std::condition_variable ready, finished;
  uint64_t generation = 0;
  bool stopping = false;
  size_t remaining = 0, count = 0, grain = 1;
  std::atomic_size_t next{0};
  void *context = nullptr;
  void (*invoke)(void *, size_t, size_t) = nullptr;

  void drain() {
    for (;;) {
      size_t begin = next.fetch_add(grain, std::memory_order_relaxed);
      if (begin >= count)
        return;
      invoke(context, begin, std::min(count, begin + grain));
    }
  }

  void run() {
    uint64_t seen = 0;
    std::unique_lock lock(mutex);
    for (;;) {
      ready.wait(lock, [&] { return stopping || generation != seen; });
      if (stopping)
        return;
      seen = generation;
      lock.unlock();
      drain();
      lock.lock();
      if (--remaining == 0)
        finished.notify_one();
    }
  }

public:
  RenderWorkers() = default;
  RenderWorkers(const RenderWorkers &) = delete;
  RenderWorkers &operator=(const RenderWorkers &) = delete;
  ~RenderWorkers() { stop(); }

  // Call only between frames. Failure joins all successfully created workers.
  bool start(size_t workers) {
    stop();
    stopping = false;
    generation = 0;
    threads.resize(workers);
    for (size_t i = 0; i < workers; ++i) {
      int status = pthread_create(
          &threads[i], nullptr,
          [](void *self) -> void * {
            static_cast<RenderWorkers *>(self)->run();
            return nullptr;
          },
          this);
      if (status != 0) {
        threads.resize(i);
        stop();
        return false;
      }
    }
    return true;
  }

  void stop() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    ready.notify_all();
    for (auto thread : threads)
      pthread_join(thread, nullptr);
    threads.clear();
  }

  template <class Function>
  static void rows(RenderWorkers *workers, size_t count, Function &&function) {
    if (!workers || workers->threads.empty() || count < 2) {
      function(0, count);
      return;
    }
    {
      std::lock_guard lock(workers->mutex);
      workers->count = count;
      workers->grain =
          std::max<size_t>(1, count / (4 * (workers->threads.size() + 1)));
      workers->next.store(0, std::memory_order_relaxed);
      workers->context = &function;
      workers->invoke = [](void *context, size_t begin, size_t end) {
        (*static_cast<std::remove_reference_t<Function> *>(context))(begin,
                                                                     end);
      };
      workers->remaining = workers->threads.size();
      ++workers->generation;
    }
    workers->ready.notify_all();
    workers->drain();
    std::unique_lock lock(workers->mutex);
    workers->finished.wait(lock, [&] { return workers->remaining == 0; });
  }
};
