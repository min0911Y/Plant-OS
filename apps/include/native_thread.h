#ifndef PLANT_NATIVE_THREAD_H
#define PLANT_NATIVE_THREAD_H

#include <ctypes.h>

enum { SYSCALL_THREAD = 0x68 };
enum {
  THREAD_CREATE,
  THREAD_SET_POINTER,
  THREAD_GET_POINTER,
  THREAD_JOIN,
  THREAD_DETACH,
  THREAD_TERMINATE,
  THREAD_WAIT_GROUP,
  THREAD_EXIT_PROCESS,
  THREAD_OPERATION_COUNT,
};
enum { THREAD_JOINABLE = 1 };

typedef struct {
  uintptr_t address;
  size_t size;
} thread_region_t;

typedef struct {
  uintptr_t entry, stack_top, argument, thread_pointer;
  thread_region_t tls, stack;
  uint32_t tid, generation, flags;
  char name[32];
} native_thread_request_t;

#ifdef __cplusplus
static_assert(sizeof(native_thread_request_t) ==
                  (sizeof(uintptr_t) == 8 ? 112 : 76),
              "native thread ABI");
extern "C" {
#else
_Static_assert(sizeof(native_thread_request_t) ==
                   (sizeof(uintptr_t) == 8 ? 112 : 76),
               "native thread ABI");
#endif

/* CREATE transfers the supplied VM regions only on success. The kernel
 * releases them after the thread stops accessing user memory. A zero-sized
 * stack region leaves ownership of a caller-provided stack with the caller.
 * JOIN/DETACH/TERMINATE validate both TID and generation within this process.
 * GET_POINTER accepts NULL; other operations use a complete request. */
intptr_t native_thread_call(unsigned operation,
                            native_thread_request_t *request);
uintptr_t native_thread_pointer(void);

#ifdef __cplusplus
}
#endif
#endif
