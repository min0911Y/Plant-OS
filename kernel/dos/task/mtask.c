// 多任务重构 -- mtask.c (区别与以前的多任务)
#include <arch.h>
#include <dos.h>
#include <input_device.h>
#include <irq.h>
#include <limits.h>
#include <platform.h>
#include <smp.h>
#include <tty_rpc.h>
#include <usb.h>
#define STACK_SIZE TASK_KERNEL_STACK_SIZE
#define REAPER_TID 0u
#define TASK_ID_NONE ((uint32_t)-1)
#define TASK_KILLED_STATUS ((unsigned)-1)
#define TASK_SLOT_CHUNK_SIZE 64u
void gc(unsigned tid);
static void task_slot_reset(mtask *task, uint32_t tid);
static void task_slot_release(mtask *task);
static void task_bootstrap(void);
static void task_finish_pending(mtask *task);
static void release_task_fifos(mtask *task);
char default_drive = 'A';
typedef struct {
  mtask **chunks;
  uint32_t chunk_count;
  uint32_t chunk_capacity;
  uint32_t slot_count;
  uint32_t free_hint;
} task_registry_t;

typedef struct {
  mtask *current;
  mtask *idle;
  mtask *next;
  uint64_t min_vruntime;
  uint32_t need_resched;
} scheduler_cpu_t;

static task_registry_t task_registry;
static scheduler_cpu_t scheduler_cpus[SMP_MAX_CPUS];
static uint32_t scheduler_cpu_total = 1;
static uint32_t scheduler_active;
static uint64_t scheduler_ticks;

static bool task_slot_in_use(const mtask *task) {
  return task != NULL && task->state != EMPTY && task->state != WILL_EMPTY &&
         task->state != READY && task->state != ALLOCATING;
}

static mtask *task_slot_at(uint32_t tid) {
  if (tid >= task_registry.slot_count) {
    return NULL;
  }
  uint32_t chunk = tid / TASK_SLOT_CHUNK_SIZE;
  uint32_t offset = tid % TASK_SLOT_CHUNK_SIZE;
  return &task_registry.chunks[chunk][offset];
}

static bool task_registry_grow(void) {
  if (task_registry.slot_count > UINT_MAX - TASK_SLOT_CHUNK_SIZE) {
    return false;
  }

  /* Task contexts may require stricter alignment than the general heap. */
  size_t chunk_bytes = sizeof(mtask) * TASK_SLOT_CHUNK_SIZE;
  mtask *chunk = page_malloc(chunk_bytes);
  if (chunk == NULL) {
    return false;
  }
  uint32_t first_tid = task_registry.slot_count;
  for (uint32_t i = 0; i < TASK_SLOT_CHUNK_SIZE; i++) {
    task_slot_reset(&chunk[i], first_tid + i);
  }

  irq_state_t state = irq_save();
  if (task_registry.chunk_count == task_registry.chunk_capacity) {
    uint32_t capacity = task_registry.chunk_capacity == 0
                            ? 1
                            : task_registry.chunk_capacity * 2;
    if (capacity < task_registry.chunk_capacity ||
        capacity > UINT_MAX / sizeof(*task_registry.chunks)) {
      irq_restore(state);
      page_free(chunk, chunk_bytes);
      return false;
    }
    mtask **chunks = realloc(task_registry.chunks,
                             capacity * sizeof(*task_registry.chunks));
    if (chunks == NULL) {
      irq_restore(state);
      page_free(chunk, chunk_bytes);
      return false;
    }
    task_registry.chunks = chunks;
    task_registry.chunk_capacity = capacity;
  }

  task_registry.chunks[task_registry.chunk_count++] = chunk;
  task_registry.slot_count += TASK_SLOT_CHUNK_SIZE;
  if (first_tid < task_registry.free_hint) {
    task_registry.free_hint = first_tid;
  }
  irq_restore(state);
  return true;
}

static mtask *task_slot_claim(bool allow_zero) {
  for (;;) {
    irq_state_t state = irq_save();
    uint32_t first = task_registry.free_hint;
    if (!allow_zero && first == 0) {
      first = 1;
    }
    for (uint32_t tid = first; tid < task_registry.slot_count; tid++) {
      mtask *task = task_slot_at(tid);
      if (task->state != EMPTY) {
        continue;
      }
      uint32_t generation = task->generation + 1;
      if (generation == 0) {
        generation = 1;
      }
      task_slot_reset(task, tid);
      task->generation = generation;
      task->state = ALLOCATING;
      task_registry.free_hint = tid + 1;
      irq_restore(state);
      return task;
    }
    irq_restore(state);
    if (!task_registry_grow()) {
      return NULL;
    }
  }
}

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
static bool init_task(void) {
  memset(&task_registry, 0, sizeof(task_registry));
  return task_registry_grow();
}

