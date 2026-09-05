#include <arch.h>
#include <dos.h>
#include <executable.h>
#include <irq.h>
#include <limits.h>
#include <math_util.h>
#include <user_space.h>
extern char *shell_data;
extern unsigned shell_size;
#define PAGE_SIZE_BYTES 0x1000u
void task_to_user_mode_shell(void);

static bool task_map_user_pages(uintptr_t start, size_t count) {
  if (count != 0 && count - 1 >
                        ((uintptr_t)-1 - start) / PAGE_SIZE_BYTES) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (!page_link(start + i * PAGE_SIZE_BYTES)) {
      return false;
    }
  }
  return true;
}

bool user_runtime_layout_calculate(uintptr_t aligned_image_end,
                                   size_t heap_pages, size_t stack_pages,
                                   bool uses_status_page, uintptr_t entry,
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
  if (total_pages > (uintptr_t)-1 || runtime_end > USER_HEAP_END ||
      stack_top > runtime_end ||
      (uses_status_page && runtime_end > (uint64_t)USER_HEAP_END)) {
    return false;
  }
  layout->total_pages = (uint32_t)total_pages;
  layout->stack_top = (uintptr_t)stack_top;
  layout->allocation_base = (uintptr_t)stack_top;
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
  uintptr_t *r = (uintptr_t *)current_task()->line;
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
  current_task()->alloc_size = malloc(sizeof(*current_task()->alloc_size));
  if (current_task()->alloc_size == NULL) {
    return false;
  }
  current_task()->alloced = 1;
  return true;
}

static void task_app_setup_memory_size(void) {
  *(current_task()->alloc_size) = 2 * 1024 * 1024;
}

void task_app() {
  char *filename = task_app_take_launch_request();
  if (!task_app_setup_fifos() || !task_app_setup_memory_alloc()) {
    task_exit(-1);
    return;
  }
  task_app_setup_memory_size();
  if (!arch_address_space_prepare_exec(current_task()->address_space)) {
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

  if (!arch_address_space_prepare_exec(current_task()->address_space)) {
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
  uintptr_t user_eip;
  uintptr_t image_end;
  if (!arch_executable_validate(p, shell_size, &user_eip, &image_end) ||
      image_end > (uintptr_t)-1 - (PAGE_SIZE_BYTES - 1)) {
    extern mtask *mouse_use_task;
    if (mouse_use_task == current_task()) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  uintptr_t alloc_addr =
      (image_end + PAGE_SIZE_BYTES - 1) & ~(uintptr_t)(PAGE_SIZE_BYTES - 1);
  size_t pg = size_div_round_up(*(task->alloc_size), PAGE_SIZE_BYTES);
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

  if (!arch_executable_load(p, shell_size, &user_eip)) {
    task_exit(-1);
    return;
  }
  *(unsigned char *)(USER_HEAP_END) = 1;
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);

  kernel_lock_leave();
  arch_task_enter_user(user_eip, layout.stack_top, 0);
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
  uintptr_t user_eip;
  uintptr_t image_end;
  if (!arch_executable_validate(p, executable_size, &user_eip, &image_end) ||
      image_end > (uintptr_t)-1 - (PAGE_SIZE_BYTES - 1)) {
    page_free(p, executable_size);
    extern mtask *mouse_use_task;
    if (mouse_use_task == task) {
      mouse_sleep(&mdec);
    }
    task_exit(-1);
    for (;;)
      ;
  }
  uintptr_t alloc_addr =
      (image_end + PAGE_SIZE_BYTES - 1) & ~(uintptr_t)(PAGE_SIZE_BYTES - 1);
  size_t pg = size_div_round_up(*(task->alloc_size), PAGE_SIZE_BYTES);
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
  if (!arch_executable_load(p, executable_size, &user_eip)) {
    task_exit(-1);
    return;
  }
  page_free(p, executable_size);
  if (uses_status_page) {
    *(unsigned char *)(USER_HEAP_END) = 0;
  }
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);

  kernel_lock_leave();
  arch_task_enter_user(user_eip, layout.stack_top, 0);
}
int os_execute(char *filename, char *line) {
  if (filename == NULL || line == NULL) {
    return -1;
  }
  extern mtask *mouse_use_task;
  mtask *backup = mouse_use_task;
  char *fm = (char *)malloc(strlen(filename) + 1);
  char *p1 = malloc(strlen(line) + 1);
  uintptr_t *r = page_malloc_one_no_mark();
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
  uintptr_t *r = page_malloc_one_no_mark();
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
