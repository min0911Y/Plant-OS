// 多任务重构 -- mtask.c (区别与以前的多任务)
#include <arch.h>
#include <arch/x86/control.h>
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <smp.h>
#include <user_space.h>
#define STACK_SIZE 1024 * 1024
#define REAPER_TID 0u
#define TASK_ID_NONE ((uint32_t)-1)
#define TASK_KILLED_STATUS ((unsigned)-1)
void free_pde(unsigned addr);
unsigned pde_clone(unsigned addr);
void gc(unsigned tid);
static void reset_task_slot(mtask *task, int tid);
static void task_bootstrap(void);
static void task_finish_pending(mtask *task);
char default_drive = 'A';
mtask m[255];
typedef struct {
  mtask *current;
  mtask *idle;
  mtask *next;
  uint64_t min_vruntime;
  uint32_t need_resched;
} scheduler_cpu_t;

static scheduler_cpu_t scheduler_cpus[SMP_MAX_CPUS];
static uint32_t scheduler_cpu_total = 1;
static uint32_t scheduler_active;
void task_set_default_drive(char drive) {
  if (drive >= 'a' && drive <= 'z') {
    drive -= 'a' - 'A';
  }
  if (drive < 'A' || drive > 'Z') {
    return;
  }
  default_drive = drive;
}
mtask null_task;
static void init_task() {
  for (int i = 0; i < 255; i++) {
    m[i].vruntime = 0;
    m[i].runtime_ticks = 0;
    m[i].cpu = 0;
    m[i].on_cpu = 0;
    m[i].sched_flags = 0;
    m[i].user_mode = 0; // 此项暂时废除
    m[i].weight = 0;
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
    m[i].fpu_initialized = false;
    m[i].fifosleep = 0;
    m[i].mx = 0;
    m[i].my = 0;
    m[i].line = NULL;
    m[i].timer = NULL;
    m[i].fs_context = NULL;
    m[i].waittid = -1;
    m[i].wait_generation = 0;
    m[i].wait_reason = WAIT_REASON_NONE;
    m[i].group_lock_owner = TASK_ID_NONE;
    m[i].group_lock_depth = 0;
    m[i].alloc_addr = 0;
    m[i].alloc_size = 0;
    m[i].alloced = 0;
    m[i].TTY = NULL;
    m[i].tty_session = NULL;
    m[i].ready = 0;
    m[i].pde = 0;
    m[i].Pkeyfifo = NULL;
    m[i].Ukeyfifo = NULL;
    m[i].sigint_up = 0;
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
extern mtask *mouse_use_task;
static uint32_t scheduler_load(uint32_t cpu) {
  uint32_t load = 0;
  for (uint32_t i = 0; i < 255; i++) {
    mtask *task = &m[i];
    if (task->state == RUNNING && task->cpu == cpu &&
        !(task->sched_flags & TASK_SCHED_IDLE)) {
      load += task->weight ? task->weight : 1;
    }
  }
  return load;
}

static uint32_t scheduler_least_loaded_cpu(void) {
  uint32_t selected = 0;
  uint32_t selected_load = UINT_MAX;
  for (uint32_t cpu = 0; cpu < scheduler_cpu_total; cpu++) {
    if (!smp_cpu_online(cpu)) {
      continue;
    }
    uint32_t load = scheduler_load(cpu);
    if (load < selected_load) {
      selected = cpu;
      selected_load = load;
    }
  }
  return selected;
}

static void scheduler_place_task(mtask *task, uint32_t cpu) {
  scheduler_cpu_t *source = &scheduler_cpus[task->cpu];
  scheduler_cpu_t *target = &scheduler_cpus[cpu];
  uint64_t lag = task->vruntime > source->min_vruntime
                     ? task->vruntime - source->min_vruntime
                     : 0;
  task->cpu = cpu;
  task->vruntime =
      (target->min_vruntime > 1 ? target->min_vruntime - 1 : 0) + lag;
  target->need_resched = 1;
  smp_send_reschedule(cpu);
}

static int scheduler_task_eligible(const mtask *task, uint32_t cpu,
                                   const mtask *current) {
  return task->state == RUNNING && task->cpu == cpu &&
         !(task->sched_flags & TASK_SCHED_IDLE) &&
         (!task->on_cpu || task == current);
}

static mtask *scheduler_pick_next(scheduler_cpu_t *cpu, uint32_t cpu_index) {
  mtask *current = cpu->current;
  mtask *next = cpu->next;
  cpu->next = NULL;
  if (!scheduler_task_eligible(next, cpu_index, current)) {
    next = NULL;
  }

  for (uint32_t i = 0; i < 255; i++) {
    mtask *candidate = &m[i];
    if (candidate->state == READY && !candidate->on_cpu &&
        candidate != current) {
      reset_task_slot(candidate, candidate->tid);
      continue;
    }
    if (!scheduler_task_eligible(candidate, cpu_index, current)) {
      continue;
    }
    if (next == NULL || candidate->urgent > next->urgent ||
        (candidate->urgent == next->urgent &&
         candidate->vruntime < next->vruntime)) {
      next = candidate;
    }
  }
  return next != NULL ? next : cpu->idle;
}

static void scheduler_balance(void) {
  uint32_t busiest = 0;
  uint32_t least = 0;
  uint32_t busiest_load = 0;
  uint32_t least_load = UINT_MAX;
  for (uint32_t cpu = 0; cpu < scheduler_cpu_total; cpu++) {
    if (!smp_cpu_online(cpu)) {
      continue;
    }
    uint32_t load = scheduler_load(cpu);
    if (load > busiest_load) {
      busiest = cpu;
      busiest_load = load;
    }
    if (load < least_load) {
      least = cpu;
      least_load = load;
    }
  }
  if (busiest == least || busiest_load <= least_load + 1) {
    return;
  }

  mtask *selected = NULL;
  for (uint32_t i = 0; i < 255; i++) {
    mtask *task = &m[i];
    if (task->state != RUNNING || task->cpu != busiest || task->on_cpu ||
        (task->sched_flags & (TASK_SCHED_IDLE | TASK_SCHED_PINNED))) {
      continue;
    }
    if (selected == NULL || task->vruntime < selected->vruntime) {
      selected = task;
    }
  }
  if (selected != NULL) {
    scheduler_place_task(selected, least);
  }
}

void scheduler_tick(void) {
  if (!scheduler_active) {
    return;
  }
  uint32_t cpu_index = smp_current_cpu();
  scheduler_cpu_t *cpu = &scheduler_cpus[cpu_index];
  mtask *current = cpu->current;
  if (current != NULL) {
    current->runtime_ticks++;
    if (!(current->sched_flags & TASK_SCHED_IDLE)) {
      uint32_t weight = current->weight ? current->weight : 1;
      current->vruntime += 1024u / weight;
    }
  }
  cpu->need_resched = 1;
  if (cpu_index == 0 && global_time % 10 == 0) {
    scheduler_balance();
  }
}

void scheduler_reschedule_interrupt(void) {
  send_eoi(0);
  scheduler_cpus[smp_current_cpu()].need_resched = 1;
  task_next();
}

void scheduler_preempt_if_needed(void) {
  if (scheduler_active && kernel_lock_depth() == 1 &&
      scheduler_cpus[smp_current_cpu()].need_resched) {
    task_next();
  }
}

void task_next(void) {
  if (!scheduler_active) {
    return;
  }
  uint32_t cpu_index = smp_current_cpu();
  scheduler_cpu_t *cpu = &scheduler_cpus[cpu_index];
  if (kernel_lock_depth() != 1) {
    cpu->need_resched = 1;
    return;
  }

  mtask *current = cpu->current;
  if (current == NULL) {
    return;
  }
  current->on_cpu = 0;
  if (current->terminate_pending) {
    task_finish_pending(current);
  }
  if (current->state == WILL_EMPTY) {
    current->state = READY;
  }
  mtask *next = scheduler_pick_next(cpu, cpu_index);
  if (next == NULL) {
    current->on_cpu = 1;
    return;
  }
  next->on_cpu = 1;
  next->urgent = 0;
  next->ready = 0;
  cpu->need_resched = 0;
  if (next->vruntime > cpu->min_vruntime) {
    cpu->min_vruntime = next->vruntime;
  }
  if (next == current) {
    return;
  }

  if (next->user_mode == 1) {
    arch_task_set_kernel_stack(next->top);
  }
  x86_fpu_flush_cpu();

  arch_task_switch(&current->context, next->context, next->pde, &cpu->current,
                   next);
}

static mtask *create_task_impl(uintptr_t entry, unsigned weight,
                               bool share_pde) {
  mtask *t = NULL;
  irq_state_t interrupt_state = irq_save();
  int first = m[0].state == EMPTY ? 0 : 1;
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
  t->tgid = share_pde && scheduler_active ? current_task()->tgid
                                          : (uint32_t)tid;
  t->ptid = share_pde && scheduler_active ? current_task()->ptid
                                          : TASK_ID_NONE;
  t->state = ALLOCATING;
  if (share_pde && scheduler_active) {
    t->cpu = smp_current_cpu();
    t->sched_flags = TASK_SCHED_PINNED;
  }
  irq_restore(interrupt_state);
  void *stack_base = page_malloc(STACK_SIZE);
  if (stack_base == NULL) {
    reset_task_slot(t, tid);
    return NULL;
  }
  uintptr_t esp_alloced = (uintptr_t)stack_base + STACK_SIZE;
  change_page_task_id(t->tid, (void *)(esp_alloced - STACK_SIZE), STACK_SIZE);
  t->context =
      (arch_task_context_t *)(esp_alloced - sizeof(arch_task_context_t));
  t->entry = entry;
  arch_task_context_init(t->context, (uintptr_t)task_bootstrap);
  t->user_mode = 0;                           // 设置是否是user_mode
  bool owns_pde = false;
  if (!scheduler_active) {                    // 还没启用多任务
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
  t->weight = weight;
  extern int init_ok_flag; // init_ok_flag 标记fs等是否初始化完成
  if (init_ok_flag) {
    if (scheduler_active && current_task()->fs_context != NULL) {
      if (share_pde) {
        t->fs_context = current_task()->fs_context;
        vfs_context_retain(t->fs_context);
      } else {
        t->fs_context = vfs_context_clone_cwd(current_task()->fs_context);
      }
    } else {
      t->fs_context = vfs_context_create(default_drive);
    }
    if (t->fs_context == NULL) {
      if (owns_pde) {
        free_pde(t->pde);
      }
      page_free(stack_base, STACK_SIZE);
      reset_task_slot(t, tid);
      return NULL;
    }
  }
  return t;
}
mtask *create_task(uintptr_t entry, unsigned weight) {
  return create_task_impl(entry, weight, false);
}
mtask *create_thread_task(uintptr_t entry, unsigned weight) {
  return create_task_impl(entry, weight, true);
}
bool task_publish(mtask *task) {
  irq_state_t state = irq_save();
  bool published = task != NULL && task->state == ALLOCATING;
  if (published) {
    uint32_t cpu = task->sched_flags & TASK_SCHED_PINNED
                       ? task->cpu
                       : scheduler_least_loaded_cpu();
    scheduler_place_task(task, cpu);
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
  mtask *task = current_task();
  struct user_runtime_layout layout;
  if (!user_runtime_layout_calculate(USER_SPACE_START, 0, 0, false, eip,
                                     &layout) ||
      esp < USER_SPACE_START || esp >= USER_HEAP_END) {
    task_exit(-1);
    return;
  }
  (void)layout;
  x86_interrupt_frame_t iframe;

  x86_user_frame_init(&iframe, eip, esp);
  iframe.gs = 0;
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);
  // task_exit(0);
  // change_page_task_id(current_task()->tid, iframe->esp - 64 * 1024, 64 *
  // 1024);
  kernel_lock_leave();
  x86_return_to_user(&iframe);
}

static bool task_slot_in_use(const mtask *task) {
  return task != NULL && task->state != EMPTY && task->state != WILL_EMPTY &&
         task->state != READY && task->state != ALLOCATING;
}

unsigned task_address_space_owner(unsigned pde) {
  mtask *fallback = NULL;

  for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
    mtask *task = &m[i];
    if (!task_slot_in_use(task) || task->pde != pde) {
      continue;
    }
    if (task->kind == TASK_PROCESS && task->tid == task->tgid) {
      return task->tid;
    }
    if (fallback == NULL) {
      fallback = task;
    }
  }

  return fallback != NULL ? fallback->tgid : TASK_ID_NONE;
}

static void task_clear_ipc_refs(mtask *task) {
  /* 丢掉自己队列里没读完的消息、注销服务名、唤醒等着给它发消息的任务 */
  ipc_task_cleanup(task);
}

static void task_clear_external_refs(mtask *task) {
  extern mtask *keyboard_use_task;
  extern int disable_flag;

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

  for (uint32_t cpu = 0; cpu < scheduler_cpu_total; cpu++) {
    if (scheduler_cpus[cpu].next == task) {
      scheduler_cpus[cpu].next = NULL;
    }
  }
  if (mouse_use_task == task) {
    mouse_sleep(&mdec);
  }
  if (keyboard_use_task == task) {
    keyboard_use_task = NULL;
    disable_flag = 0;
  }
  timer_cancel_for_task(task);
  high_text_cursor_task_exited(task);
  task_clear_ipc_refs(task);
  net_socket_cancel_waits(task->tid, task->generation);
  if (task->kind == TASK_PROCESS && task->tid == task->tgid) {
    net_socket_task_cleanup(task->tgid);
  }
  sb16_remove_task(task);
  vdisk_remove_task(task->tid);
}

static void task_release_resources(mtask *task) {
  unsigned tid = task->tid;

  x86_fpu_reset(task);
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
  if (task->fs_context) {
    vfs_context_release(task->fs_context);
    task->fs_context = NULL;
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
  task->fifosleep = 0;
  task->mx = 0;
  task->my = 0;
  task->line = NULL;
  task->timer = NULL;
  task->waittid = TASK_ID_NONE;
  task->wait_generation = 0;
  task->wait_reason = WAIT_REASON_NONE;
  task->ready = 0;
  task->pde = 0;
  task->sigint_up = 0;
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

static void request_task_termination(mtask *task, unsigned status) {
  if (task->on_cpu && task != current_task()) {
    task->terminate_status = status;
    task->terminate_pending = 1;
    scheduler_cpus[task->cpu].need_resched = 1;
    smp_send_reschedule(task->cpu);
    return;
  }
  finish_task(task, status,
              task->kind == TASK_PROCESS && task->ptid != TASK_ID_NONE &&
                  task->ptid != REAPER_TID);
}

static void terminate_thread_group(uint32_t tgid, mtask *except) {
  for (int i = 0; i < 255; i++) {
    mtask *thread = &m[i];
    if (thread == except || !task_slot_in_use(thread) ||
        thread->kind != TASK_THREAD || thread->tgid != tgid) {
      continue;
    }
    request_task_termination(thread, TASK_KILLED_STATUS);
  }
}

static void task_finish_pending(mtask *task) {
  unsigned status = task->terminate_status;
  task->terminate_pending = 0;
  if (task->kind == TASK_PROCESS) {
    terminate_thread_group(task->tgid, task);
    reparent_children(task->tgid, REAPER_TID);
  }
  bool waitable = task->kind == TASK_PROCESS && task->ptid != TASK_ID_NONE &&
                  task->ptid != REAPER_TID;
  finish_task(task, status, waitable);
}

void task_kill(unsigned tid) {
  mtask *task = get_task(tid);
  if (!task || task->state == DIED) {
    return;
  }
  irq_state_t interrupt_state = irq_save();
  bool is_current = task == current_task();
  if (task->on_cpu && !is_current) {
    task->terminate_status = TASK_KILLED_STATUS;
    task->terminate_pending = 1;
    scheduler_cpus[task->cpu].need_resched = 1;
    smp_send_reschedule(task->cpu);
    irq_restore(interrupt_state);
    return;
  }
  if (task->kind == TASK_PROCESS) {
    terminate_thread_group(task->tgid, task);
    reparent_children(task->tgid, REAPER_TID);
  }
  bool waitable = task->kind == TASK_PROCESS && task->ptid != TASK_ID_NONE &&
                  task->ptid != REAPER_TID;
  finish_task(task, TASK_KILLED_STATUS, waitable);
  if (is_current) {
    task_next();
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  irq_restore(interrupt_state);
}

mtask *current_task() {
  mtask *current = scheduler_cpus[smp_current_cpu()].current;
  if (current == NULL) {
    null_task.tid = NULL_TID;
    return &null_task;
  }
  return current;
}
int into_mtask() {
  init_task();
  arch_task_state_init();
  scheduler_cpu_total = smp_cpu_count();
  memset(scheduler_cpus, 0, sizeof(scheduler_cpus));
  for (uint32_t cpu = 0; cpu < scheduler_cpu_total; cpu++) {
    if (!smp_cpu_online(cpu)) {
      continue;
    }
    mtask *idle_task = create_task((uintptr_t)idle, 1);
    if (idle_task == NULL) {
      Panic_K("unable to create CPU idle task");
      return -1;
    }
    idle_task->cpu = cpu;
    idle_task->sched_flags = TASK_SCHED_IDLE | TASK_SCHED_PINNED;
    sprintf(idle_task->name, "idle/%d", cpu);
    scheduler_cpus[cpu].idle = idle_task;
    if (!task_publish(idle_task)) {
      Panic_K("unable to publish CPU idle task");
      return -1;
    }
  }
  mtask *init_task = create_task((uintptr_t)init, 5);
  if (init_task == NULL) {
    Panic_K("unable to create bootstrap tasks");
    return -1;
  }
  init_task->cpu = 0;
  init_task->sched_flags = TASK_SCHED_PINNED;
  task_set_name(init_task, "kinit");
  if (!task_publish(init_task)) {
    Panic_K("unable to publish bootstrap tasks");
    return -1;
  }
  scheduler_active = 1;
  mtask *idle = scheduler_cpus[0].idle;
  idle->on_cpu = 1;
  arch_task_start(idle->context, idle->pde, &scheduler_cpus[0].current, idle);
}

__attribute__((noreturn)) void scheduler_start_secondary(uint32_t cpu) {
  mtask *idle = scheduler_cpus[cpu].idle;
  if (!scheduler_active || idle == NULL) {
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  idle->on_cpu = 1;
  arch_task_start(idle->context, idle->pde, &scheduler_cpus[cpu].current,
                  idle);
}

static void task_bootstrap(void) {
  if (kernel_lock_depth() == 0) {
    kernel_lock_enter();
  }
  if (smp_current_cpu() == 0) {
    smp_release_secondary_cpus();
  }
  mtask *task = current_task();
  void (*entry)(void) = (void (*)(void))task->entry;
  entry();
  task_exit(0);
}

void task_set_name(mtask *task, const char *name) {
  if (task == NULL || name == NULL) {
    return;
  }
  size_t length = strlen(name);
  if (length >= sizeof(task->name)) {
    length = sizeof(task->name) - 1;
  }
  memcpy(task->name, name, length);
  task->name[length] = '\0';
}

bool task_pin_current(uint32_t cpu) {
  if (cpu >= scheduler_cpu_total || !smp_cpu_online(cpu)) {
    return false;
  }
  mtask *task = current_task();
  task->sched_flags |= TASK_SCHED_PINNED;
  task->cpu = cpu;
  if (cpu != smp_current_cpu()) {
    scheduler_cpus[cpu].need_resched = 1;
    smp_send_reschedule(cpu);
    task_next();
  }
  return smp_current_cpu() == cpu;
}

unsigned task_wake_tty(struct tty *tty) {
  unsigned woken = 0;
  for (unsigned tid = 0; tid < 255; tid++) {
    mtask *task = get_task(tid);
    if (task == NULL || task->state != WAITING ||
        task->wait_reason != WAIT_REASON_KEYBOARD || task->terminate_pending ||
        task->TTY != tty) {
      continue;
    }
    task_run(task);
    woken++;
  }
  return woken;
}

void task_close_tty(struct tty *tty, struct tty *fallback) {
  if (tty == NULL) {
    return;
  }

  mtask *self = current_task();
  bool kill_self = false;
  for (enum TASK_KIND kind = TASK_PROCESS; kind <= TASK_THREAD; kind++) {
    for (unsigned tid = 0; tid < 255; tid++) {
      mtask *task = &m[tid];
      if (!task_slot_in_use(task) || task->kind != kind ||
          task->tty_session != tty) {
        continue;
      }
      task->tty_session = fallback;
      if (task->TTY == tty) {
        task->TTY = fallback;
      }
      if (task->state == DIED) {
        reset_task_slot(task, tid);
        continue;
      }
      if (task == self) {
        kill_self = true;
        continue;
      }
      if (task->kind == TASK_PROCESS) {
        task->ptid = REAPER_TID;
      }
      task_kill(tid);
    }
  }
  if (kill_self) {
    if (self->kind == TASK_PROCESS) {
      self->ptid = REAPER_TID;
    }
    task_kill(self->tid);
  }
}

int task_snapshot(task_info_t *entries, uint32_t capacity, uint32_t *count) {
  uint32_t required = 0;
  for (uint32_t i = 0; i < 255; i++) {
    enum STATE state = m[i].state;
    if (state != EMPTY && state != ALLOCATING && state != WILL_EMPTY &&
        state != READY) {
      required++;
    }
  }
  *count = required;
  if (entries == NULL) {
    return 0;
  }
  if (capacity < required) {
    return TASK_SNAPSHOT_CAPACITY;
  }

  uint32_t output = 0;
  for (uint32_t i = 0; i < 255; i++) {
    mtask *task = &m[i];
    if (task->state == EMPTY || task->state == ALLOCATING ||
        task->state == WILL_EMPTY || task->state == READY) {
      continue;
    }
    task_info_t *info = &entries[output++];
    info->tid = task->tid;
    info->tgid = task->tgid;
    info->ptid = task->ptid;
    info->generation = task->generation;
    info->cpu = task->cpu;
    info->kind = task->kind;
    info->flags = task->on_cpu ? TASK_INFO_FLAG_ON_CPU : 0;
    info->runtime_ms = task->runtime_ticks * 10ull;
    switch (task->state) {
    case RUNNING:
      info->state = TASK_INFO_RUNNING;
      break;
    case WAITING:
      info->state = TASK_INFO_WAITING;
      break;
    case SLEEPING:
      info->state = TASK_INFO_SLEEPING;
      break;
    default:
      info->state = TASK_INFO_ZOMBIE;
      break;
    }
    memcpy(info->name, task->name, sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';
  }
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
  task_run(task);
}
void task_run(mtask *task) {
  if (!task || task->state == EMPTY || task->state == WILL_EMPTY ||
      task->state == READY || task->state == ALLOCATING || task->state == DIED) {
    return;
  }
  task->urgent = 1;
  if (task->state == WAITING || task->state == SLEEPING) {
    task->state = RUNNING;
    task->wait_reason = WAIT_REASON_NONE;
    task->ready = 0;
    if (!(task->sched_flags & TASK_SCHED_PINNED) && !task->on_cpu) {
      scheduler_place_task(task, scheduler_least_loaded_cpu());
    } else {
      scheduler_cpus[task->cpu].need_resched = 1;
      smp_send_reschedule(task->cpu);
    }
  } else {
    task->ready = 1;
    scheduler_cpus[task->cpu].need_resched = 1;
    smp_send_reschedule(task->cpu);
  }
  task->fifosleep = 0;
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
    task_next();
    irq_restore(interrupt_state);
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
  irq_state_t interrupt_state = irq_save();
  if (current_task()->ready == 1) {
    current_task()->ready = 0;
    current_task()->wait_reason = WAIT_REASON_NONE;
    irq_restore(interrupt_state);
    return;
  }
  current_task()->state = state;
  current_task()->wait_reason = reason;
  current_task()->ready = 0;
  task_next();
  irq_restore(interrupt_state);
}
void task_fall_blocked(enum STATE state) {
  task_fall_blocked_reason(state, WAIT_REASON_GENERIC);
}
void task_exit(unsigned status) {
  mtask *task = current_task();
  (void)irq_save();
  task->terminate_status = status;
  task_finish_pending(task);
  task_next();
  for (;;) {
    asm volatile("cli; hlt");
  }
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
    task_next();
    irq_restore(interrupt_state);
  }
}
void mtask_run_now(mtask *obj) {
  if (obj == NULL || obj->cpu >= scheduler_cpu_total) {
    return;
  }
  scheduler_cpus[obj->cpu].next = obj;
  scheduler_cpus[obj->cpu].need_resched = 1;
  smp_send_reschedule(obj->cpu);
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
      t = &(m[i]);
      break;
    }
  }
  return t;
}
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
  addr -= sizeof(arch_task_context_t);
  task->context = (arch_task_context_t *)addr;
  arch_task_context_init(task->context,
                         (uintptr_t)arch_task_interrupt_return);
}
int task_fork() {
  mtask *parent = current_task();
  irq_state_t state = irq_save();
  mtask *m = mtask_get_free();
  if (!m) {
    irq_restore(state);
    return -1;
  }
  int tid = m->tid;
  uint32_t generation = m->generation + 1;
  x86_fpu_flush_cpu();
  memcpy(m, parent, sizeof(mtask));
  m->tid = tid;
  m->generation = generation;
  m->kind = TASK_PROCESS;
  m->tgid = tid;
  m->ptid = parent->tgid;
  m->state = ALLOCATING;
  m->on_cpu = 0;
  m->sched_flags = 0;
  m->runtime_ticks = 0;
  m->terminate_pending = 0;
  m->terminate_status = 0;
  uintptr_t stack = (uintptr_t)page_malloc(STACK_SIZE);
  if (stack == 0) {
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  change_page_task_id(tid, (void *)stack, STACK_SIZE);
  uintptr_t old_stack_base = m->top - STACK_SIZE;
  uintptr_t old_context = (uintptr_t)m->context;
  uintptr_t context_offset = old_context - old_stack_base;
  memcpy((void *)stack, (void *)old_stack_base, STACK_SIZE);
  m->top = stack + STACK_SIZE;
  m->context = (arch_task_context_t *)(stack + context_offset);
  m->fs_context = NULL;
  m->Pkeyfifo = NULL;
  m->Ukeyfifo = NULL;
  m->keyfifo = NULL;
  m->mousefifo = NULL;
  m->timer = NULL;
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
  m->fs_context = vfs_context_fork(parent->fs_context);
  if (m->fs_context == NULL) {
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
    vfs_context_release(m->fs_context);
    m->fs_context = NULL;
    release_task_fifos(m);
    if (m->alloced) {
      free(m->alloc_size);
    }
    page_free((void *)stack, STACK_SIZE);
    reset_task_slot(m, tid);
    irq_restore(state);
    return -1;
  }
  m->weight = 1;
  m->ptid = parent->tgid;
  m->tgid = tid;
  m->kind = TASK_PROCESS;
  m->tid = tid;
  build_fork_stack(m);
  if (!task_publish(m)) {
    task_abort_creation(m);
    irq_restore(state);
    return -1;
  }
  irq_restore(state);
  return tid;
}
