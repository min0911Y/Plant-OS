#include <arch.h>
#include <dos.h>
#include <elf.h>
#include <executable.h>
#include <input_device.h>
#include <irq.h>
#include <limits.h>
#include <loader.h>
#include <math_util.h>
#include <user_space.h>
#include <user_vm.h>
extern char default_drive;
#define PAGE_SIZE_BYTES 0x1000u
enum { USER_STACK_PAGES = 512 };

static bool task_map_user_pages(uintptr_t start, size_t count) {
  if (count != 0 && count - 1 > ((uintptr_t)-1 - start) / PAGE_SIZE_BYTES) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (!arch_user_map_zero(start + i * PAGE_SIZE_BYTES)) {
      return false;
    }
  }
  return true;
}

bool user_runtime_layout_calculate(uintptr_t aligned_image_end,
                                   size_t heap_pages, size_t stack_pages,
                                   uintptr_t entry,
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
      stack_top > runtime_end) {
    return false;
  }
  layout->total_pages = (uint32_t)total_pages;
  layout->stack_top = (uintptr_t)stack_top;
  layout->allocation_base = (uintptr_t)stack_top;
  return true;
}

typedef struct {
  size_t filename_size;
  char text[];
} task_exec_request_t;

bool task_prepare_input(mtask *task) {
  struct FIFO8 *kfifo = page_malloc_one_no_mark();
  struct FIFO8 *mfifo = page_malloc_one_no_mark();
  unsigned char *kbuf = page_malloc_one_no_mark();
  unsigned char *mbuf = page_malloc_one_no_mark();
  if (kfifo == NULL || mfifo == NULL || kbuf == NULL || mbuf == NULL) {
    page_free(kfifo, PAGE_SIZE_BYTES);
    page_free(mfifo, PAGE_SIZE_BYTES);
    page_free(kbuf, PAGE_SIZE_BYTES);
    page_free(mbuf, PAGE_SIZE_BYTES);
    return false;
  }

  fifo8_init(kfifo, PAGE_SIZE_BYTES, kbuf);
  fifo8_init(mfifo, PAGE_SIZE_BYTES, mbuf);
  task_set_fifo(task, kfifo, mfifo);
  return true;
}

void task_app(void) {
  mtask *task = current_task();
  task_exec_request_t *request = (task_exec_request_t *)task->line;
  char *filename = request->text;
  task->line = request->text + request->filename_size;
  if (!arch_address_space_prepare_exec(task->address_space)) {
    task_exit((unsigned)-1);
    return;
  }
  task_to_user_mode_elf(filename);
}

static mtask *task_create_application(const char *filename, const char *line) {
  if (!filename || !line)
    return NULL;
  size_t filename_size = strlen(filename) + 1;
  size_t command_size = strlen(line) + 1;
  if (filename_size > INT_MAX - sizeof(task_exec_request_t) ||
      command_size > INT_MAX - sizeof(task_exec_request_t) - filename_size)
    return NULL;
  mtask *task = create_task((uintptr_t)task_app, 1);
  if (!task)
    return NULL;
  size_t bytes = sizeof(task_exec_request_t) + filename_size + command_size;
  task_exec_request_t *request = page_malloc(bytes);
  if (!request)
    goto failed;
  change_page_task_id(task->tid, request, bytes);
  request->filename_size = filename_size;
  memcpy(request->text, filename, filename_size);
  memcpy(request->text + filename_size, line, command_size);
  task->line = (char *)request;
  task->alloc_size = malloc(sizeof(*task->alloc_size));
  if (!task->alloc_size)
    goto failed;
  task->alloced = 1;
  *task->alloc_size = 2 * 1024 * 1024;
  if (!task_prepare_input(task))
    goto failed;
  task_set_name(task, filename);
  task->TTY = current_task()->TTY;
  task->tty_session = current_task()->tty_session;
  return task;
failed:
  task_abort_creation(task);
  return NULL;
}

/* The kernel interprets only PT_INTERP. Dynamic sections, shared objects and
 * all relocations belong to the user program named by that segment. */
