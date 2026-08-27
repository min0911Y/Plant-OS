// 多任务重构 -- mtask.c (区别与以前的多任务)
#include <arch.h>
#include <arch/x86/control.h>
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <irq.h>
#include <user_space.h>
#define STACK_SIZE 1024 * 1024
#define REAPER_TID 0u
#define TASK_ID_NONE ((uint32_t)-1)
#define TASK_KILLED_STATUS ((unsigned)-1)
void free_pde(unsigned addr);
unsigned pde_clone(unsigned addr);
void gc(unsigned tid);
void task_start(mtask *task);
void task_switch(mtask *next);
static void reset_task_slot(mtask *task, int tid);
char default_drive = 'A';
mtask m[255];
mtask *idle_task;
mtask *current = NULL;
char mtask_stop_flag = 0;
void task_set_default_drive(char drive) {
  if (drive >= 'a' && drive <= 'z') {
    drive -= 'a' - 'A';
  }
  if (drive < 'A' || drive > 'Z') {
    return;
  }
  default_drive = drive;
}
mtask *next_set = NULL;
mtask null_task;
static void init_task() {
  for (int i = 0; i < 255; i++) {
    m[i].jiffies = 0;   // 最后一次执行的全局时间片
    m[i].user_mode = 0; // 此项暂时废除
    m[i].running = 0;
    m[i].timeout = 0;
    m[i].state = EMPTY; // EMPTY
    m[i].tid = i;       // task id
    m[i].ptid = -1;     // parent task id
    m[i].tgid = i;
    m[i].generation = 0;
    m[i].kind = TASK_PROCESS;
    /* keyboard hook */
    m[i].keyboard_press = NULL;
    m[i].keyboard_release = NULL;
    m[i].urgent = 0;
    m[i].fpu_flag = 0;
    m[i].fifosleep = 0;
    m[i].mx = 0;
    m[i].my = 0;
    m[i].line = NULL;
    m[i].timer = NULL;
    m[i].nfs = NULL;
    m[i].mm = NULL;
    m[i].waittid = -1;
    m[i].wait_generation = 0;
    m[i].wait_reason = WAIT_REASON_NONE;
    m[i].group_lock_owner = TASK_ID_NONE;
    m[i].group_lock_depth = 0;
    m[i].alloc_addr = 0;
    m[i].alloc_size = 0;
    m[i].alloced = 0;
    m[i].ready = 0;
    m[i].pde = 0;
    m[i].Pkeyfifo = NULL;
    m[i].Ukeyfifo = NULL;
    m[i].sigint_up = 0;
    m[i].train = 0;
    m[i].signal_disable = 0;
    m[i].times = 0;
    m[i].keyboard_press = NULL;
    m[i].keyboard_release = NULL;
    ipc_task_init(&m[i]);
    for (int k = 0; k < 30; k++) {
      m[i].handler[k] = 0;
    }
  }
}
fpu_t public_fpu;
bool task_check_train(mtask *task) {
  if (!task) {
    return false;
  }
  if (task->train == 1 && timerctl.count - task->jiffies >= 5) {
    return true;
  }
  return false;
}
extern mtask *mouse_use_task;
void task_next() {
  // io_sti();
  if (current->running < current->timeout - 1 && current->state == RUNNING &&
      next_set == NULL) {
    current->running++;
    return; // 不需要调度，当前时间片仍然属于你
  }
  if (!next_set)
    current->running = 0;
  mtask *next = NULL;
  int i;
  if (next_set) {
    i = next_set->tid;
    next_set = NULL;
  } else {
    i = 0;
  }
  for (; i < 255; i++) {
    mtask *p = (&(m[i]));
    if (p == current) {
      continue;
    }
    if (p->state != RUNNING) // RUNNING
    {
      if (p->state == READY) {
        p->state = EMPTY;
      }
      if (p->state == WAITING) {
        if (p->ready) {
          p->ready = 0;
          p->state = RUNNING;
          p->wait_reason = WAIT_REASON_NONE;
          goto OK;
        }
      }
      continue;
    }
  OK:
    if (p->urgent) {
      next = p;
      break;
    }
    if (!next || p->jiffies < next->jiffies || p->running)
      if (!next || !task_check_train(next) ||
          (task_check_train(next) && task_check_train(p))) {
        next = p;
      }
  }
  if (next == NULL) {
    next = idle_task;
  }
  if (next->user_mode == 1) {
    arch_task_set_kernel_stack(next->top);
  }
  if (next->urgent) {
    next->urgent = 0;
  }
  if (next->ready) {
    next->ready = 0;
  }
  int current_fpu_flag = current->fpu_flag;
  fpu_t *current_fpu = &(current->fpu);
  x86_cr0_write(x86_cr0_read() & ~(X86_CR0_EM | X86_CR0_TS));
  if (current_fpu && current_fpu_flag)
    asm volatile("fnsave (%%eax) \n" ::"a"(current_fpu));
  next->jiffies = global_time;
  x86_fpu_disable();
  if (current_task()->state == WILL_EMPTY) {
    current_task()->state = READY;
  }
  
  task_switch(next); // 调度
}

