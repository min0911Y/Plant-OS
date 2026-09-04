#include <arch/x86/interrupt.h>
#include <dos.h>
typedef struct signal_frame {
  unsigned eip1;
  unsigned edi;
  unsigned esi;
  unsigned ebp;
  unsigned esp_dummy;

  unsigned ebx;
  unsigned edx;
  unsigned ecx;
  unsigned eax;

  unsigned gs;
  unsigned fs;
  unsigned es;
  unsigned ds;

  unsigned eip;
} signal_frame_t;

// TODO: 给GUI接管
void signal_deal(void) {
  mtask *task = current_task();
  if (task == NULL || task->tid == NULL_TID || task->signal_disable) {
    return;
  }
  if (task->signal & SIGMASK(SIGINT)) {
    task->signal &= ~SIGMASK(SIGINT);
    if (task->handler[SIGINT]) {
      x86_interrupt_frame_t *i =
          (x86_interrupt_frame_t *)(task->top - sizeof(x86_interrupt_frame_t));
      signal_frame_t *frame =
          (signal_frame_t *)(uintptr_t)(i->esp - sizeof(signal_frame_t));
      frame->edi = i->edi;
      frame->esi = i->esi;
      frame->ebp = i->ebp;
      frame->esp_dummy = i->esp_dummy;
      frame->ebx = i->ebx;
      frame->ecx = i->ecx;
      frame->edx = i->edx;
      frame->eax = i->eax;
      frame->gs = i->gs;
      frame->fs = i->fs;
      frame->es = i->es;
      frame->ds = i->ds;
      frame->eip = i->eip;
      frame->eip1 = task->ret_to_app;
      i->eip = task->handler[SIGINT];
      i->esp = (uintptr_t)frame;
    }
  } else if (task->signal & SIGMASK(SIGKIL)) {
    task_exit(0);
  }
}

void set_signal_handler(unsigned sig, unsigned handler) {
  current_task()->handler[sig] = handler;
}
