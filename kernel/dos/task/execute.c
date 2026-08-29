#include <ELF.h>
#include <arch.h>
#include <arch/x86/interrupt.h>
#include <arch/x86/control.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <user_space.h>
extern char *shell_data;
extern unsigned shell_size;
#define DIDX(addr) (((unsigned)addr >> 22) & 0x3ff) // 获取 addr 的页目录索引
#define TIDX(addr) (((unsigned)addr >> 12) & 0x3ff) // 获取 addr 的页表索引
#define PAGE(idx) ((unsigned)idx << 12) // 获取页索引 idx 对应的页开始的位置
#define PAGE_SIZE_BYTES 0x1000u
#define PAGE_ENTRY_BYTES sizeof(uint32_t)
#define PAGE_ENTRY_ADDR_MASK 0xfffff000u
#define PAGE_ENTRY_FLAG_MASK 0x00000fffu
#define PAGE_USER_CLONE_BASE USER_SPACE_START
#define PAGE_USER_PRESENT_FLAGS (PG_P | PG_USU)
#define PAGE_USER_RW_FLAGS (PG_P | PG_USU | PG_RWW)
unsigned div_round_up(unsigned num, unsigned size);
void task_to_user_mode_shell(void);

#ifdef KERNEL_PERF
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
#endif

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

static inline bool page_entry_has_all(uint32_t entry, uint32_t flags) {
  return (entry & flags) == flags;
}

static inline uint32_t *page_table_entry_from_dir(uint32_t pde_entry,
                                                  unsigned index) {
  return (uint32_t *)(page_entry_addr(pde_entry) + index * PAGE_ENTRY_BYTES);
}

static bool task_map_user_pages(uint32_t start, uint32_t count) {
  if (count != 0 && count - 1 > (UINT_MAX - start) / PAGE_SIZE_BYTES) {
    return false;
  }
  for (uint32_t i = 0; i < count; i++) {
    if (!page_link(start + i * PAGE_SIZE_BYTES)) {
      return false;
    }
  }
  return true;
}

bool user_runtime_layout_calculate(uint32_t aligned_image_end,
                                   uint32_t heap_pages,
                                   uint32_t stack_pages,
                                   bool uses_status_page, uint32_t entry,
                                   struct user_runtime_layout *layout) {
  if (layout == NULL || aligned_image_end < USER_SPACE_START ||
      aligned_image_end >= USER_HEAP_END ||
      (aligned_image_end & (PAGE_SIZE_BYTES - 1)) != 0 ||
      entry < USER_SPACE_START || entry >= USER_HEAP_END) {
    return false;
  }
  uint64_t total_pages = (uint64_t)heap_pages + stack_pages;
  uint64_t runtime_end =
      (uint64_t)aligned_image_end + total_pages * PAGE_SIZE_BYTES;
  uint64_t stack_top =
      (uint64_t)aligned_image_end + (uint64_t)stack_pages * PAGE_SIZE_BYTES;
  if (total_pages > UINT_MAX || runtime_end > USER_HEAP_END ||
      stack_top > runtime_end ||
      (uses_status_page && runtime_end > (uint64_t)USER_HEAP_END)) {
    return false;
  }
  layout->total_pages = (uint32_t)total_pages;
  layout->stack_top = (uint32_t)stack_top;
  layout->allocation_base = (uint32_t)stack_top;
  return true;
}