static mtask *create_task_impl(uintptr_t eip, unsigned esp, unsigned ticks,
                               unsigned floor, bool share_pde) {
  (void)esp;
  mtask *t = NULL;
  irq_state_t interrupt_state = irq_save();
  int first = (current == NULL && m[0].state == EMPTY) ? 0 : 1;
  for (int i = first; i < 255; i++) {
    if (m[i].state == EMPTY) {
      t = &(m[i]);
      break;
    }
  }
  if (!t) {
    irq_restore(interrupt_state);
    return NULL;
  }
  int tid = (int)(t - m);
  uint32_t generation = t->generation + 1;
  reset_task_slot(t, tid);
  t->generation = generation;
  t->kind = share_pde ? TASK_THREAD : TASK_PROCESS;
  t->tgid = share_pde && current != NULL ? current_task()->tgid : (uint32_t)tid;
  t->ptid = share_pde && current != NULL ? current_task()->ptid : TASK_ID_NONE;
  t->state = ALLOCATING;
  irq_restore(interrupt_state);
  void *stack_base = page_malloc(STACK_SIZE);
  if (stack_base == NULL) {
    reset_task_slot(t, tid);
    return NULL;
  }
  uintptr_t esp_alloced = (uintptr_t)stack_base + STACK_SIZE;
  change_page_task_id(t->tid, (void *)(esp_alloced - STACK_SIZE), STACK_SIZE);
  t->esp = (stack_frame *)(esp_alloced - sizeof(stack_frame)); // switch用到的栈帧
  t->esp->eip = eip;                          // 设置跳转地址
  t->user_mode = 0;                           // 设置是否是user_mode
  bool owns_pde = false;
  if (current == NULL) {                      // 还没启用多任务
    t->pde = PDE_ADDRESS;                     // 所以先用预设好的页表
    t->times = PDE_ADDRESS;
  } else if (share_pde) {
    t->pde = current_task()->pde;
    t->times = t->pde;
    pde_retain(t->pde);
    owns_pde = true;
  } else {
    t->pde = pde_clone(current_task()->pde); // 启用了就复制一个
    if (t->pde == 0) {
      page_free(stack_base, STACK_SIZE);
      reset_task_slot(t, tid);
      return NULL;
    }
    t->times = t->pde;
    owns_pde = true;
  }
  t->top = esp_alloced; // r0的esp
  t->floor = floor;
  t->running = 0;
  t->timeout = ticks;
  t->drive = default_drive;
  t->drive_number = default_drive - 'A';
  t->jiffies = 0;
  extern int init_ok_flag; // init_ok_flag 标记fs等是否初始化完成
  if (init_ok_flag) {
    bool vfs_ready = current != NULL && current_task()->nfs != NULL
                         ? vfs_clone_for_task(current_task(), t)
                         : vfs_change_disk_for_task(t->drive, t);
    if (!vfs_ready) {
      if (owns_pde) {
        free_pde(t->pde);
      }
      page_free(stack_base, STACK_SIZE);
      reset_task_slot(t, tid);
      return NULL;
    }
    t->drive = t->nfs->drive;
    t->drive_number = t->drive - 'A';
  }
  return t;
}
mtask *create_task(uintptr_t eip, unsigned esp, unsigned ticks, unsigned floor) {
  return create_task_impl(eip, esp, ticks, floor, false);
}
mtask *create_thread_task(uintptr_t eip, unsigned esp, unsigned ticks,
                          unsigned floor) {
  return create_task_impl(eip, esp, ticks, floor, true);
}
bool task_publish(mtask *task) {
  irq_state_t state = irq_save();
  bool published = task != NULL && task->state == ALLOCATING;
  if (published) {
    task->state = RUNNING;
  }
  irq_restore(state);
  return published;
}
mtask *get_task(unsigned tid) {
  if (tid >= 255) {
    return NULL;
  }
  if (m[tid].state == EMPTY || m[tid].state == WILL_EMPTY ||
      m[tid].state == READY || m[tid].state == ALLOCATING) {
    return NULL;
  }
  return &(m[tid]);
}
void task_to_user_mode(unsigned eip, unsigned esp) {
  mtask *task = current;
  struct user_runtime_layout layout;
  if (!user_runtime_layout_calculate(USER_SPACE_START, 0, 0, false, eip,
                                     &layout) ||
      esp < USER_SPACE_START || esp >= USER_HEAP_END) {
    task_exit(-1);
    return;
  }
  (void)layout;
  logk("TTT %d\n", task->tid);
  x86_interrupt_frame_t iframe;

  x86_user_frame_init(&iframe, eip, esp);
  iframe.gs = 0;
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);
  // task_exit(0);
  // change_page_task_id(current_task()->tid, iframe->esp - 64 * 1024, 64 *
  // 1024);
  x86_return_to_user(&iframe);
}

