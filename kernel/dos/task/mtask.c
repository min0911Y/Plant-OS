// 多任务重构 -- mtask.c (区别与以前的多任务)
#include <dos.h>
void free_pde(unsigned addr);
char default_drive, default_drive_number;
static char flags_once = false;
mtask m[255];
struct TSS32 tss;
mtask *idle_task;
mtask *current = NULL;
char mtask_stop_flag = 0;
unsigned get_cr3() { asm volatile("movl %cr3, %eax\n"); }
void set_cr3(uint32_t pde) { asm volatile("movl %%eax, %%cr3\n" ::"a"(pde)); }
mtask *next_set = NULL;
static void init_task() {
  for (int i = 0; i < 255; i++) {
    m[i].jiffies = 0;   // 最后一次执行的全局时间片
    m[i].user_mode = 0; // 此项暂时废除
    m[i].running = 0;
    m[i].timeout = 0;
    m[i].state = EMPTY; // EMPTY
    m[i].tid = i;       // task id
    m[i].ptid = -1;     // parent task id
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
    m[i].alloc_addr = 0;
    m[i].alloc_size = 0;
    m[i].ready = 0;
    m[i].pde = 0;
    m[i].Pkeyfifo = NULL;
    m[i].Ukeyfifo = NULL;
    m[i].sigint_up = 0;
    lock_init(&(m[i].ipc_header.l));
    m[i].ipc_header.now = 0;
    for (int k = 0; k < MAX_IPC_MESSAGE; k++) {
      m[i].ipc_header.messages[k].from_tid = -1;
      m[i].ipc_header.messages[k].flag1 = 0;
      m[i].ipc_header.messages[k].flag2 = 0;
    }
    for (int k = 0; k < 30; k++) {
      m[i].handler[k] = 0;
    }
  }
}
fpu_t public_fpu;
void task_next() {
  // io_sti();
  if (mtask_stop_flag && next_set == NULL)
    return;
  if (current->running < current->timeout - 1 && current->state == RUNNING &&
      next_set == NULL) {
    current->running++;
    return; // 不需要调度，当前时间片仍然属于你
  }
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
          goto OK;
        }
        if (p->waittid == -1)
          continue;
        if ((m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
             m[i].state == READY) ||
            m[p->waittid].ptid != p->tid) {
          p->state = RUNNING;
          p->waittid = -1;
          i--;
        }
      }
      continue;
    }
  OK:
    if (!next || p->jiffies < next->jiffies || p->urgent)
      next = p;
  }
H:
  if (next->user_mode == 1) {
    tss.esp0 = next->top;
  }
  if (next == NULL) {
    next = idle_task;
  }
  if (next->urgent) {
    next->urgent = 0;
  }
  int current_fpu_flag = current->fpu_flag;
  fpu_t *current_fpu = &(current->fpu);
  set_cr0(get_cr0() & ~(CR0_EM | CR0_TS));
  if (current_fpu && current_fpu_flag)
    asm volatile("fnsave (%%eax) \n" ::"a"(current_fpu));
  next->jiffies = global_time;
  fpu_disable(); // 禁用fpu 如果使用FPU就会调用ERROR7
  if (current_task()->state == WILL_EMPTY) {
    current_task()->state = READY;
  }
  task_switch(next); // 调度
}

