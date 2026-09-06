#include "runtime_lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

typedef enum { EXIT_PLAIN, EXIT_OBJECT, EXIT_FINISHED } exit_kind_t;
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
void *__dso_handle = &__dso_handle;

static int exit_callback_add(exit_callback_t callback) {
  if (callback_count == callback_capacity) {
    size_t capacity = callback_capacity ? callback_capacity * 2 : 16;
    if (capacity < callback_capacity ||
        capacity > (size_t)-1 / sizeof(*callbacks))
      return -1;
    exit_callback_t *replacement =
        realloc(callbacks, capacity * sizeof(*callbacks));
    if (!replacement)
      return -1;
    callbacks = replacement;
    callback_capacity = capacity;
  }
  callbacks[callback_count++] = callback;
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
void __cxa_finalize(void *dso) {
  size_t index = callback_count;
  while (index) {
    exit_callback_t callback = callbacks[--index];
    if (callback.kind == EXIT_FINISHED || (dso && callback.dso != dso))
      continue;
    callbacks[index].kind = EXIT_FINISHED;
    size_t before = callback_count;
    if (callback.kind == EXIT_PLAIN)
      callback.function.plain();
    else
      callback.function.object(callback.argument);
    if (callback_count > before)
      index = callback_count;
  }
  while (callback_count && callbacks[callback_count - 1].kind == EXIT_FINISHED)
    callback_count--;
}
void exit(unsigned status) {
  __cxa_finalize(NULL);
  runtime_finalize();
  __cxa_finalize(NULL);
  stdio_shutdown();
  _exit(status);
}
