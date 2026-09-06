#ifndef PLANT_LOADER_H
#define PLANT_LOADER_H

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
typedef struct {
  void (*initialize)(void);
  void (*finalize)(void);
} runtime_linker_t;

#endif
