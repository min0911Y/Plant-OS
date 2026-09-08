#include "runtime_lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

typedef enum { EXIT_PLAIN, EXIT_OBJECT, EXIT_QUICK, EXIT_FINISHED } exit_kind_t;
typedef struct {
  exit_kind_t kind;
  union {
    void (*plain)(void);
    void (*object)(void *);
  } function;
  void *argument;
  void *dso;
} exit_callback_t;
static exit_callback_t *callbacks;
static size_t callback_count, callback_capacity;
pthread_mutex_t runtime_exit_lock = PTHREAD_MUTEX_INITIALIZER;
extern void *__dso_handle __attribute__((visibility("hidden")));

static int exit_callback_add(exit_callback_t callback) {
  pthread_mutex_lock(&runtime_exit_lock);
  if (callback_count == callback_capacity) {
    size_t capacity = callback_capacity ? callback_capacity * 2 : 16;
    if (capacity < callback_capacity ||
        capacity > (size_t)-1 / sizeof(*callbacks)) {
      pthread_mutex_unlock(&runtime_exit_lock);
      return -1;
    }
    exit_callback_t *replacement =
        realloc(callbacks, capacity * sizeof(*callbacks));
    if (!replacement) {
      pthread_mutex_unlock(&runtime_exit_lock);
      return -1;
    }
    callbacks = replacement;
    callback_capacity = capacity;
  }
  callbacks[callback_count++] = callback;
  pthread_mutex_unlock(&runtime_exit_lock);
  return 0;
}
int atexit(void (*callback)(void)) {
  if (!callback)
    return -1;
  return exit_callback_add((exit_callback_t){
      .kind = EXIT_PLAIN, .function.plain = callback, .dso = __dso_handle});
}
int __cxa_atexit(void (*callback)(void *), void *argument, void *dso) {
  if (!callback)
    return -1;
  return exit_callback_add((exit_callback_t){.kind = EXIT_OBJECT,
                                             .function.object = callback,
                                             .argument = argument,
                                             .dso = dso});
}
static void exit_callbacks_run(void *dso, bool quick) {
  pthread_mutex_lock(&runtime_exit_lock);
  size_t index = callback_count;
  while (index) {
    exit_callback_t callback = callbacks[--index];
    if (callback.kind == EXIT_FINISHED ||
        (callback.kind == EXIT_QUICK) != quick || (dso && callback.dso != dso))
      continue;
    callbacks[index].kind = EXIT_FINISHED;
    size_t before = callback_count;
    pthread_mutex_unlock(&runtime_exit_lock);
    if (callback.kind != EXIT_OBJECT)
      callback.function.plain();
    else
      callback.function.object(callback.argument);
    pthread_mutex_lock(&runtime_exit_lock);
    if (callback_count > before || index > callback_count)
      index = callback_count;
  }
  while (callback_count && callbacks[callback_count - 1].kind == EXIT_FINISHED)
    callback_count--;
  pthread_mutex_unlock(&runtime_exit_lock);
}
void __cxa_finalize(void *dso) { exit_callbacks_run(dso, false); }
int at_quick_exit(void (*callback)(void)) {
  if (!callback)
    return -1;
  return exit_callback_add(
      (exit_callback_t){.kind = EXIT_QUICK, .function.plain = callback});
}
void quick_exit(int status) {
  exit_callbacks_run(NULL, true);
  _Exit(status);
}
void exit(int status) {
  runtime_thread_finalize();
  __cxa_finalize(NULL);
  runtime_finalize();
  __cxa_finalize(NULL);
  stdio_shutdown();
  _Exit(status);
}
