#ifndef PLANT_LOADER_H
#define PLANT_LOADER_H

#include "tls.h"
#include <ctypes.h>

/* Main receives NULL for a static application. PT_INTERP receives this
 * descriptor instead; the executable fd belongs to the new process. */
typedef struct {
  uint32_t size;
  int32_t executable_fd;
  const char *executable_path;
  const char *interpreter_path;
} loader_start_t;

/* The interpreter passes these hooks to the application's Main. Constructors
 * run after libp initialization, and finalizers run through normal exit(). */
struct native_dl_info;
typedef struct {
  void (*initialize)(void);
  void (*finalize)(void);
  tls_control_t *(*tls_allocate)(size_t runtime_size, thread_region_t *region);
  int (*symbol)(const char *name, void **address);
  int (*address_info)(const void *address, struct native_dl_info *information);
  const char *executable_path;
} runtime_linker_t;

#ifdef __cplusplus
static_assert(sizeof(loader_start_t) == 8 + 2 * sizeof(void *),
              "loader startup ABI");
static_assert(sizeof(runtime_linker_t) == 6 * sizeof(void *),
              "runtime linker ABI");
#else
_Static_assert(sizeof(loader_start_t) == 8 + 2 * sizeof(void *),
               "loader startup ABI");
_Static_assert(sizeof(runtime_linker_t) == 6 * sizeof(void *),
               "runtime linker ABI");
#endif

#endif