static bool task_slot_in_use(const mtask *task) {
  return task != NULL && task->state != EMPTY && task->state != WILL_EMPTY &&
         task->state != READY && task->state != ALLOCATING;
}

static void task_clear_ipc_refs(mtask *task) {
  /* 丢掉自己队列里没读完的消息、注销服务名、唤醒等着给它发消息的任务 */
  ipc_task_cleanup(task);
}

static void task_clear_external_refs(mtask *task) {
  extern mtask *keyboard_use_task;
  extern int disable_flag;
  extern unsigned custom_handler;
  extern unsigned custom_handler_pde;
  extern mtask *custom_handler_owner;

  mtask *leader = get_task(task->tgid);
  if (leader && leader->group_lock_owner == task->tid) {
    leader->group_lock_owner = TASK_ID_NONE;
    leader->group_lock_depth = 0;
    for (int i = 0; i < 255; i++) {
      if (task_slot_in_use(&m[i]) && m[i].tgid == task->tgid &&
          m[i].state == WAITING &&
          m[i].wait_reason == WAIT_REASON_TASK_GROUP_LOCK) {
        task_run(&m[i]);
      }
    }
  }

  if (next_set == task) {
    next_set = NULL;
  }
  if (mouse_use_task == task) {
    mouse_sleep(&mdec);
  }
  if (keyboard_use_task == task) {
    keyboard_use_task = NULL;
    disable_flag = 0;
  }
  if (custom_handler_owner == task) {
    custom_handler = 0;
    custom_handler_pde = 0;
    custom_handler_owner = NULL;
  }
  timer_cancel_for_task(task);
  high_text_cursor_task_exited(task);
  task_clear_ipc_refs(task);
  sb16_remove_task(task);
  vdisk_remove_task(task->tid);
}

