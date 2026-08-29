#ifndef KERNEL_TASK_SNAPSHOT_H
#define KERNEL_TASK_SNAPSHOT_H

#include <ctypes.h>

enum {
  TASK_INFO_NAME_MAX = 32,
  TASK_INFO_FLAG_ON_CPU = 1u << 0,
  TASK_SNAPSHOT_CAPACITY = -2,
};

typedef enum {
  TASK_INFO_RUNNING,
  TASK_INFO_WAITING,
  TASK_INFO_SLEEPING,
  TASK_INFO_ZOMBIE,
} task_info_state_t;

typedef struct {
  uint32_t tid;
  uint32_t tgid;
  uint32_t ptid;
  uint32_t generation;
  uint32_t cpu;
  uint32_t state;
  uint32_t kind;
  uint32_t flags;
  uint64_t runtime_ms;
  char name[TASK_INFO_NAME_MAX];
} task_info_t;

#ifdef __cplusplus
static_assert(sizeof(task_info_t) == 72, "task snapshot ABI size");
#else
_Static_assert(sizeof(task_info_t) == 72, "task snapshot ABI size");
#endif

#endif
