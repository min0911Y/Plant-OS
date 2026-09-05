#include <syscall.h>

int __cxa_guard_acquire(void *storage) {
  unsigned char *guard = storage;
  for (;;) {
    if (__atomic_load_n(guard, __ATOMIC_ACQUIRE))
      return 0;
    if (!__atomic_exchange_n(guard + 1, 1, __ATOMIC_ACQUIRE)) {
      if (!__atomic_load_n(guard, __ATOMIC_RELAXED))
        return 1;
      __atomic_store_n(guard + 1, 0, __ATOMIC_RELEASE);
      return 0;
    }
    api_yield();
  }
}
void __cxa_guard_release(void *storage) {
  unsigned char *guard = storage;
  __atomic_store_n(guard, 1, __ATOMIC_RELEASE);
  __atomic_store_n(guard + 1, 0, __ATOMIC_RELEASE);
}
void __cxa_guard_abort(void *storage) {
  unsigned char *guard = storage;
  __atomic_store_n(guard + 1, 0, __ATOMIC_RELEASE);
}