static void task_release_resources(mtask *task) {
  unsigned tid = task->tid;

  task_clear_external_refs(task);
  if (task == current_task()) {
    x86_cr3_write(PDE_ADDRESS);
  }
  if (task->pde && task->pde != PDE_ADDRESS) {
    free_pde(task->pde);
  }
  gc(tid);
  if (task->Pkeyfifo) {
    page_free(task->Pkeyfifo->buf, 4096);
    free(task->Pkeyfifo);
    task->Pkeyfifo = NULL;
  }
  if (task->Ukeyfifo) {
    page_free(task->Ukeyfifo->buf, 4096);
    free(task->Ukeyfifo);
    task->Ukeyfifo = NULL;
  }
  if (task->nfs) {
    vfs_free_task_instance(task->nfs);
    task->nfs = NULL;
  }
  if (task->alloced && task->alloc_size) {
    free(task->alloc_size);
  }
  if (task->timer != NULL) {
    struct FIFO8 *fifo = task->timer->fifo;
    if (fifo != NULL) {
      if (fifo->buf != NULL) {
        page_free(fifo->buf, 50 * sizeof(unsigned char));
      }
      page_free(fifo, sizeof(struct FIFO8));
    }
    timer_free(task->timer);
  }
  task->alloc_addr = 0;
  task->alloc_size = NULL;
  task->alloced = 0;
  task->urgent = 0;
  task->fpu_flag = 0;
  task->fifosleep = 0;
  task->mx = 0;
  task->my = 0;
  task->line = NULL;
  task->jiffies = 0;
  task->timer = NULL;
  task->mm = NULL;
  task->waittid = TASK_ID_NONE;
  task->wait_generation = 0;
  task->wait_reason = WAIT_REASON_NONE;
  task->running = 0;
  task->ready = 0;
  task->pde = 0;
  task->sigint_up = 0;
  task->train = 0;
  task->times = 0;
  task->signal = 0;
  task->signal_disable = 0;
  task->keyboard_press = NULL;
  task->keyboard_release = NULL;
  task->group_lock_owner = TASK_ID_NONE;
  task->group_lock_depth = 0;
  for (int k = 0; k < 30; k++) {
    task->handler[k] = 0;
  }
}

void task_abort_creation(mtask *task) {
  if (task == NULL || task->state != ALLOCATING) {
    return;
  }
  int tid = task->tid;
  task_release_resources(task);
  reset_task_slot(task, tid);
}

static void wake_child_waiter(mtask *child) {
  if (child->ptid == TASK_ID_NONE || child->ptid == REAPER_TID) {
    return;
  }
  for (int i = 0; i < 255; i++) {
    mtask *waiter = &m[i];
    if (!task_slot_in_use(waiter) || waiter->tgid != child->ptid ||
        waiter->state != WAITING ||
        waiter->wait_reason != WAIT_REASON_CHILD ||
        waiter->waittid != child->tid ||
        waiter->wait_generation != child->generation) {
      continue;
    }
    task_run(waiter);
  }
}

static void reparent_children(uint32_t old_parent, uint32_t new_parent) {
  for (int i = 0; i < 255; i++) {
    mtask *child = &m[i];
    if (!task_slot_in_use(child) || child->kind != TASK_PROCESS ||
        child->ptid != old_parent) {
      continue;
    }
    child->ptid = new_parent;
    for (int j = 0; j < 255; j++) {
      if (task_slot_in_use(&m[j]) && m[j].kind == TASK_THREAD &&
          m[j].tgid == child->tgid) {
        m[j].ptid = new_parent;
      }
    }
    if (child->state == DIED && new_parent == REAPER_TID) {
      reset_task_slot(child, i);
    }
  }
}

static void finish_task(mtask *task, unsigned status, bool waitable) {
  bool is_current = task == current_task();
  task_release_resources(task);
  task->status = status;
  if (waitable) {
    task->state = DIED;
    wake_child_waiter(task);
  } else if (is_current) {
    task->state = WILL_EMPTY;
  } else {
    reset_task_slot(task, task->tid);
  }
}

