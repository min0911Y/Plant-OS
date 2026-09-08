#ifndef LIBP_RUNTIME_LIFECYCLE_H
#define LIBP_RUNTIME_LIFECYCLE_H
#include <loader.h>
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
void runtime_initialize(const runtime_linker_t *linker);
void runtime_finalize(void);
int runtime_thread_initialize(const runtime_linker_t *linker);
void runtime_thread_finalize(void);
void runtime_thread_after_fork(void);
extern pthread_mutex_t runtime_environment_lock
    __attribute__((visibility("hidden")));
extern pthread_mutex_t runtime_exit_lock __attribute__((visibility("hidden")));
void runtime_stdio_fork_lock(void) __attribute__((visibility("hidden")));
void runtime_stdio_fork_unlock(bool child)
    __attribute__((visibility("hidden")));
void allocator_lock_acquire(void) __attribute__((visibility("hidden")));
void allocator_lock_release(void) __attribute__((visibility("hidden")));
#ifdef __cplusplus
}
#endif
#endif
