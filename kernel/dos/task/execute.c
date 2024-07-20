#include <ELF.h>
#include <dos.h>
extern char             *shell_data;
extern struct TSS32      tss;
extern struct PAGE_INFO *pages;
#define IDX(addr)  ((unsigned)addr >> 12)           // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx)  ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
void task_app() {
  char *filename;
  while (!current_task()->line) {
    io_sti();
    task_next();
    //printk("%d\n",current_task()->line);
  }
  unsigned *r          = current_task()->line;
  filename             = r[0];
  current_task()->line = r[1];
  page_free_one(r);
  logk("%08x", current_task()->top);

  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf  = (char *)page_malloc_one();
  char *mbuf  = (char *)page_malloc_one();

  fifo8_init((struct FIFO8 *)kfifo, 4096, (u8 *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (u8 *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  current_task()->alloc_size    = (u32 *)malloc(4);
  current_task()->alloced       = 1;
  *(current_task()->alloc_size) = 2 * 1024 * 1024;
  unsigned pde                  = current_task()->pde;
  io_cli();
  set_cr3(PDE_ADDRESS);
  logk("P1 %08x", current_task()->pde);
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    u32 *pde_entry = (u32 *)(pde + i);

    if ((*pde_entry & PG_SHARED) || pages[IDX(*pde_entry)].count > 1) {
      // if (pde_entry == 0x08e6b718) {
      //   while (true)
      //     ;
      // }
      if (pages[IDX(*pde_entry)].count > 1) {
        u32 old    = *pde_entry & 0xfffff000;
        u32 attr   = *pde_entry & 0xfff;
        *pde_entry = (unsigned)page_malloc_one_count_from_4gb();
        memcpy((void *)(*pde_entry), (void *)old, 0x1000);
        pages[IDX(old)].count--;
        *pde_entry |= PG_USU | PG_P | PG_RWW;
      } else {
        *pde_entry &= 0xfffff;
        *pde_entry |= 7;
      }
    }
    unsigned p = *pde_entry & (0xfffff000);
    for (int j = 0; j < 0x1000; j += 4) {
      u32 *pte_entry = (u32 *)(p + j);
      if ((*pte_entry & PG_SHARED)) {
        *pte_entry &= 0xfffff000;
        *pte_entry |= PG_USU | PG_P;
      }
    }
  }
  io_sti();
  set_cr3(pde);
  char tmp[100];
  task_to_user_mode_elf(filename);
  while (true)
    ;
}
void task_shell() {
  while (!current_task()->line)
    ;
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf  = (char *)page_malloc_one();
  char *mbuf  = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (u8 *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (u8 *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  current_task()->alloc_size    = (u32 *)malloc(4);
  current_task()->alloced       = 1;
  *(current_task()->alloc_size) = 1 * 1024 * 1024;

  unsigned pde = current_task()->pde;
  io_cli();
  set_cr3(PDE_ADDRESS);
  for (int i = DIDX(0x70000000) * 4; i < 0x1000; i += 4) {
    u32 *pde_entry = (u32 *)(pde + i);
    if ((*pde_entry & PG_SHARED) || pages[IDX(*pde_entry)].count > 1) {

      if (pages[IDX(*pde_entry)].count > 1) {
        u32 old    = *pde_entry & 0xfffff000;
        u32 attr   = *pde_entry & 0xfff;
        *pde_entry = (unsigned)page_malloc_one_count_from_4gb();
        memcpy((void *)(*pde_entry), (void *)old, 0x1000);
        pages[IDX(old)].count--;
        *pde_entry |= PG_USU | PG_P | PG_RWW;
      } else {
        *pde_entry &= 0xfffff;
        *pde_entry |= 7;
      }
    } else {
    }
    unsigned p = *pde_entry & (0xfffff000);
    for (int j = 0; j < 0x1000; j += 4) {
      u32 *pte_entry = (u32 *)(p + j);
      if ((*pte_entry & PG_SHARED)) {
        *pte_entry &= 0xfffff000;
        *pte_entry |= PG_USU | PG_P;
      }
    }
  }
  io_sti();
  set_cr3(pde);
  char tmp[100];
  task_to_user_mode_shell();
  while (true)
    ;
}
void task_to_user_mode_shell() {

  unsigned addr = (unsigned)current_task()->top;

  addr                 -= sizeof(intr_frame_t);
  intr_frame_t *iframe  = (intr_frame_t *)(addr);

  iframe->edi       = 1;
  iframe->esi       = 2;
  iframe->ebp       = 3;
  iframe->esp_dummy = 4;
  iframe->ebx       = 5;
  iframe->edx       = 6;
  iframe->ecx       = 7;
  iframe->eax       = 8;

  iframe->gs     = GET_SEL(5 * 8, SA_RPL3);
  iframe->ds     = GET_SEL(3 * 8, SA_RPL3);
  iframe->es     = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs     = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss     = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs     = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp    = NULL; // 设置用户态堆栈
  char *p        = shell_data;
  if (!elf32Validate(p)) {
    while (FindForCount(1, vfs_now->path) != NULL) {
      free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val);
      DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
      extern mtask *mouse_use_task;
      if (mouse_use_task == current_task()) { mouse_sleep(&mdec); }
    }
    DeleteList(vfs_now->path);
    free(vfs_now->cache);
    free((void *)vfs_now);
    task_exit(-1);
    while (true)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr(p) & 0xfffff000) + 0x1000;
  unsigned pg         = div_round_up(*(current_task()->alloc_size), 0x1000);
  for (int i = 0; i < pg + 128; i++) {
    // logk("%d",i);
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp  = alloc_addr + 128 * 0x1000;
  alloc_addr           += 128 * 0x1000;
  iframe->esp           = alloced_esp;
  page_link(0xf0000000);
  *(u8 *)(0xf0000000)        = 1;
  current_task()->alloc_addr = alloc_addr;

  iframe->eip               = load_elf(p);
  current_task()->user_mode = 1;
  tss.esp0                  = current_task()->top;
  change_page_task_id(current_task()->tid, iframe->esp - 512 * 1024, 512 * 1024);

  asm volatile("movl %0, %%esp\n"
               "xchg %%bx,%%bx\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  while (true)
    ;
}
void task_to_user_mode_elf(char *filename) {

  unsigned addr = (unsigned)current_task()->top;

  addr                 -= sizeof(intr_frame_t);
  intr_frame_t *iframe  = (intr_frame_t *)(addr);

  iframe->edi       = 1;
  iframe->esi       = 2;
  iframe->ebp       = 3;
  iframe->esp_dummy = 4;
  iframe->ebx       = 5;
  iframe->edx       = 6;
  iframe->ecx       = 7;
  iframe->eax       = 8;

  iframe->gs     = GET_SEL(5 * 8, SA_RPL3);
  iframe->ds     = GET_SEL(3 * 8, SA_RPL3);
  iframe->es     = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs     = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss     = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs     = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp    = NULL; // 设置用户态堆栈
  tss.eflags     = 0x202;
  char *p        = page_malloc(vfs_filesize(filename));
  vfs_readfile(filename, p);
  if (!elf32Validate(p)) {
    page_free(p, vfs_filesize(filename));
    while (FindForCount(1, vfs_now->path) != NULL) {
      free(FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val);
      DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
      extern mtask *mouse_use_task;
      if (mouse_use_task == current_task()) { mouse_sleep(&mdec); }
    }
    DeleteList(vfs_now->path);
    free(vfs_now->cache);
    free((void *)vfs_now);
    task_exit(-1);
    while (true)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr(p) & 0xfffff000) + 0x1000;
  unsigned pg         = div_round_up(*(current_task()->alloc_size), 0x1000);
  for (int i = 0; i < pg + 128 * 4; i++) {
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp  = alloc_addr + 128 * 0x1000 * 4;
  alloc_addr           += 128 * 0x1000 * 4;
  iframe->esp           = alloced_esp;
  if (current_task()->ptid != -1) {
    page_link(0xf0000000);
    *(u8 *)(0xf0000000) = 0;
  }
  // *(u32 *)(0xb5000000) = 2;
  // logk("value = %08x",*(u32 *)(0xb5000000));
  current_task()->alloc_addr = alloc_addr;
  iframe->eip                = load_elf(p);
  logk("eip = %08x", &(iframe->eip));
  current_task()->user_mode = 1;
  tss.esp0                  = current_task()->top;
  change_page_task_id(current_task()->tid, p, vfs_filesize(filename));
  change_page_task_id(current_task()->tid, iframe->esp - 512 * 1024, 512 * 1024);
  logk("%d", get_interrupt_state());
  asm volatile("movl %0, %%esp\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  while (true)
    ;
}
int os_execute(char *filename, char *line) {
  extern mtask *mouse_use_task;
  mtask        *backup = mouse_use_task;
  extern int    init_ok_flag;
  char         *fm = (char *)malloc(strlen(filename) + 1);
  strcpy(fm, filename);
  init_ok_flag = 0;

  mtask *t = create_task(task_app, 0, 1, 1);
  // 轮询
  t->train = 0;
  vfs_change_disk_for_task(current_task()->nfs->drive, t);
  List *l;
  char *path;
  for (int i = 1; FindForCount(i, current_task()->nfs->path) != NULL; i++) {
    l    = FindForCount(i, current_task()->nfs->path);
    path = (char *)l->val;
    t->nfs->cd(t->nfs, path);
  }
  init_ok_flag              = 1;
  t->ptid                   = current_task()->tid;
  int old                   = current_task()->sigint_up;
  current_task()->sigint_up = 0;
  t->sigint_up              = 1;
  struct tty *tty_backup    = current_task()->TTY;
  t->TTY                    = current_task()->TTY;
  current_task()->TTY       = NULL;
  char *p1                  = malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o                     = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  unsigned *r               = page_malloc_one_no_mark();
  r[0]                      = fm;
  r[1]                      = p1;
  t->line                   = r;

  unsigned status           = waittid(t->tid);
  current_task()->fifosleep = o;

  free(p1);
  free(fm);
  current_task()->TTY = tty_backup;
  if (backup) {
    mouse_ready(&mdec);
    mouse_use_task = backup;
  } else {
    mouse_sleep(&mdec);
  }
  current_task()->sigint_up = old;

  return status;
}
int os_execute_shell(char *line) {
  extern int init_ok_flag;
  init_ok_flag              = 0;
  mtask *t                  = create_task(task_shell, 0, 1, 1);
  t->nfs                    = current_task()->nfs;
  t->train                  = 1;
  int old                   = current_task()->sigint_up;
  current_task()->sigint_up = 0;
  t->sigint_up              = 1;
  init_ok_flag              = 1;
  t->ptid                   = current_task()->tid;
  struct tty *tty_backup    = current_task()->TTY;
  t->TTY                    = current_task()->TTY;
  current_task()->TTY       = NULL;
  char *p1                  = malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o                     = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  t->line                   = p1;
  // io_sti();
  unsigned status           = waittid(t->tid);
  current_task()->fifosleep = o;
  free(p1);
  current_task()->TTY       = tty_backup;
  current_task()->sigint_up = old;
  return status;
}
void os_execute_no_ret(char *filename, char *line) {
  mtask      *t          = create_task(task_app, 0, 1, 1);
  struct tty *tty_backup = current_task()->TTY;
  t->TTY                 = current_task()->TTY;
  current_task()->TTY    = NULL;
  unsigned *r            = page_malloc_one_no_mark();
  r[0]                   = filename;
  r[1]                   = line;
  t->line                = r;
}