static void terminate_thread_group(uint32_t tgid, mtask *except) {
  for (int i = 0; i < 255; i++) {
    mtask *thread = &m[i];
    if (thread == except || !task_slot_in_use(thread) ||
        thread->kind != TASK_THREAD || thread->tgid != tgid) {
      continue;
    }
    finish_task(thread, TASK_KILLED_STATUS, false);
  }
}

void task_kill(unsigned tid) {
  mtask *task = get_task(tid);
  if (!task || task->state == DIED) {
    return;
  }
  irq_state_t interrupt_state = irq_save();
  bool is_current = task == current_task();
  if (task->kind == TASK_PROCESS) {
    terminate_thread_group(task->tgid, task);
    reparent_children(task->tgid, REAPER_TID);
  }
  bool waitable = task->kind == TASK_PROCESS && task->ptid != TASK_ID_NONE &&
                  task->ptid != REAPER_TID;
  finish_task(task, TASK_KILLED_STATUS, waitable);
  if (is_current) {
    io_sti();
    for (;;)
      ;
  }
  irq_restore(interrupt_state);
}

mtask *current_task() {
  if (current == NULL) {
    null_task.tid = NULL_TID;
    return &null_task;
  }
  return current;
}
int into_mtask() {
  init_task();
  x86_cr0_write(x86_cr0_read() & ~(X86_CR0_EM | X86_CR0_TS));
  asm volatile("fninit");
  asm volatile("fnsave (%%eax) \n" ::"a"(&public_fpu));
  x86_fpu_disable();
  arch_task_state_init();
  idle_task = create_task((uintptr_t)idle, 0, 1, 3);
  if (idle_task == NULL) {
    Panic_K("unable to create bootstrap tasks");
    return -1;
  }
  mtask *init_task = create_task((uintptr_t)init, 0, 5, 1);
  if (init_task == NULL) {
    task_abort_creation(idle_task);
    idle_task = NULL;
    Panic_K("unable to create bootstrap tasks");
    return -1;
  }
  if (!task_publish(idle_task) || !task_publish(init_task)) {
    Panic_K("unable to publish bootstrap tasks");
    return -1;
  }
  x86_cr0_write(x86_cr0_read() | X86_CR0_EM | X86_CR0_TS | X86_CR0_NE);
  task_start(&(m[0]));
  return 0;
}
void task_set_fifo(mtask *task, struct FIFO8 *kfifo, struct FIFO8 *mfifo) {
  task->keyfifo = kfifo;
  task->mousefifo = mfifo;
}
struct FIFO8 *task_get_key_fifo(mtask *task) { return task->keyfifo; }
void task_sleep(mtask *task) {
  task->state = SLEEPING;
  task->wait_reason = WAIT_REASON_GENERIC;
  task->fifosleep = 1;
}
void task_wake_up(mtask *task) {
  task->state = RUNNING;
  task->wait_reason = WAIT_REASON_NONE;
  task->fifosleep = 0;
}
void task_run(mtask *task) {
  if (!task || task->state == EMPTY || task->state == WILL_EMPTY ||
      task->state == READY || task->state == ALLOCATING || task->state == DIED) {
    return;
  }
  // 加急一下
  task->urgent = 1;
  task->ready = 1;
  task->running = 0;
}
void task_fifo_sleep(mtask *task) { task->fifosleep = 1; }
struct FIFO8 *task_get_mouse_fifo(mtask *task) { return task->mousefifo; }
static mtask *task_group_leader(mtask *task) {
  mtask *leader = get_task(task->tgid);
  return leader ? leader : task;
}

