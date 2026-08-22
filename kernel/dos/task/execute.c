#include <ELF.h>
#include <dos.h>
extern char *shell_data;
extern struct TSS32 tss;
extern struct PAGE_INFO *pages;
#define IDX(addr) ((unsigned)addr >> 12)            // 获取 addr 的页索引
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx) ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
#define PAGE_SIZE_BYTES 0x1000u
#define PAGE_ENTRY_BYTES sizeof(uint32_t)
#define PAGE_ENTRY_ADDR_MASK 0xfffff000u
#define PAGE_ENTRY_FLAG_MASK 0x00000fffu
#define PAGE_USER_CLONE_BASE 0x70000000u
#define PAGE_USER_PRESENT_FLAGS (PG_P | PG_USU)
#define PAGE_USER_RW_FLAGS (PG_P | PG_USU | PG_RWW)
unsigned div_round_up(unsigned num, unsigned size);
bool get_interrupt_state(void);
void task_to_user_mode_shell(void);

static int task_is_boot_shell_name(const char *filename) {
  const char *suffix;
  unsigned len;

  if (filename == NULL) {
    return 0;
  }

  len = strlen(filename);
  if (len < 7) {
    return 0;
  }

  suffix = filename + len - 7;
  return (suffix[0] == 'p' || suffix[0] == 'P') &&
         (suffix[1] == 's' || suffix[1] == 'S') &&
         (suffix[2] == 'h' || suffix[2] == 'H') && suffix[3] == '.' &&
         (suffix[4] == 'b' || suffix[4] == 'B') &&
         (suffix[5] == 'i' || suffix[5] == 'I') &&
         (suffix[6] == 'n' || suffix[6] == 'N');
}

static inline unsigned page_entry_addr(uint32_t entry) {
  return entry & PAGE_ENTRY_ADDR_MASK;
}

static inline uint32_t page_entry_flags(uint32_t entry) {
  return entry & PAGE_ENTRY_FLAG_MASK;
}

static inline uint32_t page_entry_make(unsigned addr, uint32_t flags) {
  return (addr & PAGE_ENTRY_ADDR_MASK) | (flags & PAGE_ENTRY_FLAG_MASK);
}

static inline uint32_t page_entry_add_flags(uint32_t entry, uint32_t flags) {
  return page_entry_make(page_entry_addr(entry), page_entry_flags(entry) | flags);
}

static inline bool page_entry_has_any(uint32_t entry, uint32_t flags) {
  return (entry & flags) != 0;
}

static inline uint32_t *page_table_entry_from_dir(uint32_t pde_entry,
                                                  unsigned index) {
  return (uint32_t *)(page_entry_addr(pde_entry) + index * PAGE_ENTRY_BYTES);
}

static void task_clone_user_page_tables(unsigned pde) {
  for (int i = DIDX(PAGE_USER_CLONE_BASE) * 4; i < 0x1000; i += 4) {
    uint32_t *pde_entry = (uint32_t *)(pde + i);

    if (page_entry_has_any(*pde_entry, PG_SHARED) || pages[IDX(*pde_entry)].count > 1) {
      if (pages[IDX(*pde_entry)].count > 1) {
        uint32_t old = page_entry_addr(*pde_entry);
        *pde_entry = (unsigned)page_malloc_one_count_from_4gb();
        memcpy((void *)(*pde_entry), (void *)old, PAGE_SIZE_BYTES);
        pages[IDX(old)].count--;
        *pde_entry = page_entry_add_flags(*pde_entry, PAGE_USER_RW_FLAGS);
      } else {
        *pde_entry = page_entry_add_flags(*pde_entry, PAGE_USER_RW_FLAGS);
      }
    }
    for (int j = 0; j < 0x1000 / 4; j++) {
      uint32_t *pte_entry = page_table_entry_from_dir(*pde_entry, (unsigned)j);
      if (page_entry_has_any(*pte_entry, PG_SHARED)) {
        *pte_entry = page_entry_make(page_entry_addr(*pte_entry),
                                     PAGE_USER_PRESENT_FLAGS);
      }
    }
  }
}
static __attribute__((optimize("O0"))) char *task_app_take_launch_request(void) {
  char *filename;
  while (!current_task()->line) {
    io_sti();
    task_next();
  }
  unsigned *r = (unsigned *)current_task()->line;
  filename = (char *)r[0];
  current_task()->line = (char *)r[1];
  page_free_one(r);
  logk("%08x\n", current_task()->top);
  return filename;
}

