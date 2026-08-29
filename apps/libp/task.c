#include <stdlib.h>
#include <task.h>

enum { TASK_SNAPSHOT_CAPACITY = -2 };

int api_task_snapshot(task_info_t *entries, uint32_t capacity,
                      uint32_t *count);

int task_list(task_info_t **entries, size_t *count) {
  if (entries == NULL || count == NULL) {
    return -1;
  }
  *entries = NULL;
  *count = 0;

  for (;;) {
    uint32_t required = 0;
    int result = api_task_snapshot(NULL, 0, &required);
    if (result < 0) {
      return result;
    }
    if (required == 0) {
      return 0;
    }

    task_info_t *snapshot = malloc(required * sizeof(*snapshot));
    if (snapshot == NULL) {
      return -1;
    }
    uint32_t capacity = required;
    result = api_task_snapshot(snapshot, capacity, &required);
    if (result == TASK_SNAPSHOT_CAPACITY) {
      free(snapshot);
      continue;
    }
    if (result < 0) {
      free(snapshot);
      return result;
    }
    *entries = snapshot;
    *count = required;
    return 0;
  }
}