static bool executable_interpreter(int descriptor, char **interpreter) {
  vfs_context_t *context = current_task()->fs_context;
  vfs_stat_t status;
  Elf_Ehdr header;
  *interpreter = NULL;
  if (vfs_fd_stat(context, descriptor, &status) < 0 ||
      status.type != VFS_NODE_FILE || status.size < sizeof(header) ||
      status.size > INT_MAX ||
      vfs_fd_read(context, descriptor, &header, sizeof(header)) !=
          sizeof(header))
    return false;
  if (memcmp(header.e_ident, "\177ELF", 4) ||
      header.e_ident[EI_CLASS] != ELF_NATIVE_CLASS ||
      header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_ident[EI_VERSION] != EV_CURRENT ||
      header.e_version != EV_CURRENT ||
      header.e_machine != ELF_NATIVE_MACHINE ||
      (header.e_type != ET_EXEC && header.e_type != ET_DYN) ||
      header.e_ehsize != sizeof(header) || !header.e_phnum ||
      header.e_phentsize != sizeof(Elf_Phdr) || header.e_phoff > status.size ||
      header.e_phnum > (status.size - header.e_phoff) / sizeof(Elf_Phdr))
    return false;
  size_t size = header.e_phnum * sizeof(Elf_Phdr);
  Elf_Phdr *segments = page_malloc(size);
  if (!segments)
    return false;
  change_page_task_id(current_task()->tid, segments, size);
  bool valid = false;
  if (vfs_fd_seek(context, descriptor, header.e_phoff, 0) < 0 ||
      vfs_fd_read(context, descriptor, segments, size) != size)
    goto done;
  const Elf_Phdr *interp = NULL;
  for (size_t i = 0; i < header.e_phnum; i++) {
    const Elf_Phdr *p = segments + i;
    if (p->p_type != PT_INTERP)
      continue;
    if (interp || p->p_filesz < 2 || p->p_filesz > INT_MAX - 2 ||
        p->p_offset > status.size || p->p_filesz > status.size - p->p_offset)
      goto done;
    interp = p;
  }
  if (interp) {
    size_t path_size = interp->p_filesz + 2;
    char *path = page_malloc(path_size);
    if (!path)
      goto done;
    change_page_task_id(current_task()->tid, path, path_size);
    path[0] = default_drive;
    path[1] = ':';
    if (vfs_fd_seek(context, descriptor, interp->p_offset, 0) < 0 ||
        vfs_fd_read(context, descriptor, path + 2, interp->p_filesz) !=
            interp->p_filesz ||
        path[2] != '/' || path[path_size - 1] ||
        strlen(path) != path_size - 1) {
      page_free(path, path_size);
      goto done;
    }
    *interpreter = path;
  }
  valid = true;
done:
  page_free(segments, size);
  return valid;
}

static char *task_read_executable(int descriptor, int *size) {
  vfs_context_t *context = current_task()->fs_context;
  vfs_stat_t status;
  if (vfs_fd_stat(context, descriptor, &status) < 0 ||
      status.type != VFS_NODE_FILE || !status.size || status.size > INT_MAX)
    return NULL;
  char *image = page_malloc(status.size);
  if (!image)
    return NULL;
  change_page_task_id(current_task()->tid, image, status.size);
  if (vfs_fd_seek(context, descriptor, 0, 0) < 0 ||
      vfs_fd_read(context, descriptor, image, status.size) != status.size) {
    page_free(image, status.size);
    return NULL;
  }
  *size = status.size;
  return image;
}