static bool task_clone_user_page_tables(unsigned pde) {
  for (uint32_t offset = DIDX(PAGE_USER_CLONE_BASE) * PAGE_ENTRY_BYTES;
       offset < PAGE_SIZE_BYTES; offset += PAGE_ENTRY_BYTES) {
    uint32_t *pde_entry = (uint32_t *)(pde + offset);
    if (!page_entry_has_all(*pde_entry, PAGE_USER_PRESENT_FLAGS)) {
      continue;
    }

    unsigned table_refs = page_ref_count(page_entry_addr(*pde_entry));
    if (table_refs == 0) {
      return false;
    }
    if (table_refs > 1) {
      uint32_t old = page_entry_addr(*pde_entry);
      void *new_table = page_malloc_one_count_from_4gb();
      if (new_table == NULL) {
        return false;
      }
      *pde_entry = (unsigned)new_table;
      memcpy(new_table, (void *)old, PAGE_SIZE_BYTES);
      page_ref_release(old);
    }
    *pde_entry = page_entry_add_flags(*pde_entry, PAGE_USER_RW_FLAGS);
    for (uint32_t index = 0; index < PAGE_SIZE_BYTES / PAGE_ENTRY_BYTES;
         index++) {
      uint32_t *pte_entry = page_table_entry_from_dir(*pde_entry, index);
      if (page_entry_has_any(*pte_entry, PG_SHARED)) {
        if (!page_entry_has_all(*pte_entry, PAGE_USER_PRESENT_FLAGS)) {
          return false;
        }
        /* A new executable does not inherit process-owned shared mappings. */
        page_ref_release(page_entry_addr(*pte_entry));
        *pte_entry = 0;
      }
    }
  }
  return true;
}
static __attribute__((optimize("O0"))) char *task_app_take_launch_request(void) {
  char *filename;
  irq_state_t state;
  for (;;) {
    state = irq_save();
    if (current_task()->line) {
      break;
    }
    task_next();
    irq_restore(state);
  }
  unsigned *r = (unsigned *)current_task()->line;
  filename = (char *)r[0];
  current_task()->line = (char *)r[1];
  irq_restore(state);
  page_free_one(r);
  return filename;
}

static bool task_app_setup_fifos(void) {
  char *kfifo = (char *)page_malloc_one();
  char *mfifo = (char *)page_malloc_one();
  char *kbuf = (char *)page_malloc_one();
  char *mbuf = (char *)page_malloc_one();
  if (kfifo == NULL || mfifo == NULL || kbuf == NULL || mbuf == NULL) {
    return false;
  }

  fifo8_init((struct FIFO8 *)kfifo, 4096, (unsigned char *)kbuf);
  fifo8_init((struct FIFO8 *)mfifo, 4096, (unsigned char *)mbuf);
  task_set_fifo(current_task(), (struct FIFO8 *)kfifo, (struct FIFO8 *)mfifo);
  return true;
}

static bool task_app_setup_memory_alloc(void) {
  current_task()->alloc_size = (uint32_t *)malloc(4);
  if (current_task()->alloc_size == NULL) {
    return false;
  }
  current_task()->alloced = 1;
  return true;
}

static void task_app_setup_memory_size(void) {
  *(current_task()->alloc_size) = 2 * 1024 * 1024;
}

static __attribute__((noinline)) unsigned task_app_get_pde(void) {
  unsigned pde;
  memcpy(&pde, &current_task()->pde, sizeof(pde));
  return pde;
}

static bool task_app_clone_user_space(unsigned pde) {
  irq_state_t state = irq_save();
  x86_cr3_write(PDE_ADDRESS);
  bool cloned = task_clone_user_page_tables(pde);
  x86_cr3_write(pde);
  irq_restore(state);
  return cloned;
}

