// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

struct llvm::sys::Watchdog::State {
  std::mutex Mutex;
  std::condition_variable Changed;
  bool Cancelled = false;
  std::thread Worker;

  explicit State(unsigned Seconds)
      : Worker([this, Seconds] {
          std::unique_lock Lock(Mutex);
          if (!Changed.wait_for(Lock, std::chrono::seconds(Seconds),
                                [this] { return Cancelled; }))
            std::_Exit(124);
        }) {}

  ~State() {
    {
      std::lock_guard Lock(Mutex);
      Cancelled = true;
    }
    Changed.notify_one();
    Worker.join();
  }
};

llvm::sys::Watchdog::Watchdog(unsigned Seconds)
    : Timer(Seconds ? new State(Seconds) : nullptr) {}
llvm::sys::Watchdog::~Watchdog() { delete Timer; }
