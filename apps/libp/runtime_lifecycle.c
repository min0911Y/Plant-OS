#include "runtime_lifecycle.h"
#include <stdbool.h>

typedef void (*initializer_t)(void);
extern initializer_t __init_array_start[] __attribute__((weak));
extern initializer_t __init_array_end[] __attribute__((weak));
extern initializer_t __fini_array_start[] __attribute__((weak));
extern initializer_t __fini_array_end[] __attribute__((weak));
static bool initialized;

void runtime_initialize_static(void) {
  if (__init_array_start && __init_array_end) {
    for (initializer_t *entry = __init_array_start; entry != __init_array_end;
         entry++) {
      (*entry)();
    }
  }
  initialized = true;
}

void runtime_finalize_static(void) {
  if (!initialized)
    return;
  initialized = false;
  if (__fini_array_start && __fini_array_end) {
    for (initializer_t *entry = __fini_array_end;
         entry != __fini_array_start;) {
      (*--entry)();
    }
  }
}