mtask *create_task(unsigned eip, unsigned esp, unsigned ticks, unsigned floor) {
  mtask *t = NULL;
  for (int i = 0; i < 255; i++) {
    if (m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
        m[i].state == READY) {
      t = &(m[i]);
      break;
    }
  }
  if (!t) {
    return NULL;
  }
  t->esp = esp - sizeof(stack_frame); // switch用到的栈帧
  t->esp->eip = eip;                  // 设置跳转地址
  t->user_mode = 0;                   // 设置是否是user_mode
  if (current == NULL) {              // 还没启用多任务
    t->pde = PDE_ADDRESS;             // 所以先用预设好的页表
  } else {
    t->pde = pde_clone(current_task()->pde); // 启用了就复制一个
  }
  t->top = esp; // r0的esp
  t->floor = floor;
  t->running = 0;
  t->timeout = ticks;
  t->state = RUNNING; // running
  t->drive_number = default_drive_number;
  t->drive = default_drive;
  t->jiffies = 0;
  // 获取default_drive_number
  if (!flags_once) {
    if (memcmp((void *)"FAT12   ", (void *)0x7c00 + BS_FileSysType, 8) == 0 ||
        memcmp((void *)"FAT16   ", (void *)0x7c00 + BS_FileSysType, 8) == 0) {
      if (*(unsigned char *)(0x7c00 + BS_DrvNum) >= 0x80) {
        default_drive_number =
            *(unsigned char *)(0x7c00 + BS_DrvNum) - 0x80 + 0x02;
      } else {
        default_drive_number = *(unsigned char *)(0x7c00 + BS_DrvNum);
      }
    } else if (memcmp((void *)"FAT32   ",
                      (void *)0x7c00 + BPB_Fat32ExtByts + BS_FileSysType,
                      8) == 0) {
      if (*(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum) >= 0x80) {
        default_drive_number =
            *(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum) - 0x80 +
            0x02;
      } else {
        default_drive_number =
            *(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum);
      }
    } else {
      if (*(unsigned char *)(0x7c00) >= 0x80) {
        default_drive_number = *(unsigned char *)(0x7c00) - 0x80 + 0x02;
      } else {
        default_drive_number = *(unsigned char *)(0x7c00);
      }
    }
    default_drive = default_drive_number + 0x41;
    flags_once = true;
  }
  extern int init_ok_flag; // init_ok_flag 标记fs等是否初始化完成
  if (init_ok_flag) {
    vfs_change_disk_for_task(t->drive, t);
  }
  return t;
}
mtask *get_task(unsigned tid) {
  if (tid >= 255) {
    return NULL;
  }
  if (m[tid].state == EMPTY || m[tid].state == WILL_EMPTY ||
      m[tid].state == READY) {
    return NULL;
  }
  return &(m[tid]);
}
void task_to_user_mode(unsigned eip, unsigned esp) {

  unsigned addr = (unsigned)current->top;

  addr -= sizeof(intr_frame_t);
  intr_frame_t *iframe = (intr_frame_t *)(addr);

  iframe->edi = 1;
  iframe->esi = 2;
  iframe->ebp = 3;
  iframe->esp_dummy = 4;
  iframe->ebx = 5;
  iframe->edx = 6;
  iframe->ecx = 7;
  iframe->eax = 8;

  iframe->gs = 0;
  iframe->ds = GET_SEL(3 * 8, SA_RPL3);
  iframe->es = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs = GET_SEL(4 * 8, SA_RPL3);
  iframe->eip = eip;
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp = esp; // 设置用户态堆栈
  current->user_mode = 1;
  tss.esp0 = current->top;
  change_page_task_id(current_task()->tid, iframe->esp - 64 * 1024, 64 * 1024);
  io_sti();
  asm volatile("movl %0, %%esp\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  for (;;)
    ;
}

void task_kill(unsigned tid) {
  for (int i = 0; i < 255; i++) {
    if (m[i].state == EMPTY || m[i].state == WILL_EMPTY || m[i].state == READY)
      continue;
    if (m[i].tid == tid)
      continue;
    if (m[i].ptid == tid) {
      task_kill(m[i].tid);
    }
  }
  io_cli();
  free_pde(m[tid].pde);
  gc(tid); // 释放内存
  if (m[tid].Pkeyfifo) {
    page_free(m[tid].Pkeyfifo->buf, 4096);
    free(m[tid].Pkeyfifo);
  }
  if (m[tid].Ukeyfifo) {
    page_free(m[tid].Ukeyfifo->buf, 4096);
    free(m[tid].Ukeyfifo);
  }
  m[tid].urgent = 0;
  m[tid].fpu_flag = 0;
  m[tid].fifosleep = 0;
  m[tid].mx = 0;
  m[tid].my = 0;
  m[tid].line = NULL;
  m[tid].jiffies = 0;
  m[tid].timer = NULL;
  m[tid].nfs = NULL;
  m[tid].mm = NULL;
  m[tid].waittid = -1;
  m[tid].state = WILL_EMPTY;
  m[tid].alloc_addr = 0;
  m[tid].alloc_size = 0;
  m[tid].running = 0;
  m[tid].ready = 0;
  m[tid].pde = 0;
  m[tid].ipc_header.now = 0;
  m[tid].sigint_up = 0;
  lock_init(&(m[tid].ipc_header.l));
  for (int k = 0; k < MAX_IPC_MESSAGE; k++) {
    m[tid].ipc_header.messages[k].from_tid = -1;
    m[tid].ipc_header.messages[k].flag1 = 0;
    m[tid].ipc_header.messages[k].flag2 = 0;
  }
  for (int k = 0; k < 30; k++) {
    m[tid].handler[k] = 0;
  }
  if (m[tid].ptid != -1 && m[m[tid].ptid].waittid == tid) {
    m[m[tid].ptid].state = RUNNING;
  }

  m[tid].ptid = -1;
  io_sti();
  if (get_task(tid) == current_task())
    for (;;)
      ;
}

mtask *current_task() { return current; }
int into_mtask() {
  init_task();
  set_cr0(get_cr0() & ~(CR0_EM | CR0_TS));
  asm volatile("fninit");
  asm volatile("fnsave (%%eax) \n" ::"a"(&public_fpu));
  fpu_disable();
  struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *)ADR_GDT;
  memset(&tss, 0, sizeof(tss));
  tss.ss0 = 1 * 8;
  set_segmdesc(gdt + 103, 103, &tss, AR_TSS32);
  load_tr(103 * 8);
  idle_task = create_task(idle, page_malloc(64 * 1024) + 64 * 1024, 1, 3);
  create_task(init, page_malloc(64 * 1024) + 64 * 1024, 5, 1);
  set_cr0(get_cr0() | CR0_EM | CR0_TS | CR0_NE);
  task_start(&(m[0]));
}
void task_set_fifo(mtask *task, struct FIFO8 *kfifo, struct FIFO8 *mfifo) {
  task->keyfifo = kfifo;
  task->mousefifo = mfifo;
}
struct FIFO8 *task_get_key_fifo(mtask *task) { return task->keyfifo; }
void task_sleep(mtask *task) {
  task->state = SLEEPING;
  task->fifosleep = 1;
}
void task_wake_up(mtask *task) {
  task->state = RUNNING;
  task->fifosleep = 0;
}
void task_run(mtask *task) {
  // 加急一下
  task->urgent = 1;
  task->ready = 1;
}
void task_fifo_sleep(mtask *task) { task->fifosleep = 1; }
struct FIFO8 *task_get_mouse_fifo(mtask *task) { return task->mousefifo; }
void task_lock() {
  if (current_task()->ptid == -1) {
    for (int i = 0; i < 255; i++) {
      if (m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
          m[i].state == READY)
        continue;
      if (m[i].tid == get_tid(current_task()))
        continue;
      if (m[i].ptid == get_tid(current_task()) && m[i].state == 1) {
        m[i].state = WAITING; // WAITING
      }
    }
  } else {
    for (int i = 0; i < 255; i++) {
      if (m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
          m[i].state == READY)
        continue;
      if (m[i].tid == get_tid(current_task()))
        continue;
      if ((m[i].tid == current_task()->ptid ||
           m[i].ptid == current_task()->ptid) &&
          m[i].state == RUNNING) {
        m[i].state = WAITING; // WAITING
      }
    }
  }
}
void task_unlock() {
  if (current_task()->ptid == -1) {
    for (int i = 0; i < 255; i++) {
      if (m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
          m[i].state == READY)
        continue;
      if (m[i].tid == get_tid(current_task()))
        continue;
      if (m[i].ptid == get_tid(current_task()) && m[i].state == 2) {
        m[i].state = RUNNING; // RUNNING
      }
    }
  } else {
    for (int i = 0; i < 255; i++) {
      if (m[i].state == EMPTY || m[i].state == WILL_EMPTY ||
          m[i].state == READY)
        continue;
      if (m[i].tid == get_tid(current_task()))
        continue;
      if ((m[i].tid == current_task()->ptid ||
           m[i].ptid == current_task()->ptid) &&
          m[i].state == WAITING) {
        m[i].state = RUNNING; // RUNNING
      }
    }
  }
}
uint32_t get_father_tid(mtask *t) {
  if (t->ptid == -1) {
    return get_tid(t);
  }
  return get_father_tid(get_task(t->ptid));
}
void task_fall_blocked(enum STATE state) {
  if (current_task()->ready == 1)
    return;
  current_task()->state = state;
  current_task()->ready = 0;
  task_next();
}
void waittid(uint32_t tid) {
  mtask *t = get_task(tid);
  if (!t)
    return;
  if (t->ptid != current_task()->tid)
    return;
  current_task()->waittid = tid;
  while (t->state != EMPTY && t->ptid == current_task()->tid)
    task_fall_blocked(WAITING);
}
void mtask_stop() { mtask_stop_flag = 1; }
void mtask_start() { mtask_stop_flag = 0; }
void mtask_run_now(mtask *obj) { next_set = obj; }
