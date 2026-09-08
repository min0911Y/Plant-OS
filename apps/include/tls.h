#ifndef PLANT_TLS_H
#define PLANT_TLS_H

#include "native_thread.h"

/* x86 ELF variant II: module storage precedes the thread pointer. */
typedef struct {
  void *self;
  void **modules;
  size_t module_count;
  void *runtime;
  int error_number;
  unsigned locale;
  const char *loader_error;
} __attribute__((aligned(16))) tls_control_t;

typedef struct {
  uintptr_t module, offset;
} tls_index_t;

static inline tls_control_t *tls_current(void) {
  tls_control_t *pointer;
#if __SIZEOF_POINTER__ == 8
  __asm__("mov %%fs:0,%0" : "=r"(pointer));
#else
  __asm__("mov %%gs:0,%0" : "=r"(pointer));
#endif
  return pointer;
}

#endif