static void task_app_setup_fifos(void) {
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();

  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
}

static void task_app_setup_memory_alloc(void) {
  current_task()->alloc_size = (uint32_t *)malloc(4);
  current_task()->alloced = 1;
}

static void task_app_setup_memory_size(void) {
  *(current_task()->alloc_size) = 2 * 1024 * 1024;
}

static __attribute__((noinline)) unsigned task_app_get_pde(void) {
  unsigned pde;
  memcpy(&pde, &current_task()->pde, sizeof(pde));
  return pde;
}

static void task_app_clone_user_space(unsigned pde) {
  io_cli();
  set_cr3(PDE_ADDRESS);
  logk("P1 %08x\n", current_task()->pde);
  task_clone_user_page_tables(pde);
  io_sti();
  set_cr3(pde);
}

void task_app() {
  unsigned pde;
  char *filename = task_app_take_launch_request();
  task_app_setup_fifos();
  task_app_setup_memory_alloc();
  task_app_setup_memory_size();
  pde = task_app_get_pde();
  task_app_clone_user_space(pde);
  char tmp[100];
  task_to_user_mode_elf(filename);
  for (;;)
    ;
}
void task_shell() {
  while (!current_task()->line)
    ;
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();
  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  current_task()->alloc_size = (uint32_t *)malloc(4);
  current_task()->alloced = 1;
  *(current_task()->alloc_size) = 1 * 1024 * 1024;

  unsigned pde = current_task()->pde;
  io_cli();
  set_cr3(PDE_ADDRESS);
  task_clone_user_page_tables(pde);
  io_sti();
  set_cr3(pde);
  char tmp[100];
  task_to_user_mode_shell();
  for (;;)
    ;
}
void task_to_user_mode_shell() {

  unsigned addr = (unsigned)current_task()->top;

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

  iframe->gs = GET_SEL(5 * 8, SA_RPL3);
  iframe->ds = GET_SEL(3 * 8, SA_RPL3);
  iframe->es = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp = (uintptr_t)NULL; // 设置用户态堆栈
  char *p = shell_data;
  if (!elf32Validate((Elf32_Ehdr *)p)) {
    extern mtask *mouse_use_task;
    if (mouse_use_task == current_task()) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr((Elf32_Ehdr *)p) & 0xfffff000) + 0x1000;
  unsigned pg = div_round_up(*(current_task()->alloc_size), 0x1000);
  for (int i = 0; i < pg + 128; i++) {
    // logk("%d\n",i);
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp = alloc_addr + 128 * 0x1000;
  alloc_addr += 128 * 0x1000;
  iframe->esp = alloced_esp;
  page_link(0xf0000000);
  *(unsigned char *)(0xf0000000) = 1;
  current_task()->alloc_addr = alloc_addr;

  iframe->eip = load_elf((Elf32_Ehdr *)p);
  current_task()->user_mode = 1;
  tss.esp0 = current_task()->top;
  change_page_task_id(current_task()->tid, (void *)(uintptr_t)(iframe->esp - 512 * 1024),
                      512 * 1024);
#ifdef KERNEL_PERF
  perf_boot_stop_and_dump("shell-iret");
#endif

  asm volatile("movl %0, %%esp\n"
               "xchg %%bx,%%bx\n"
               "popa\n"
               "pop %%gs\n"
               "pop %%fs\n"
               "pop %%es\n"
               "pop %%ds\n"
               "iret" ::"m"(iframe));
  for (;;)
    ;
}
void task_to_user_mode_elf(char *filename) {

  unsigned addr = (unsigned)current_task()->top;

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

  iframe->gs = GET_SEL(5 * 8, SA_RPL3);
  iframe->ds = GET_SEL(3 * 8, SA_RPL3);
  iframe->es = GET_SEL(3 * 8, SA_RPL3);
  iframe->fs = GET_SEL(3 * 8, SA_RPL3);
  iframe->ss = GET_SEL(3 * 8, SA_RPL3);
  iframe->cs = GET_SEL(4 * 8, SA_RPL3);
  iframe->eflags = (0 << 12 | 0b10 | 1 << 9);
  iframe->esp = (uintptr_t)NULL; // 设置用户态堆栈
  tss.eflags = 0x202;
  char *p = page_malloc(vfs_filesize(filename));
  vfs_readfile(filename, p);
  if (!elf32Validate((Elf32_Ehdr *)p)) {
    page_free(p, vfs_filesize(filename));
    extern mtask *mouse_use_task;
    if (mouse_use_task == current_task()) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  unsigned alloc_addr = (elf32_get_max_vaddr((Elf32_Ehdr *)p) & 0xfffff000) + 0x1000;
  unsigned pg = div_round_up(*(current_task()->alloc_size), 0x1000);
  for (int i = 0; i < pg + 128 * 4; i++) {
    page_link(alloc_addr + i * 0x1000);
  }
  unsigned alloced_esp = alloc_addr + 128 * 0x1000 * 4;
  alloc_addr += 128 * 0x1000 * 4;
  iframe->esp = alloced_esp;
  if (current_task()->ptid != -1) {
    page_link(0xf0000000);
    *(unsigned char *)(0xf0000000) = 0;
  }
  // *(unsigned int *)(0xb5000000) = 2;
  // logk("value = %08x\n",*(unsigned int *)(0xb5000000));
  current_task()->alloc_addr = alloc_addr;
  iframe->eip = load_elf((Elf32_Ehdr *)p);
  logk("eip = %08x\n", &(iframe->eip));
  current_task()->user_mode = 1;
  tss.esp0 = current_task()->top;
  change_page_task_id(current_task()->tid, p, vfs_filesize(filename));
  change_page_task_id(current_task()->tid, (void *)(uintptr_t)(iframe->esp - 512 * 1024),
                      512 * 1024);
#ifdef KERNEL_PERF
  if (task_is_boot_shell_name(filename)) {
    perf_boot_stop_and_dump("psh-iret");
  }
#endif
  logk("%d\n", get_interrupt_state());
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
int os_execute(char *filename, char *line) {
  extern mtask *mouse_use_task;
  mtask *backup = mouse_use_task;
  extern int init_ok_flag;
  char *fm = (char *)malloc(strlen(filename) + 1);
  strcpy(fm, filename);
  init_ok_flag = 0;

  mtask *t = create_task((uintptr_t)task_app, 0, 1, 1);
  if (t == NULL) {
    init_ok_flag = 1;
    free(fm);
    return -1;
  }
  // 轮询
  t->train = 0;
  vfs_change_disk_for_task(current_task()->nfs->drive, t);
  List *l;
  char *path;
  for (int i = 1; FindForCount(i, current_task()->nfs->path) != NULL; i++) {
    l = FindForCount(i, current_task()->nfs->path);
    path = (char *)l->val;
    t->nfs->cd(t->nfs, path);
  }
  init_ok_flag = 1;
  t->ptid = current_task()->tgid;
  int old = current_task()->sigint_up;
  current_task()->sigint_up = 0;
  t->sigint_up = 1;
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  char *p1 = malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  unsigned *r = page_malloc_one_no_mark();
  r[0] = (uintptr_t)fm;
  r[1] = (uintptr_t)p1;
  t->line = (char *)r;

  unsigned status = waittid(t->tid);
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
  init_ok_flag = 0;
  mtask *t = create_task((uintptr_t)task_shell, 0, 1, 1);
  if (t == NULL) {
    init_ok_flag = 1;
    return -1;
  }
  vfs_clone_for_task(current_task(), t);
  t->train = 1;
  int old = current_task()->sigint_up;
  current_task()->sigint_up = 0;
  t->sigint_up = 1;
  init_ok_flag = 1;
  t->ptid = current_task()->tgid;
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  char *p1 = malloc(strlen(line) + 1);
  strcpy(p1, line);
  int o = current_task()->fifosleep;
  current_task()->fifosleep = 1;
  t->line = p1;
  // io_sti();
  unsigned status = waittid(t->tid);
  current_task()->fifosleep = o;
  free(p1);
  current_task()->TTY = tty_backup;
  current_task()->sigint_up = old;
  return status;
}
void os_execute_no_ret(char *filename, char *line) {
  mtask *t = create_task((uintptr_t)task_app, 0, 1, 1);
  if (t == NULL) {
    return;
  }
  t->ptid = 0; /* detached tasks are adopted by the idle reaper */
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  current_task()->TTY = NULL;
  unsigned *r = page_malloc_one_no_mark();
  r[0] = (uintptr_t)filename;
  r[1] = (uintptr_t)line;
  t->line = (char *)r;
}
