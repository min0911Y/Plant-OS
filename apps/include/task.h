#ifndef PLANT_OS_TASK_H
#define PLANT_OS_TASK_H

#include <ctypes.h>
#include <stddef.h>

enum {
  TASK_INFO_NAME_MAX = 32,
  TASK_INFO_FLAG_ON_CPU = 1u << 0,
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

/* Allocated snapshot in ascending TID order; release it with free(). */
#ifdef __cplusplus
extern "C" {
#endif
int task_list(task_info_t **entries, size_t *count);
static inline const task_info_t *
task_snapshot_find(const task_info_t *entries, size_t count, unsigned tid) {
  size_t first = 0, last = count;
  while (first < last) {
    size_t middle = first + (last - first) / 2;
    if (entries[middle].tid < tid)
      first = middle + 1;
    else
      last = middle;
  }
  return first < count && entries[first].tid == tid ? &entries[first] : NULL;
}
unsigned cpu_count(void);
unsigned cpu_current(void);
#ifdef __cplusplus
}
#endif

#endif
