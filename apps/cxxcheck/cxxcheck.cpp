#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <syscall.h>
#include <system_error>
#include <thread>
#include <vector>

static std::atomic<unsigned> constructed, destroyed;
struct Local {
  Local() { constructed++; }
  ~Local() { destroyed++; }
};
static const std::string &shared_value() {
  static const std::string value = [] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return std::string("native C++");
  }();
  return value;
}

int main() {
  std::mutex mutex;
  std::condition_variable condition;
  unsigned arrived = 0;
  bool released = false;
  std::vector<std::thread> threads;
  std::vector<std::future<unsigned>> results;
  for (unsigned i = 0; i < 4; i++) {
    std::promise<unsigned> promise;
    results.push_back(promise.get_future());
    threads.emplace_back([&, i, promise = std::move(promise)]() mutable {
      thread_local Local local;
      bool valid = shared_value() == "native C++";
      std::unique_lock<std::mutex> lock(mutex);
      arrived++;
      condition.notify_all();
      condition.wait(lock, [&] { return released; });
      lock.unlock();
      promise.set_value(valid ? 42 + i : 0);
    });
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&] { return arrived == threads.size(); });
    released = true;
  }
  condition.notify_all();
  bool valid = true;
  for (unsigned i = 0; i < threads.size(); i++) {
    valid &= results[i].get() == 42 + i;
    threads[i].join();
  }
  valid &= constructed == 4 && destroyed == 4;
  std::ostringstream text;
  text << std::hex << 255 << ' ' << std::fixed << std::setprecision(2) << 1.25;
  valid &= text.str() == "ff 1.25";
  valid &= std::error_code(EINVAL, std::generic_category()).message() ==
           "Invalid argument";
  struct alignas(256) Aligned {
    unsigned value;
  };
  auto *aligned = new Aligned{29};
  valid &=
      aligned->value == 29 && !(reinterpret_cast<uintptr_t>(aligned) & 255);
  delete aligned;
  auto asynchronous = std::async(std::launch::async, [] { return 13; });
  valid &= asynchronous.get() == 13;
  std::random_device random;
  unsigned first = random(), changed = 0;
  for (unsigned i = 0; i < 32; i++)
    changed |= random() ^ first;
  valid &= changed != 0 && random.entropy() == 0;
  std::cout << "CXXCHECK " << text.str() << std::endl;
  logk(valid ? (char *)"CXXCHECK PASS\n" : (char *)"CXXCHECK FAIL\n");
  return valid ? 0 : 1;
}
