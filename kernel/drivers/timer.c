#include <arch/x86/io.h>
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

struct TIMERCTL timerctl;
#define TIMER_FLAGS_ALLOC 1 /* 已配置状态 */
#define TIMER_FLAGS_USING 2 /* 定时器运行中 */
extern int cg_flag0;
extern struct TASK *c_task;
void init_pit(void) {
  x86_port_write8(0x43, 0x34);
  x86_port_write8(0x40, 0x9c);
  x86_port_write8(0x40, 0x2e);

  int i;
  struct TIMER *t;
  timerctl.count = 0;
  for (i = 0; i < MAX_TIMER; i++) {
    timerctl.timers0[i].flags = 0; /* 没有使用 */
  }
  t = timer_alloc(); /* 取得一个 */
  if (t == NULL) {
    Panic_K("unable to allocate PIT sentinel timer");
    return;
  }
  t->timeout = 0xffffffff;
  t->flags = TIMER_FLAGS_USING;
  t->next = 0;     /* 末尾 */
  timerctl.t0 = t; /* 因为现在只有哨兵，所以他就在最前面*/
  timerctl.next =
      0xffffffff; /* 因为只有哨兵，所以下一个超时时刻就是哨兵的时刻 */
  return;
}

struct TIMER *timer_alloc(void) {
  int i;
  for (i = 0; i < MAX_TIMER; i++) {
    if (timerctl.timers0[i].flags == 0) {
      timerctl.timers0[i].flags = TIMER_FLAGS_ALLOC;
      timerctl.timers0[i].waiter = NULL;
      return &timerctl.timers0[i];
    }
  }
  return 0; /* 没找到 */
}

void timer_free(struct TIMER *timer) {
  if (timer == NULL) {
    return;
  }
  timer_cancel(timer);
  irq_state_t state = irq_save();
  timer->flags = 0; /* 未使用 */
  timer->waiter = NULL;
  timer->fifo = NULL;
  timer->next = NULL;
  irq_restore(state);
}

bool timer_cancel(struct TIMER *timer) {
  if (timer == NULL) {
    return false;
  }
  irq_state_t state = irq_save();
  if (timer->flags != TIMER_FLAGS_USING) {
    irq_restore(state);
    return false;
  }

  struct TIMER **link = &timerctl.t0;
  for (int i = 0; i < MAX_TIMER && *link != NULL; i++) {
    if (*link == timer) {
      *link = timer->next;
      timer->flags = TIMER_FLAGS_ALLOC;
      timer->next = NULL;
      timerctl.next = timerctl.t0 != NULL ? timerctl.t0->timeout : 0xffffffff;
      irq_restore(state);
      return true;
    }
    link = &(*link)->next;
  }
  timer->flags = TIMER_FLAGS_ALLOC;
  timer->next = NULL;
  irq_restore(state);
  return false;
}

void timer_cancel_for_task(mtask *task) {
  if (task == NULL) {
    return;
  }
  for (int i = 0; i < MAX_TIMER; i++) {
    struct TIMER *timer = &timerctl.timers0[i];
    if (timer->waiter == task) {
      timer_cancel(timer);
      timer->waiter = NULL;
      if (timer != task->timer) {
        timer_free(timer);
      }
    }
  }
}

void timer_init(struct TIMER *timer, struct FIFO8 *fifo, unsigned char data) {
  timer->fifo = fifo;
  timer->data = data;
  return;
}