void task_lock() {
  mtask *self = current_task();
  mtask *leader = task_group_leader(self);
  for (;;) {
    irq_state_t interrupt_state = irq_save();
    if (leader->group_lock_owner == TASK_ID_NONE ||
        leader->group_lock_owner == self->tid) {
      leader->group_lock_owner = self->tid;
      leader->group_lock_depth++;
      irq_restore(interrupt_state);
      return;
    }
    self->state = WAITING;
    self->wait_reason = WAIT_REASON_TASK_GROUP_LOCK;
    self->ready = 0;
    io_sti();
    task_next();
  }
}

void task_unlock() {
  mtask *self = current_task();
  mtask *leader = task_group_leader(self);
  irq_state_t interrupt_state = irq_save();
  if (leader->group_lock_owner != self->tid || leader->group_lock_depth == 0) {
    irq_restore(interrupt_state);
    return;
  }
  leader->group_lock_depth--;
  if (leader->group_lock_depth == 0) {
    leader->group_lock_owner = TASK_ID_NONE;
    for (int i = 0; i < 255; i++) {
      if (task_slot_in_use(&m[i]) && m[i].tgid == self->tgid &&
          m[i].state == WAITING &&
          m[i].wait_reason == WAIT_REASON_TASK_GROUP_LOCK) {
        task_run(&m[i]);
      }
    }
  }
  irq_restore(interrupt_state);
}
uint32_t get_father_tid(mtask *t) {
  if (!t) {
    return TASK_ID_NONE;
  }
  if (t->ptid == TASK_ID_NONE || t->ptid == REAPER_TID) {
    return t->tgid;
  }
  mtask *parent = get_task(t->ptid);
  return parent ? get_father_tid(parent) : t->tgid;
}
void task_fall_blocked_reason(enum STATE state, enum WAIT_REASON reason) {
  if (current_task()->ready == 1) {
    current_task()->ready = 0;
    current_task()->wait_reason = WAIT_REASON_NONE;
    return;
  }
  current_task()->state = state;
  current_task()->wait_reason = reason;
  current_task()->ready = 0;
  io_sti();
  task_next();
}
void task_fall_blocked(enum STATE state) {
  task_fall_blocked_reason(state, WAIT_REASON_GENERIC);
}
void task_exit(unsigned status) {
  mtask *task = current_task();
  (void)irq_save();
  if (task->kind == TASK_PROCESS) {
    terminate_thread_group(task->tgid, task);
    reparent_children(task->tgid, REAPER_TID);
  }
  bool waitable = task->kind == TASK_PROCESS && task->ptid != TASK_ID_NONE &&
                  task->ptid != REAPER_TID;
  finish_task(task, status, waitable);
  io_sti();
  for (;;)
    ;
}
int waittid(uint32_t tid) {
  mtask *self = current_task();
  uint32_t generation;

  irq_state_t interrupt_state = irq_save();
  mtask *child = get_task(tid);
  if (!child || child->kind != TASK_PROCESS || child->ptid != self->tgid) {
    irq_restore(interrupt_state);
    return -1;
  }
  generation = child->generation;
  irq_restore(interrupt_state);

  for (;;) {
    interrupt_state = irq_save();
    child = get_task(tid);
    if (!child || child->generation != generation ||
        child->kind != TASK_PROCESS || child->ptid != self->tgid) {
      self->waittid = TASK_ID_NONE;
      self->wait_generation = 0;
      self->wait_reason = WAIT_REASON_NONE;
      irq_restore(interrupt_state);
      return -1;
    }
    if (child->state == DIED) {
      unsigned status = child->status;
      self->waittid = TASK_ID_NONE;
      self->wait_generation = 0;
      self->wait_reason = WAIT_REASON_NONE;
      reset_task_slot(child, tid);
      irq_restore(interrupt_state);
      logk("task exit with code %d\n", status);
      return status;
    }
    self->waittid = tid;
    self->wait_generation = generation;
    self->wait_reason = WAIT_REASON_CHILD;
    self->state = WAITING;
    self->ready = 0;
    io_sti();
    task_next();
  }
}
void mtask_stop() { mtask_stop_flag = 1; }
void mtask_start() { mtask_stop_flag = 0; }
void mtask_run_now(mtask *obj) { next_set = obj; }
static bool copy_vfs(mtask *src, mtask *dest) {
  return vfs_clone_for_task(src, dest);
}
static void release_task_fifos(mtask *task) {
  if (task->Pkeyfifo) {
    page_free(task->Pkeyfifo->buf, 4096);
    free(task->Pkeyfifo);
    task->Pkeyfifo = NULL;
  }
  if (task->Ukeyfifo) {
    page_free(task->Ukeyfifo->buf, 4096);
    free(task->Ukeyfifo);
    task->Ukeyfifo = NULL;
  }
  if (task->keyfifo) {
    page_free(task->keyfifo->buf, 4096);
    page_free(task->keyfifo, sizeof(struct FIFO8));
    task->keyfifo = NULL;
  }
  if (task->mousefifo) {
    page_free(task->mousefifo->buf, 4096);
    page_free(task->mousefifo, sizeof(struct FIFO8));
    task->mousefifo = NULL;
  }
}
static bool clone_task_fifo(struct FIFO8 **dest, struct FIFO8 *src,
                            bool use_kernel_heap) {
  if (src == NULL) {
    *dest = NULL;
    return true;
  }
  if (use_kernel_heap) {
    *dest = malloc(sizeof(struct FIFO8));
    if (*dest == NULL) {
      return false;
    }
  } else {
    *dest = (struct FIFO8 *)page_malloc_one();
    if (*dest == NULL) {
      return false;
    }
  }
  memcpy(*dest, src, sizeof(struct FIFO8));
  (*dest)->buf = page_malloc(4096);
  if ((*dest)->buf == NULL) {
    if (use_kernel_heap) {
      free(*dest);
    } else {
      page_free(*dest, sizeof(struct FIFO8));
    }
    *dest = NULL;
    return false;
  }
  memcpy((*dest)->buf, src->buf, 4096);
  return true;
}
static void reset_task_slot(mtask *task, int tid) {
  uint32_t generation = task->generation;
  memset(task, 0, sizeof(mtask));
  task->tid = tid;
  task->ptid = TASK_ID_NONE;
  task->tgid = tid;
  task->generation = generation;
  task->kind = TASK_PROCESS;
  task->state = EMPTY;
  task->waittid = TASK_ID_NONE;
  task->wait_reason = WAIT_REASON_NONE;
  task->group_lock_owner = TASK_ID_NONE;
  ipc_task_init(task);
}
mtask *mtask_get_free() {
  mtask *t = NULL;
  for (int i = 1; i < 255; i++) {
    if (m[i].state == EMPTY) {
      logk("f:%d\n", i);
      t = &(m[i]);
      logk("%d\n", t->tid);
      break;
    }
  }
  return t;
}
// THE FUNCTION CAN ONLY BE CALLED IN USER MODE!!!!
void interrput_exit();
void roc() {
  logk("ROCT\n");
  for (;;)
    ;
}
static void build_fork_stack(mtask *task) {
  uintptr_t addr = task->top;
  addr -= sizeof(x86_interrupt_frame_t);
  x86_interrupt_frame_t *iframe = (x86_interrupt_frame_t *)addr;
  iframe->eax = 0;
  logk("iframe = %08x\n", iframe->eip);
  addr -= sizeof(stack_frame);
  stack_frame *sframe = (stack_frame *)addr;
  sframe->ebp = 0x114514;
  sframe->ebx = 0x114514;
  sframe->ecx = 0x114514;
  sframe->edx = 0x114514;
  sframe->eip = (uintptr_t)interrput_exit;

  task->esp = sframe;
}
int task_fork() {
  mtask *parent = current_task();
  irq_state_t state = irq_save();
  mtask *m = mtask_get_free();
  if (!m) {
    irq_restore(state);
    return -1;
  }
  logk("get free %08x\n", m);
  logk("current = %08x\n", get_tid(parent));
  int tid = m->tid;
  uint32_t generation = m->generation + 1;
  memcpy(m, parent, sizeof(mtask));
  m->tid = tid;
  m->generation = generation;
  m->kind = TASK_PROCESS;
  m->tgid = tid;
  m->ptid = parent->tgid;
  m->state = ALLOCATING;
  uintptr_t stack = (uintptr_t)page_malloc(STACK_SIZE);
  if (stack == 0) {
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  change_page_task_id(tid, (void *)stack, STACK_SIZE);
  uintptr_t old_stack_base = m->top - STACK_SIZE;
  uintptr_t old_esp = (uintptr_t)m->esp;
  uintptr_t esp_offset = old_esp - old_stack_base;
  memcpy((void *)stack, (void *)old_stack_base, STACK_SIZE);
  logk("s = %08x \n", old_stack_base);
  m->top = stack + STACK_SIZE;
  m->esp = (stack_frame *)(stack + esp_offset);
  m->nfs = NULL;
  m->Pkeyfifo = NULL;
  m->Ukeyfifo = NULL;
  m->keyfifo = NULL;
  m->mousefifo = NULL;
  m->timer = NULL;
  m->mm = NULL;
  m->alloced = 0;
  m->alloc_size = NULL;
  /* 消息队列不继承：父进程队列里的负载归父进程所有 */
  ipc_task_init(m);
  m->waittid = TASK_ID_NONE;
  m->wait_generation = 0;
  m->wait_reason = WAIT_REASON_NONE;
  m->group_lock_owner = TASK_ID_NONE;
  m->group_lock_depth = 0;
  m->ready = 0;
  m->urgent = 0;
  m->line = NULL;
  m->signal = 0;
  if (parent->alloced && parent->alloc_size) {
    m->alloc_size = malloc(sizeof(uint32_t));
    if (m->alloc_size == NULL) {
      page_free((void *)stack, STACK_SIZE);
      reset_task_slot(m, tid);
      irq_restore(state);
      return -1;
    }
    *(m->alloc_size) = *(parent->alloc_size);
    m->alloced = 1;
  } else {
    m->alloc_size = parent->alloc_size;
    m->alloced = 0;
  }
  if (!clone_task_fifo(&m->Pkeyfifo, parent->Pkeyfifo, true) ||
      !clone_task_fifo(&m->Ukeyfifo, parent->Ukeyfifo, true) ||
      !clone_task_fifo(&m->keyfifo, parent->keyfifo, false) ||
      !clone_task_fifo(&m->mousefifo, parent->mousefifo, false)) {
    release_task_fifos(m);
    if (m->alloced) {
      free(m->alloc_size);
    }
    page_free((void *)stack, STACK_SIZE);
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  logk("copy vfs\n");
  if (!copy_vfs(parent, m)) {
    release_task_fifos(m);
    if (m->alloced) {
      free(m->alloc_size);
    }
    page_free((void *)stack, STACK_SIZE);
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  m->pde = pde_clone(parent->pde);
  if (m->pde == 0) {
    vfs_free_task_instance(m->nfs);
    m->nfs = NULL;
    release_task_fifos(m);
    if (m->alloced) {
      free(m->alloc_size);
    }
    page_free((void *)stack, STACK_SIZE);
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  m->running = 0;
  m->jiffies = 0;
  m->timeout = 1;
  m->ptid = parent->tgid;
  m->tgid = tid;
  m->kind = TASK_PROCESS;
  m->tid = tid;
  logk("m->tid = %d\n", m->tid);
  tid = m->tid;
  logk("BUILD FORK STACK\n");
  build_fork_stack(m);
  if (!task_publish(m)) {
    task_abort_creation(m);
    irq_restore(state);
    return -1;
  }
  irq_restore(state);
  return tid;
}