void task_app() {
  unsigned pde;
  char *filename = task_app_take_launch_request();
  if (!task_app_setup_fifos() || !task_app_setup_memory_alloc()) {
    task_exit(-1);
    return;
  }
  task_app_setup_memory_size();
  pde = task_app_get_pde();
  if (!task_app_clone_user_space(pde)) {
    task_exit(-1);
    return;
  }
  task_to_user_mode_elf(filename);
  for (;;)
    ;
}
void task_shell() {
  while (!current_task()->line)
    ;
  if (!task_app_setup_fifos() || !task_app_setup_memory_alloc()) {
    task_exit(-1);
    return;
  }
  *(current_task()->alloc_size) = 1 * 1024 * 1024;

  unsigned pde = current_task()->pde;
  irq_state_t state = irq_save();
  x86_cr3_write(PDE_ADDRESS);
  bool cloned = task_clone_user_page_tables(pde);
  x86_cr3_write(pde);
  irq_restore(state);
  if (!cloned) {
    task_exit(-1);
    return;
  }
  task_to_user_mode_shell();
  for (;;)
    ;
}
void task_to_user_mode_shell() {
  mtask *task = current_task();
  char *p = shell_data;
  uint32_t user_eip;
  uint32_t image_end;
  if (!elf32_validate_executable(p, shell_size, &user_eip, &image_end) ||
      image_end > UINT_MAX - (PAGE_SIZE_BYTES - 1)) {
    extern mtask *mouse_use_task;
    if (mouse_use_task == current_task()) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  unsigned alloc_addr =
      (image_end + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);
  unsigned pg = div_round_up(*(task->alloc_size), 0x1000);
  struct user_runtime_layout layout;
  if (!user_runtime_layout_calculate(alloc_addr, pg, 128, true, user_eip,
                                     &layout) ||
      !task_map_user_pages(alloc_addr, layout.total_pages)) {
    task_exit(-1);
    return;
  }
  if (!page_link(USER_HEAP_END)) {
    task_exit(-1);
    return;
  }
  task->alloc_addr = layout.allocation_base;

  if (!elf32_load_executable(p, shell_size, &user_eip)) {
    task_exit(-1);
    return;
  }
  *(unsigned char *)(USER_HEAP_END) = 1;
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);
#ifdef KERNEL_PERF
  perf_boot_stop_and_dump("shell-iret");
#endif

  x86_interrupt_frame_t iframe;
  x86_user_frame_init(&iframe, user_eip, layout.stack_top);
  kernel_lock_leave();
  x86_return_to_user(&iframe);
}
void task_to_user_mode_elf(char *filename) {
  mtask *task = current_task();
  vfs_handle_t *stream = NULL;
  vfs_stat_t status;
  if (vfs_open(task->fs_context, filename, VFS_OPEN_READ, &stream) < 0 ||
      vfs_fstat(stream, &status) < 0 || status.type != VFS_NODE_FILE ||
      status.size == 0 || status.size > INT_MAX) {
    if (stream != NULL) {
      vfs_close(stream);
    }
    task_exit(-1);
    for (;;) {
    }
  }
  int executable_size = status.size;
  char *p = page_malloc(executable_size);
  if (p == NULL) {
    vfs_close(stream);
    task_exit(-1);
    for (;;) {
    }
  }
  change_page_task_id(task->tid, p, executable_size);
  int read = vfs_read(stream, p, executable_size);
  vfs_close(stream);
  if (read != executable_size) {
    task_exit(-1);
    for (;;) {
    }
  }
  uint32_t user_eip;
  uint32_t image_end;
  if (!elf32_validate_executable(p, executable_size, &user_eip, &image_end) ||
      image_end > UINT_MAX - (PAGE_SIZE_BYTES - 1)) {
    page_free(p, executable_size);
    extern mtask *mouse_use_task;
    if (mouse_use_task == task) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  unsigned alloc_addr =
      (image_end + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);
  unsigned pg = div_round_up(*(task->alloc_size), 0x1000);
  bool uses_status_page = task->ptid != (uint32_t)-1;
  struct user_runtime_layout layout;
  if (!user_runtime_layout_calculate(alloc_addr, pg, 128 * 4,
                                     uses_status_page, user_eip, &layout) ||
      !task_map_user_pages(alloc_addr, layout.total_pages)) {
    task_exit(-1);
    return;
  }
  if (uses_status_page) {
    if (!page_link(USER_HEAP_END)) {
      task_exit(-1);
      return;
    }
  }
  // *(unsigned int *)(0xb5000000) = 2;
  // logk("value = %08x\n",*(unsigned int *)(0xb5000000));
  task->alloc_addr = layout.allocation_base;
  if (!elf32_load_executable(p, executable_size, &user_eip)) {
    task_exit(-1);
    return;
  }
  page_free(p, executable_size);
  if (uses_status_page) {
    *(unsigned char *)(USER_HEAP_END) = 0;
  }
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);
#ifdef KERNEL_PERF
  if (task_is_boot_shell_name(filename)) {
    perf_boot_stop_and_dump("psh-iret");
  }
#endif

  x86_interrupt_frame_t iframe;
  x86_user_frame_init(&iframe, user_eip, layout.stack_top);
  kernel_lock_leave();
  x86_return_to_user(&iframe);
}
int os_execute(char *filename, char *line) {
  if (filename == NULL || line == NULL) {
    return -1;
  }
  extern mtask *mouse_use_task;
  mtask *backup = mouse_use_task;
  char *fm = (char *)malloc(strlen(filename) + 1);
  char *p1 = malloc(strlen(line) + 1);
  unsigned *r = page_malloc_one_no_mark();
  if (fm == NULL || p1 == NULL || r == NULL) {
    free(fm);
    free(p1);
    if (r != NULL) {
      page_free_one(r);
    }
    return -1;
  }
  strcpy(fm, filename);
  strcpy(p1, line);
  r[0] = (uintptr_t)fm;
  r[1] = (uintptr_t)p1;

  mtask *t = create_task((uintptr_t)task_app, 1);
  if (t == NULL) {
    free(fm);
    free(p1);
    page_free_one(r);
    return -1;
  }
  t->ptid = current_task()->tgid;
  int old = current_task()->sigint_up;
  t->sigint_up = 1;
  task_set_name(t, filename);
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  t->tty_session = current_task()->tty_session;
  int o = current_task()->fifosleep;
  t->line = (char *)r;
  if (!task_publish(t)) {
    task_abort_creation(t);
    free(p1);
    free(fm);
    page_free_one(r);
    return -1;
  }
  current_task()->sigint_up = 0;
  current_task()->TTY = NULL;
  current_task()->fifosleep = 1;

  unsigned status = waittid(t->tid);
  current_task()->fifosleep = o;

  free(p1);
  free(fm);
  current_task()->TTY = current_task()->tty_session == tty_backup
                            ? tty_backup
                            : current_task()->tty_session;
  if (backup) {
    mouse_ready(&mdec);
    mouse_use_task = backup;
  } else {
    mouse_sleep(&mdec);
  }
  current_task()->sigint_up = old;

  return status;
}
int os_execute_shell(const char *line, size_t line_length) {
  if (line == NULL || line_length >= INT_MAX) {
    return -1;
  }

  char *line_copy = malloc(line_length + 1);
  if (line_copy == NULL) {
    return -1;
  }
  memcpy(line_copy, line, line_length);
  line_copy[line_length] = '\0';

  mtask *t = create_task((uintptr_t)task_shell, 1);
  if (t == NULL) {
    free(line_copy);
    return -1;
  }
  task_set_name(t, "psh.bin");
  int old = current_task()->sigint_up;
  t->sigint_up = 1;
  t->ptid = current_task()->tgid;
  struct tty *tty_backup = current_task()->TTY;
  t->TTY = current_task()->TTY;
  t->tty_session = current_task()->tty_session;
  int o = current_task()->fifosleep;
  t->line = line_copy;
  if (!task_publish(t)) {
    task_abort_creation(t);
    free(line_copy);
    return -1;
  }
  current_task()->sigint_up = 0;
  current_task()->TTY = NULL;
  current_task()->fifosleep = 1;
  unsigned status = waittid(t->tid);
  current_task()->fifosleep = o;
  free(line_copy);
  current_task()->TTY = current_task()->tty_session == tty_backup
                            ? tty_backup
                            : current_task()->tty_session;
  current_task()->sigint_up = old;
  return status;
}
void os_execute_no_ret(char *filename, char *line) {
  unsigned *r = page_malloc_one_no_mark();
  if (r == NULL) {
    return;
  }
  r[0] = (uintptr_t)filename;
  r[1] = (uintptr_t)line;
  mtask *t = create_task((uintptr_t)task_app, 1);
  if (t == NULL) {
    page_free_one(r);
    return;
  }
  t->ptid = 0; /* detached tasks are adopted by the idle reaper */
  task_set_name(t, filename);
  t->TTY = current_task()->TTY;
  t->tty_session = current_task()->tty_session;
  t->line = (char *)r;
  if (!task_publish(t)) {
    task_abort_creation(t);
    page_free_one(r);
    return;
  }
  current_task()->TTY = NULL;
}
