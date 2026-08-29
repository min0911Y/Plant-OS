#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <task.h>

static const char *task_state_name(uint32_t state) {
  static const char *const names[] = {"run", "wait", "sleep", "zombie"};
  return state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

int main(void) {
  task_info_t *tasks = NULL;
  size_t count = 0;
  if (task_list(&tasks, &count) < 0) {
    printf("ps: unable to read task snapshot\n");
    return 1;
  }

  char line[160];
  sprintf(line, "CPUs online: %u\n", cpu_count());
  print(line);
  logk(line);
  print(" TID TGID CPU STATE    TIME NAME\n");
  logk(" TID TGID CPU STATE    TIME NAME\n");
  for (size_t i = 0; i < count; i++) {
    task_info_t *task = &tasks[i];
    sprintf(line, "%4u %4u %3u%c %-7s %5u %s\n", task->tid, task->tgid,
            task->cpu, task->flags & TASK_INFO_FLAG_ON_CPU ? '*' : ' ',
            task_state_name(task->state), (uint32_t)task->runtime_ms,
            task->name[0] ? task->name : "-");
    print(line);
    logk(line);
  }
  free(tasks);
  return 0;
}