static uint32_t scheduler_load(uint32_t cpu) {
  uint32_t load = 0;
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *task = task_slot_at(i);
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

static bool task_pin_address_space(arch_address_space_t address_space,
                                   uint32_t cpu) {
  mtask *current = current_task();
  for (uint32_t tid = 0; tid < task_registry.slot_count; tid++) {
    mtask *task = task_slot_at(tid);
    if (!task_slot_in_use(task) || task->address_space != address_space) {
      continue;
    }
    if (task->on_cpu && task != current) {
      return false;
    }
  }

  for (uint32_t tid = 0; tid < task_registry.slot_count; tid++) {
    mtask *task = task_slot_at(tid);
    if (!task_slot_in_use(task) || task->address_space != address_space) {
      continue;
    }
    task->sched_flags |= TASK_SCHED_PINNED;
    if (task->on_cpu) {
      task->cpu = cpu;
    } else if (task->cpu != cpu) {
      scheduler_place_task(task, cpu);
    }
  }
  return true;
}

static int scheduler_task_eligible(const mtask *task, uint32_t cpu,
                                   const mtask *current) {
  return task != NULL && task->state == RUNNING && task->cpu == cpu &&
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

  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *candidate = task_slot_at(i);
    if (candidate->state == READY && !candidate->on_cpu &&
        candidate != current) {
      task_slot_release(candidate);
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
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *task = task_slot_at(i);
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
  if (cpu_index == 0 && ++scheduler_ticks % 10 == 0) {
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
  arch_fpu_flush_cpu();

  arch_task_switch(&current->context, next->context, next->address_space, &cpu->current,
                   next);
}

static mtask *create_task_impl(uintptr_t entry, unsigned weight,
                               bool share_pde) {
  if (share_pde && scheduler_active) {
    irq_state_t state = irq_save();
    bool pinned =
        task_pin_address_space(current_task()->address_space, smp_current_cpu());
    irq_restore(state);
    if (!pinned) {
      return NULL;
    }
  }
  mtask *t = task_slot_claim(!scheduler_active);
  if (t == NULL) {
    return NULL;
  }
  uint32_t tid = t->tid;
  t->kind = share_pde ? TASK_THREAD : TASK_PROCESS;
  t->tgid = share_pde && scheduler_active ? current_task()->tgid
                                          : (uint32_t)tid;
  t->ptid = share_pde && scheduler_active ? current_task()->ptid
                                          : TASK_ID_NONE;
  if (share_pde && scheduler_active) {
    t->cpu = smp_current_cpu();
    t->sched_flags = TASK_SCHED_PINNED;
  }
  void *stack_base = page_malloc(STACK_SIZE);
  if (stack_base == NULL) {
    task_slot_release(t);
    return NULL;
  }
  uintptr_t esp_alloced = (uintptr_t)stack_base + STACK_SIZE;
  t->top = esp_alloced;
  t->context =
      (arch_task_context_t *)(esp_alloced - sizeof(arch_task_context_t));
  t->entry = entry;
  arch_task_context_init(t->context, (uintptr_t)task_bootstrap);
  t->user_mode = 0;                           // 设置是否是user_mode
  bool owns_pde = false;
  if (!scheduler_active) {                    // 还没启用多任务
    t->address_space = arch_address_space_kernel();
  } else if (share_pde) {
    t->address_space = current_task()->address_space;
    arch_address_space_retain(t->address_space);
    owns_pde = true;
  } else {
    t->address_space = arch_address_space_clone(arch_address_space_kernel());
    if (t->address_space == 0) {
      task_slot_release(t);
      return NULL;
    }
    owns_pde = true;
  }
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
        arch_address_space_release(t->address_space);
      }
      task_slot_release(t);
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
  mtask *task = task_slot_at(tid);
  if (task == NULL) {
    return NULL;
  }
  if (task->state == EMPTY || task->state == WILL_EMPTY ||
      task->state == READY || task->state == ALLOCATING) {
    return NULL;
  }
  return task;
}
unsigned task_address_space_owner(arch_address_space_t address_space) {
  mtask *fallback = NULL;

  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *task = task_slot_at(i);
    if (!task_slot_in_use(task) || task->address_space != address_space) {
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

mtask *task_iter_next(task_iterator_t *iterator) {
  if (iterator == NULL) {
    return NULL;
  }
  while (iterator->next_tid < task_registry.slot_count) {
    mtask *task = task_slot_at(iterator->next_tid++);
    if (task_slot_in_use(task)) {
      return task;
    }
  }
  return NULL;
}

static void task_clear_ipc_refs(mtask *task) {
  /* 丢掉自己队列里没读完的消息、注销服务名、唤醒等着给它发消息的任务 */
  ipc_task_cleanup(task);
}

static void task_clear_external_refs(mtask *task) {
#if defined(KERNEL_ARCH_X86_64)
  platform_video_release_owner(task);
#endif

  mtask *leader = get_task(task->tgid);
  if (leader && leader->group_lock_owner == task->tid) {
    leader->group_lock_owner = TASK_ID_NONE;
    leader->group_lock_depth = 0;
    for (uint32_t i = 0; i < task_registry.slot_count; i++) {
      mtask *waiter = task_slot_at(i);
      if (task_slot_in_use(waiter) && waiter->tgid == task->tgid &&
          waiter->state == WAITING &&
          waiter->wait_reason == WAIT_REASON_TASK_GROUP_LOCK) {
        task_run(waiter);
      }
    }
  }

  for (uint32_t cpu = 0; cpu < scheduler_cpu_total; cpu++) {
    if (scheduler_cpus[cpu].next == task) {
      scheduler_cpus[cpu].next = NULL;
    }
  }
  if (mouse_use_task == task) {
    mouse_sleep();
  }
  if (keyboard_use_task == task) {
    keyboard_use_task = NULL;
  }
  timer_cancel_for_task(task);
#if defined(KERNEL_ARCH_I386)
  high_text_cursor_task_exited(task);
#endif
  task_clear_ipc_refs(task);
  fartty_task_cleanup(task);
  net_socket_cancel_waits(task->tid, task->generation);
  if (task->kind == TASK_PROCESS && task->tid == task->tgid) {
    net_socket_task_cleanup(task->tgid);
  }
  vdisk_remove_task(task->tid);
}

static void task_release_resources(mtask *task) {
  usb_cancel_task(task->tid, task->generation);
  unsigned tid = task->tid;

  arch_fpu_reset(task);
  task_clear_external_refs(task);
  if (task == current_task()) {
    arch_address_space_activate(arch_address_space_kernel());
  }
  if (task->address_space &&
      task->address_space != arch_address_space_kernel()) {
    arch_address_space_release(task->address_space);
  }
  release_task_fifos(task);
  gc(tid);
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
  task->address_space = 0;
  task->sigint_up = 0;
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
  task_release_resources(task);
  task_slot_release(task);
}

static void wake_child_waiter(mtask *child) {
  if (child->ptid == TASK_ID_NONE || child->ptid == REAPER_TID) {
    return;
  }
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *waiter = task_slot_at(i);
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
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *child = task_slot_at(i);
    if (!task_slot_in_use(child) || child->kind != TASK_PROCESS ||
        child->ptid != old_parent) {
      continue;
    }
    child->ptid = new_parent;
    for (uint32_t j = 0; j < task_registry.slot_count; j++) {
      mtask *thread = task_slot_at(j);
      if (task_slot_in_use(thread) && thread->kind == TASK_THREAD &&
          thread->tgid == child->tgid) {
        thread->ptid = new_parent;
      }
    }
    if (child->state == DIED && new_parent == REAPER_TID) {
      task_slot_release(child);
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
    task_slot_release(task);
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
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *thread = task_slot_at(i);
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
    arch_halt();
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
  if (!init_task()) {
    Panic_K("unable to initialize task registry");
    return -1;
  }
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

  (void)irq_save();
  scheduler_active = 1;
  init_task->on_cpu = 1;
  arch_task_start(init_task->context, init_task->address_space,
                  &scheduler_cpus[0].current, init_task);
}

__attribute__((noreturn)) void scheduler_start_secondary(uint32_t cpu) {
  mtask *idle = scheduler_cpus[cpu].idle;
  if (!scheduler_active || idle == NULL) {
    arch_halt();
  }
  idle->on_cpu = 1;
  arch_task_start(idle->context, idle->address_space, &scheduler_cpus[cpu].current,
                  idle);
}

static void task_bootstrap(void) {
  mtask *task = current_task();
  if (kernel_lock_depth() == 0) {
    kernel_lock_enter();
  }
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
  irq_state_t state = irq_save();
  bool pinned = task_pin_address_space(task->address_space, cpu);
  if (!pinned) {
    irq_restore(state);
    return false;
  }
  if (cpu != smp_current_cpu()) {
    scheduler_cpus[cpu].need_resched = 1;
    smp_send_reschedule(cpu);
    task_next();
  }
  bool on_target = smp_current_cpu() == cpu;
  irq_restore(state);
  return on_target;
}

unsigned task_wake_tty(struct tty *tty) {
  unsigned woken = 0;
  for (uint32_t tid = 0; tid < task_registry.slot_count; tid++) {
    mtask *task = task_slot_at(tid);
    if (!task_slot_in_use(task) || task->state != WAITING ||
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
    for (uint32_t tid = 0; tid < task_registry.slot_count; tid++) {
      mtask *task = task_slot_at(tid);
      if (!task_slot_in_use(task) || task->kind != kind ||
          task->tty_session != tty) {
        continue;
      }
      task->tty_session = fallback;
      if (task->TTY == tty) {
        task->TTY = fallback;
      }
      if (task->state == DIED) {
        task_slot_release(task);
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
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    enum STATE state = task_slot_at(i)->state;
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
  for (uint32_t i = 0; i < task_registry.slot_count; i++) {
    mtask *task = task_slot_at(i);
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
    for (uint32_t i = 0; i < task_registry.slot_count; i++) {
      mtask *waiter = task_slot_at(i);
      if (task_slot_in_use(waiter) && waiter->tgid == self->tgid &&
          waiter->state == WAITING &&
          waiter->wait_reason == WAIT_REASON_TASK_GROUP_LOCK) {
        task_run(waiter);
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
  arch_halt();
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
      task_slot_release(child);
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
    *dest = (struct FIFO8 *)page_malloc_one_no_mark();
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
static void task_slot_reset(mtask *task, uint32_t tid) {
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

static void task_slot_release(mtask *task) {
  irq_state_t state = irq_save();
  uint32_t tid = task->tid;
  // The scheduler or a waiter retires the slot after leaving its kernel stack.
  if (task->top)
    page_free((void *)(task->top - STACK_SIZE), STACK_SIZE);
  task_slot_reset(task, tid);
  if (tid < task_registry.free_hint) {
    task_registry.free_hint = tid;
  }
  irq_restore(state);
}

void roc() {
  logk("ROCT\n");
  for (;;)
    ;
}
int task_fork() {
  mtask *parent = current_task();
  mtask *child = task_slot_claim(false);
  if (child == NULL) {
    return -1;
  }
  uint32_t tid = child->tid;
  uint32_t generation = child->generation;
  irq_state_t state = irq_save();
  arch_fpu_flush_cpu();
  memcpy(child, parent, sizeof(mtask));
  child->tid = tid;
  child->generation = generation;
  child->kind = TASK_PROCESS;
  child->return_cwd = false;
  child->tgid = tid;
  child->ptid = parent->tgid;
  child->state = ALLOCATING;
  child->on_cpu = 0;
  child->sched_flags = 0;
  child->runtime_ticks = 0;
  child->terminate_pending = 0;
  child->terminate_status = 0;
  child->top = 0;
  child->address_space = 0;
  child->fs_context = NULL;
  child->Pkeyfifo = NULL;
  child->Ukeyfifo = NULL;
  child->keyfifo = NULL;
  child->mousefifo = NULL;
  child->timer = NULL;
  child->alloced = 0;
  child->alloc_size = NULL;
  /* 消息队列不继承：父进程队列里的负载归父进程所有 */
  ipc_task_init(child);
  child->waittid = TASK_ID_NONE;
  child->wait_generation = 0;
  child->wait_reason = WAIT_REASON_NONE;
  child->group_lock_owner = TASK_ID_NONE;
  child->group_lock_depth = 0;
  child->ready = 0;
  child->urgent = 0;
  child->line = NULL;
  child->signal = 0;
  uintptr_t stack = (uintptr_t)page_malloc(STACK_SIZE);
  if (stack == 0)
    goto failed;
  uintptr_t old_stack_base = parent->top - STACK_SIZE;
  uintptr_t context_offset = (uintptr_t)parent->context - old_stack_base;
  memcpy((void *)stack, (void *)old_stack_base, STACK_SIZE);
  child->top = stack + STACK_SIZE;
  child->context = (arch_task_context_t *)(stack + context_offset);
  if (parent->alloc_size) {
    child->alloc_size = malloc(sizeof(*child->alloc_size));
    if (!child->alloc_size)
      goto failed;
    *child->alloc_size = *parent->alloc_size;
    child->alloced = 1;
  }
  if (!clone_task_fifo(&child->Pkeyfifo, parent->Pkeyfifo, true) ||
      !clone_task_fifo(&child->Ukeyfifo, parent->Ukeyfifo, true) ||
      !clone_task_fifo(&child->keyfifo, parent->keyfifo, false) ||
      !clone_task_fifo(&child->mousefifo, parent->mousefifo, false))
    goto failed;
  child->fs_context = vfs_context_fork(parent->fs_context);
  if (!child->fs_context)
    goto failed;
  child->address_space = arch_address_space_clone(parent->address_space);
  if (!child->address_space)
    goto failed;
  child->weight = 1;
  child->ptid = parent->tgid;
  child->tgid = tid;
  child->kind = TASK_PROCESS;
  child->tid = tid;
  arch_task_fork_context_init(child);
  if (!task_publish(child))
    goto failed;
  irq_restore(state);
  return (int)tid;
failed:
  task_abort_creation(child);
  irq_restore(state);
  return -1;
}
