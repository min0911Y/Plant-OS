#include <dos.h>

// TODO: 给GUI接管
void signal_deal(void) {
  mtask *task = current_task();
  if (task == NULL || task->tid == NULL_TID || task->signal_disable) {
    return;
  }
  if (task->signal & SIGMASK(SIGINT)) {
    task->signal &= ~SIGMASK(SIGINT);
    if (task->handler[SIGINT]) {
      arch_task_prepare_signal(task, task->handler[SIGINT], task->ret_to_app);
    }
  } else if (task->signal & SIGMASK(SIGKIL)) {
    task_exit(0);
  }
}

void set_signal_handler(unsigned sig, uintptr_t handler) {
  current_task()->handler[sig] = handler;
}