void task_to_user_mode_elf(char *filename) {
  mtask *task = current_task();
  int descriptor = vfs_fd_open(task->fs_context, filename, VFS_OPEN_READ);
  int executable_size = 0;
  char *p = NULL;
  char *interpreter = NULL;
  if (descriptor < 0)
    goto failed;
  if (filename[0] && filename[1] == ':' &&
      vfs_context_change_drive(task->fs_context, filename[0]) < 0)
    goto failed;
  if (!executable_interpreter(descriptor, &interpreter))
    goto failed;
  bool dynamic = interpreter != NULL;
  int image_fd = descriptor;
  if (dynamic) {
    image_fd = vfs_fd_open(task->fs_context, interpreter, VFS_OPEN_READ);
  }
  if (image_fd >= 0)
    p = task_read_executable(image_fd, &executable_size);
  if (dynamic && image_fd >= 0)
    vfs_fd_close(task->fs_context, image_fd);
  if (!p)
    goto failed;
  if (dynamic && vfs_fd_seek(task->fs_context, descriptor, 0, 0) < 0)
    goto failed;
  uintptr_t user_eip;
  uintptr_t image_end;
  if (!arch_executable_validate(p, executable_size, &user_eip, &image_end) ||
      image_end > (uintptr_t)-1 - (PAGE_SIZE_BYTES - 1)) {
    goto failed;
  }
  uintptr_t alloc_addr =
      (image_end + PAGE_SIZE_BYTES - 1) & ~(uintptr_t)(PAGE_SIZE_BYTES - 1);
  size_t pg = size_div_round_up(*(task->alloc_size), PAGE_SIZE_BYTES);
  struct user_runtime_layout layout;
  if (!user_runtime_layout_calculate(alloc_addr, pg, USER_STACK_PAGES, user_eip,
                                     &layout) ||
      !task_map_user_pages(alloc_addr, layout.total_pages)) {
    goto failed;
  }
  task->alloc_addr = layout.allocation_base;
  if (!arch_executable_load(p, executable_size, &user_eip)) {
    goto failed;
  }
  page_free(p, executable_size);
  p = NULL;
  uintptr_t argument = 0;
  if (dynamic) {
    size_t path_size = strlen(filename) + 1;
    size_t interpreter_size = strlen(interpreter) + 1;
    size_t available = 512 * PAGE_SIZE_BYTES - sizeof(loader_start_t) - 64;
    if (path_size > available || interpreter_size > available - path_size)
      goto failed;
    layout.stack_top -= interpreter_size;
    char *interpreter_path = (char *)layout.stack_top;
    memcpy(interpreter_path, interpreter, interpreter_size);
    page_free(interpreter, interpreter_size);
    interpreter = NULL;
    layout.stack_top -= path_size;
    char *path = (char *)layout.stack_top;
    memcpy(path, filename, path_size);
    layout.stack_top =
        (layout.stack_top - sizeof(loader_start_t)) & ~(uintptr_t)15;
    loader_start_t *start = (loader_start_t *)layout.stack_top;
    *start =
        (loader_start_t){sizeof(*start), descriptor, path, interpreter_path};
    argument = (uintptr_t)start;
  } else {
    vfs_fd_close(task->fs_context, descriptor);
  }
  task->user_stack = (thread_region_t){alloc_addr,
                                       USER_STACK_PAGES * PAGE_SIZE_BYTES};
  task->user_mode = 1;
  arch_task_set_kernel_stack(task->top);

  kernel_lock_leave();
  arch_task_enter_user(user_eip, layout.stack_top, argument);
failed:
  if (interpreter)
    page_free(interpreter, strlen(interpreter) + 1);
  if (p)
    page_free(p, executable_size);
  if (descriptor >= 0)
    vfs_fd_close(task->fs_context, descriptor);
  task_exit(-1);
}
int os_execute(char *filename, char *line, execute_mode_t mode) {
  mtask *t = task_create_application(filename, line);
  if (!t)
    return -1;
  bool mouse_owned = mouse_use_task == current_task();
  t->ptid = current_task()->tgid;
  t->return_cwd = mode == EXECUTE_COMMAND;
  int old = current_task()->sigint_up;
  t->sigint_up = 1;
  struct tty *tty_backup = current_task()->TTY;
  int o = current_task()->fifosleep;
  irq_state_t state = irq_save();
  if (!task_publish(t)) {
    irq_restore(state);
    task_abort_creation(t);
    return -1;
  }
  current_task()->sigint_up = 0;
  current_task()->TTY = NULL;
  current_task()->fifosleep = 1;
  if (mouse_owned) {
    mouse_sleep();
  }
  irq_restore(state);

  unsigned status = waittid(t->tid);
  current_task()->fifosleep = o;

  current_task()->TTY = current_task()->tty_session == tty_backup
                            ? tty_backup
                            : current_task()->tty_session;
  if (mouse_owned && mouse_use_task == NULL) {
    mouse_ready();
  }
  current_task()->sigint_up = old;

  return status;
}
void os_execute_no_ret(char *filename, char *line) {
  mtask *t = task_create_application(filename, line);
  if (!t)
    return;
  t->ptid = 0; /* detached tasks are adopted by the idle reaper */
  if (!task_publish(t)) {
    task_abort_creation(t);
    return;
  }
  current_task()->TTY = NULL;
}
