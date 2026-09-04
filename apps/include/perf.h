#ifndef PLANT_OS_PERF_H
#define PLANT_OS_PERF_H

#include <ctypes.h>

typedef enum {
  PERF_STATE_UNAVAILABLE,
  PERF_STATE_IDLE,
  PERF_STATE_RUNNING,
  PERF_STATE_DUMPING,
} perf_state_t;

typedef struct {
  uint32_t state;
  uint32_t samples;
  uint32_t stacks;
  uint32_t dropped;
  uint32_t capacity;
} perf_status_t;

typedef enum {
  PERF_CONTROL_START,
  PERF_CONTROL_STOP,
  PERF_CONTROL_STATUS,
  PERF_CONTROL_COUNT,
} perf_control_operation_t;

typedef struct {
  uint32_t size;
  uint32_t operation;
  perf_status_t status;
} perf_control_request_t;

enum {
  PERF_OK = 0,
  PERF_ERR_UNAVAILABLE = -1,
  PERF_ERR_STATE = -2,
  PERF_ERR_INVALID = -3,
};

#ifdef __cplusplus
static_assert(sizeof(perf_status_t) == 20, "perf status ABI size");
static_assert(sizeof(perf_control_request_t) == 28,
              "perf control request ABI size");
extern "C" {
#else
_Static_assert(sizeof(perf_status_t) == 20, "perf status ABI size");
_Static_assert(sizeof(perf_control_request_t) == 28,
               "perf control request ABI size");
#endif

int perf_control(perf_control_request_t *request);

#ifdef __cplusplus
}
#endif

#endif
