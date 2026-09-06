#include "runtime_lifecycle.h"
#include <stdbool.h>

typedef void (*initializer_t)(void);
extern initializer_t __init_array_start[] __attribute__((weak));
extern initializer_t __init_array_end[] __attribute__((weak));
extern initializer_t __fini_array_start[] __attribute__((weak));
extern initializer_t __fini_array_end[] __attribute__((weak));
static bool initialized;
static const runtime_linker_t *dynamic_linker;

void runtime_initialize(const runtime_linker_t *linker) {
  dynamic_linker = linker;
  initialized = true;
  if (linker) {
    linker->initialize();
  } else if (__init_array_start && __init_array_end) {
    for (initializer_t *entry = __init_array_start; entry != __init_array_end;
         entry++) {
      (*entry)();
    }
  }
}

void runtime_finalize(void) {
  if (!initialized)
    return;
  initialized = false;
  if (dynamic_linker) {
    dynamic_linker->finalize();
  } else if (__fini_array_start && __fini_array_end) {
    for (initializer_t *entry = __fini_array_end;
         entry != __fini_array_start;) {
      (*--entry)();
    }
  }
}