void timer_settime(struct TIMER *timer, unsigned int timeout) {
  if (timer == NULL) {
    return;
  }
  timer_cancel(timer);
  irq_state_t state = irq_save();
  struct TIMER *t, *s;
  timer->timeout = timeout + timerctl.count;
  timer->flags = TIMER_FLAGS_USING;
  t = timerctl.t0;
  if (t == NULL) {
    timerctl.t0 = timer;
    timer->next = NULL;
    timerctl.next = timer->timeout;
    irq_restore(state);
    return;
  }
  if (timer->timeout <= t->timeout) {
    /* 插入最前面的情况 */
    timerctl.t0 = timer;
    timer->next = t; /* 下面是设定t */
    timerctl.next = timer->timeout;
    irq_restore(state);
    return;
  }
  for (;;) {
    s = t;
    t = t->next;
    if (timer->timeout <= t->timeout) {
      /* 插入s和t之间的情况 */
      s->next = timer; /* s下一个是timer */
      timer->next = t; /* timer的下一个是t */
      irq_restore(state);
      return;
    }
  }
}
void usleep(uint64_t nano);
void sleep(unsigned long long milliseconds) {
  uint64_t ticks64 = milliseconds / 10 + (milliseconds % 10 != 0);
  uint32_t ticks = ticks64 > UINT_MAX ? UINT_MAX : (uint32_t)ticks64;
  if (ticks == 0) {
    return;
  }

  mtask *task = current_task();
  if (task->tid == NULL_TID) {
    uint32_t started = timerctl.count;
    while (timerctl.count - started < ticks) {
    }
    return;
  }

  struct TIMER *timer = timer_alloc();
  if (timer == NULL) {
    WARNING_K("unable to allocate blocking sleep timer");
    return;
  }
  unsigned char value;
  struct FIFO8 fifo;
  fifo8_init(&fifo, 1, &value);
  timer_init(timer, &fifo, 1);
  timer->waiter = task;
  timer_settime(timer, ticks);
  task_fall_blocked_reason(WAITING, WAIT_REASON_TIMER);
  timer->waiter = NULL;
  timer_free(timer);
}

uint32_t mt2flag = 0;
int g = 0;
uint64_t global_time = 0;
void inthandler20(int cs, x86_interrupt_frame_t *frame) {
  (void)cs;
#ifdef KERNEL_PERF
  perf_sample(frame->eip, frame->ebp, (frame->cs & 3) == 3);
#else
  (void)frame;
#endif
   //logk("*");
  // printk("CS:EIP=%04x:%08x\n",current_task()->tss.cs,esp[-10]);
  send_eoi(0);
  apic_timer_on_interrupt();
  if (smp_current_cpu() != 0) {
    scheduler_tick();
    task_next();
    return;
  }
  global_time++;
  struct TIMER *timer;

  timerctl.count++;
  net_stack_tick();
  net_socket_tick();
  ipc_tick(); /* 唤醒等到超时的 IPC 等待者 */

  timer = timerctl.t0; /* 首先把最前面的地址赋给timer */
  if (timer == NULL) {
    timerctl.next = 0xffffffff;
    scheduler_tick();
    task_next();
    return;
  }
  for (;;) {
    /* 因为timers的定时器都处于运行状态，所以不确认flags */
    if (timer->timeout > timerctl.count) {
      break;
    }
    /* 超时 */
    timer->flags = TIMER_FLAGS_ALLOC;
    task_run(timer->waiter);
    fifo8_put(timer->fifo, timer->data);
    timer = timer->next; /* 将下一个定时器的地址赋给timer*/
  }
  timerctl.t0 = timer;
  timerctl.next = timer->timeout;

  scheduler_tick();
  task_next();
  // extern struct TIMER *mt_timer1, *mt_timer2, *mt_timer3;
  // extern int tasknum;
  // struct TIMER* timer;

  // timerctl.count++;
  // if (timerctl.next > timerctl.count) {
  //   return;
  // }
  // timer = timerctl.t0; /* 首先把最前面的地址赋给timer */
  // char ts = 0;
  // for (;;) {
  //   /* 因为timers的定时器都处于运行状态，所以不确认flags */
  //   if (timer->timeout > timerctl.count) {
  //     break;
  //   }
  //   /* 超时 */
  //   timer->flags = TIMER_FLAGS_ALLOC;
  //   if (timer == mt_timer3) {  // mt_timer3超时
  //     mt2flag = 0;
  //     if (count == 3) {
  //       mt2flag = 1;
  //       count = 0;
  //     }
  //     ts = 3;
  //     count++;
  //   } else if (timer == mt_timer2) {  // mt_timer2超时
  //     ts = 2;
  //   } else if (timer == mt_timer1) {  // mt_timer1超时
  //     ts = 1;
  //   } else {
  //     fifo8_put(timer->fifo, timer->data);
  //   }
  //   timer = timer->next; /* 将下一个定时器的地址赋给timer*/
  // }
  // timerctl.t0 = timer;
  // timerctl.next = timer->timeout;
  // printk("*");
  // extern int st_task;
  // if (ts == 3) {
  //   mt_taskswitch3();
  // }
  // if (ts == 2) {
  //   mt_taskswitch2();
  // }
  // if (ts == 1) {
  //   mt_taskswitch1();
  // }
}
