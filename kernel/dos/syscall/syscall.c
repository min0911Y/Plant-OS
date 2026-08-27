#include <arch/x86/interrupt.h>
#include <calendar.h>
#include <cmd.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <user_space.h>

unsigned div_round_up(unsigned num, unsigned size);

unsigned custom_handler;
unsigned custom_handler_pde;
mtask *custom_handler_owner;

void page_set_physics_attr_pde(uint32_t vaddr, void *paddr, uint32_t attr,
                               unsigned pde_backup);

static void keyboard_press(uint8_t data, uint32_t tid) {
  fifo8_put(get_task(tid)->Pkeyfifo, data);
}

static void keyboard_release(uint8_t data, uint32_t tid) {
  fifo8_put(get_task(tid)->Ukeyfifo, data);
}

static void user_thread_entry(void) {
  while (!current_task()->line) {
  }

  unsigned *request = (unsigned *)current_task()->line;
  unsigned esp = request[0];
  unsigned eip = request[1];
  page_free_one(request);

  struct FIFO8 *key_fifo = page_malloc_one();
  struct FIFO8 *mouse_fifo = page_malloc_one();
  unsigned char *key_buffer = page_malloc_one();
  unsigned char *mouse_buffer = page_malloc_one();
  fifo8_init(key_fifo, 4096, key_buffer);
  fifo8_init(mouse_fifo, 4096, mouse_buffer);
  task_set_fifo(current_task(), key_fifo, mouse_fifo);
  task_to_user_mode(eip, esp);

  for (;;) {
  }
}

static int user_range_ok(uint32_t addr, uint32_t size) {
  if (addr < USER_SPACE_START || addr > USER_HEAP_END) {
    return 0;
  }
  return size <= USER_HEAP_END - addr;
}

static char *copy_user_string(uint32_t addr, size_t *length_out) {
  if (!user_range_ok(addr, 1)) {
    return NULL;
  }

  const char *source = (const char *)(uintptr_t)addr;
  const size_t available = USER_HEAP_END - addr;
  size_t length = 0;
  while (length < available && source[length] != '\0') {
    length++;
  }
  if (length == available || length >= INT_MAX) {
    return NULL;
  }

  char *copy = malloc(length + 1);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, source, length);
  copy[length] = '\0';
  *length_out = length;
  return copy;
}

enum ipc_syscall_id {
  IPC_SYSCALL_SEND = 0x01,
  IPC_SYSCALL_RECEIVE = 0x02,
  IPC_SYSCALL_PEEK = 0x03,
  IPC_SYSCALL_PENDING = 0x04,
  IPC_SYSCALL_REGISTER = 0x05,
  IPC_SYSCALL_UNREGISTER = 0x06,
  IPC_SYSCALL_LOOKUP = 0x07,
  IPC_SYSCALL_GENERATION = 0x08,
  IPC_SYSCALL_COUNT,
};

typedef int (*ipc_syscall_handler_t)(uint32_t arg1, uint32_t arg2);

static int ipc_syscall_send(uint32_t arg1, uint32_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, sizeof(ipc_user_msg_t))) {
    return IPC_ERR_INVAL;
  }

  ipc_user_msg_t *message = (ipc_user_msg_t *)(uintptr_t)arg1;
  if (message->size &&
      !user_range_ok((uint32_t)(uintptr_t)message->data, message->size)) {
    return IPC_ERR_INVAL;
  }
  return ipc_send(message->peer_tid, message->peer_generation, message->type,
                  message->id, message->data, message->size, message->flags,
                  message->timeout_ms);
}

static int ipc_syscall_receive(uint32_t arg1, uint32_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, sizeof(ipc_user_msg_t))) {
    return IPC_ERR_INVAL;
  }

  ipc_user_msg_t *message = (ipc_user_msg_t *)(uintptr_t)arg1;
  if (message->size &&
      !user_range_ok((uint32_t)(uintptr_t)message->data, message->size)) {
    return IPC_ERR_INVAL;
  }

  ipc_msg_info_t info;
  int result = ipc_recv(message->data, message->size, &info,
                        message->from_filter, message->flags,
                        message->timeout_ms);
  if (result >= 0 || result == IPC_ERR_TOOBIG) {
    message->peer_tid = info.from_tid;
    message->peer_generation = info.from_generation;
    message->type = info.type;
    message->id = info.id;
    message->size = info.size;
  }
  return result;
}

static int ipc_syscall_peek(uint32_t arg1, uint32_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, sizeof(ipc_user_msg_t))) {
    return IPC_ERR_INVAL;
  }

  ipc_user_msg_t *message = (ipc_user_msg_t *)(uintptr_t)arg1;
  ipc_msg_info_t info;
  int result = ipc_peek(&info, message->from_filter);
  if (result == IPC_OK) {
    message->peer_tid = info.from_tid;
    message->peer_generation = info.from_generation;
    message->type = info.type;
    message->id = info.id;
    message->size = info.size;
  }
  return result;
}

static int ipc_syscall_pending(uint32_t arg1, uint32_t arg2) {
  (void)arg1;
  (void)arg2;
  return ipc_pending();
}

static int ipc_syscall_register(uint32_t arg1, uint32_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, IPC_NAME_MAX)) {
    return IPC_ERR_INVAL;
  }
  return ipc_service_register((const char *)(uintptr_t)arg1);
}

static int ipc_syscall_unregister(uint32_t arg1, uint32_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, IPC_NAME_MAX)) {
    return IPC_ERR_INVAL;
  }
  return ipc_service_unregister((const char *)(uintptr_t)arg1);
}

static int ipc_syscall_lookup(uint32_t arg1, uint32_t arg2) {
  if (!user_range_ok(arg1, IPC_NAME_MAX) ||
      (arg2 && !user_range_ok(arg2, sizeof(uint32_t)))) {
    return IPC_ERR_INVAL;
  }

  uint32_t generation;
  int tid = ipc_service_lookup((const char *)(uintptr_t)arg1, &generation);
  if (tid >= 0 && arg2) {
    *(uint32_t *)(uintptr_t)arg2 = generation;
  }
  return tid;
}

static int ipc_syscall_generation(uint32_t arg1, uint32_t arg2) {
  (void)arg1;
  (void)arg2;
  return (int)current_task()->generation;
}

static const ipc_syscall_handler_t ipc_syscall_handlers[IPC_SYSCALL_COUNT] = {
    [IPC_SYSCALL_SEND] = ipc_syscall_send,
    [IPC_SYSCALL_RECEIVE] = ipc_syscall_receive,
    [IPC_SYSCALL_PEEK] = ipc_syscall_peek,
    [IPC_SYSCALL_PENDING] = ipc_syscall_pending,
    [IPC_SYSCALL_REGISTER] = ipc_syscall_register,
    [IPC_SYSCALL_UNREGISTER] = ipc_syscall_unregister,
    [IPC_SYSCALL_LOOKUP] = ipc_syscall_lookup,
    [IPC_SYSCALL_GENERATION] = ipc_syscall_generation,
};

static int ipc_syscall_dispatch(uint32_t id, uint32_t arg1, uint32_t arg2) {
  if (id >= IPC_SYSCALL_COUNT || ipc_syscall_handlers[id] == NULL) {
    return IPC_ERR_INVAL;
  }
  return ipc_syscall_handlers[id](arg1, arg2);
}

enum syscall_id {
  SYSCALL_VERSION = 0x01,
  SYSCALL_PRINT_CHARACTER = 0x02,
  SYSCALL_LEGACY_GRAPHICS = 0x03,
  SYSCALL_SET_CURSOR = 0x04,
  SYSCALL_PRINT_STRING = 0x05,
  SYSCALL_SLEEP = 0x06,
  SYSCALL_HEAP_ADDRESS = 0x08,
  SYSCALL_HEAP_SIZE = 0x09,
  SYSCALL_TEXT_BOX = 0x0c,
  SYSCALL_BEEP = 0x0d,
  SYSCALL_CURSOR_POSITION = 0x0e,
  SYSCALL_MOUSE_EVENT = 0x0f,
  SYSCALL_MOUSE_SUPPORTED = 0x10,
  SYSCALL_INPUT = 0x16,
  SYSCALL_RUN_COMMAND = 0x19,
  SYSCALL_FILE_OPERATION = 0x1a,
  SYSCALL_COMMAND_LINE = 0x1b,
  SYSCALL_COPY = 0x1c,
  SYSCALL_KEYBOARD_HIT = 0x1d,
  SYSCALL_EXIT = 0x1e,
  SYSCALL_VBE_CONTROL = 0x20,
  SYSCALL_BIOS_VIDEO = 0x21,
  SYSCALL_TASK_CONTROL = 0x22,
  SYSCALL_TTY_COLOR = 0x23,
  SYSCALL_TIMER_CONTROL = 0x24,
  SYSCALL_FORMAT = 0x25,
  SYSCALL_RTC = 0x26,
  SYSCALL_DRAW_PIXEL = 0x27,
  SYSCALL_READ_PIXEL = 0x28,
  SYSCALL_COPY_FRAMEBUFFER = 0x29,
  SYSCALL_DRAW_BUFFER = 0x2a,
  SYSCALL_SCROLL_FRAMEBUFFER = 0x2b,
  SYSCALL_DRAW_BOX = 0x2c,
  SYSCALL_TIMESTAMP = 0x2d,
  SYSCALL_UPTIME = 0x2e,
  SYSCALL_RESET_FPU = 0x2f,
  SYSCALL_KEYBOARD_SETUP = 0x30,
  SYSCALL_KEY_PRESS_PENDING = 0x31,
  SYSCALL_KEY_RELEASE_PENDING = 0x32,
  SYSCALL_KEY_PRESS_READ = 0x33,
  SYSCALL_KEY_RELEASE_READ = 0x34,
  SYSCALL_GROW_HEAP = 0x35,
  SYSCALL_READ_ENV = 0x36,
  SYSCALL_PATH_WITHOUT_DRIVE = 0x37,
  SYSCALL_CURRENT_DRIVE = 0x38,
  SYSCALL_EXECUTE = 0x39,
  SYSCALL_CLEAR = 0x3a,
  SYSCALL_MOUNT_CHECK = 0x3b,
  SYSCALL_MOUNT = 0x3c,
  SYSCALL_CHANGE_DISK = 0x3d,
  SYSCALL_MEMORY_SIZE = 0x3e,
  SYSCALL_USED_PAGES = 0x3f,
  SYSCALL_DELETE_FILE = 0x40,
  SYSCALL_CHANGE_PATH = 0x41,
  SYSCALL_CURSOR_START = 0x42,
  SYSCALL_CURSOR_STOP = 0x43,
  SYSCALL_UNMOUNT = 0x44,
  SYSCALL_TTY_WIDTH = 0x45,
  SYSCALL_TTY_HEIGHT = 0x46,
  SYSCALL_RENAME = 0x47,
  SYSCALL_LOG = 0x48,
  SYSCALL_SIGNAL_HANDLER = 0x49,
  SYSCALL_FORK = 0x4a,
  SYSCALL_WAIT = 0x4b,
  SYSCALL_SET_RT = 0x4c,
  SYSCALL_MOUSE_ENABLE = 0x4d,
  SYSCALL_MOUSE_PENDING = 0x4e,
  SYSCALL_MOUSE_READ = 0x4f,
  SYSCALL_YIELD = 0x50,
  SYSCALL_TTY_ALLOC = 0x51,
  SYSCALL_TTY_SET = 0x52,
  SYSCALL_TTY_FREE = 0x53,
  SYSCALL_RETURN_TO_APP = 0x54,
  SYSCALL_USE_KEYBOARD = 0x55,
  SYSCALL_CUSTOM_HANDLER = 0x56,
  SYSCALL_MAP_MEMORY = 0x57,
  SYSCALL_TASK_LEVEL_HIGH = 0x58,
  SYSCALL_TASK_LEVEL_NORMAL = 0x59,
  SYSCALL_MODULE_LOAD = 0x5a,
  SYSCALL_MODULE_UNLOAD = 0x5b,
  SYSCALL_MODULE_LIST = 0x5c,
  SYSCALL_IPC = 0x5d,
  SYSCALL_NETWORK = 0x5e,
  SYSCALL_COUNT,
};

typedef void (*syscall_handler_t)(x86_interrupt_frame_t *frame);

static void syscall_version(x86_interrupt_frame_t *frame) {
  frame->edx = 0x302e3762;
}

static void syscall_print_character(x86_interrupt_frame_t *frame) {
  printchar(frame->edx & 0xff);
}

static void syscall_legacy_graphics(x86_interrupt_frame_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }

  switch (frame->ebx) {
  case 0x01:
    SwitchToText8025();
    break;
  case 0x02:
    SwitchTo320X200X256();
    break;
  case 0x03:
    Draw_Char(frame->ecx, frame->edx, frame->esi, frame->edi);
    break;
  case 0x04:
    PrintChineseChar(frame->ecx, frame->edx, frame->edi, frame->esi);
    break;
  case 0x05:
    Draw_Box(frame->ecx, frame->edx, frame->esi, frame->edi, frame->ebp);
    break;
  case 0x06:
    Draw_Px(frame->ecx, frame->edx, frame->esi);
    break;
  case 0x07:
    Draw_Str(frame->ecx, frame->edx, (char *)(uintptr_t)frame->esi,
             frame->edi);
    break;
  case 0x08:
    PrintChineseStr(frame->ecx, frame->edx, frame->edi,
                    (unsigned char *)(uintptr_t)frame->esi);
    break;
  }
}

static void syscall_set_cursor(x86_interrupt_frame_t *frame) {
  gotoxy(frame->edx, frame->ecx);
}

static void syscall_print_string(x86_interrupt_frame_t *frame) {
  print((char *)(uintptr_t)frame->edx);
}

static void syscall_sleep(x86_interrupt_frame_t *frame) {
  sleep(frame->edx);
}

static void syscall_heap_info(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  if (frame->eax == SYSCALL_HEAP_ADDRESS) {
    frame->edx = task->alloc_addr;
  } else {
    frame->edx = task->alloc_size ? *task->alloc_size : 0;
  }
}

static void syscall_text_box(x86_interrupt_frame_t *frame) {
  Text_Draw_Box(frame->ecx, frame->ebx, frame->esi, frame->edx,
                (unsigned char)frame->edi);
}

static void syscall_beep(x86_interrupt_frame_t *frame) {
  beep(frame->ebx, frame->ecx, frame->edx);
}

static void syscall_cursor_position(x86_interrupt_frame_t *frame) {
  frame->ecx = get_y();
  frame->edx = get_x();
}

static void syscall_mouse_event(x86_interrupt_frame_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }

  mtask *task = current_task();
  int old_mouse_x = task->mx;
  int old_mouse_y = task->my;
  int buffer_x = task->mx * 8;
  int buffer_y = task->my * 16;
  int background = *(char *)(task->TTY->vram + old_mouse_y *
                                                    task->TTY->xsize * 2 +
                            old_mouse_x * 2 + 1);
  if (mdec.sleep == 1) {
    mouse_ready(&mdec);
  }

  for (;;) {
    if (fifo8_status(task_get_mouse_fifo(task)) == 0) {
      task_next();
      signal_deal();
      continue;
    }

    int data = fifo8_get(task_get_mouse_fifo(task));
    if (mouse_decode(&mdec, data) == 0) {
      continue;
    }
    if (task->TTY != now_tty() && task->TTY->using1 == 1) {
      continue;
    }
    if (mdec.roll != MOUSE_ROLL_NONE) {
      frame->ecx = task->mx;
      frame->edx = task->my;
      frame->esi = 3 + mdec.roll;
      *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                task->mx * 2 + 1) = background;
      task->mx = old_mouse_x;
      task->my = old_mouse_y;
      return;
    }

    old_mouse_x = task->mx;
    old_mouse_y = task->my;
    buffer_x += mdec.x;
    buffer_y += mdec.y;
    if (buffer_x > (task->TTY->xsize - 1) * 8) {
      buffer_x = (task->TTY->xsize - 1) * 8;
    } else if (buffer_x < 0) {
      buffer_x = 0;
    }
    if (buffer_y > (task->TTY->ysize - 1) * 16) {
      buffer_y = (task->TTY->ysize - 1) * 16;
    } else if (buffer_y < 0) {
      buffer_y = 0;
    }

    task->mx = buffer_x / 8;
    task->my = buffer_y / 16;
    *(char *)(task->TTY->vram + old_mouse_y * task->TTY->xsize * 2 +
              old_mouse_x * 2 + 1) = background;
    background = *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                           task->mx * 2 + 1);
    *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
              task->mx * 2 + 1) = ~background;

    if (mdec.btn & 0x01) {
      frame->esi = 1;
    } else if (mdec.btn & 0x02) {
      frame->esi = 2;
    } else if (mdec.btn & 0x04) {
      frame->esi = 3;
    } else {
      continue;
    }
    frame->ecx = task->mx;
    frame->edx = task->my;
    break;
  }

  *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 +
            1) = background;
  task->mx = old_mouse_x;
  task->my = old_mouse_y;
}

static void syscall_mouse_supported(x86_interrupt_frame_t *frame) {
  extern mtask *mouse_use_task;
  frame->eax = running_mode == POWERINTDOS && mouse_use_task == NULL;
}

static void syscall_input(x86_interrupt_frame_t *frame) {
  switch (frame->ebx) {
  case 0x01:
    frame->edx = getch();
    break;
  case 0x02:
    frame->edx = input_char_inSM();
    break;
  case 0x03:
    input((char *)(uintptr_t)frame->edx, frame->ecx);
    break;
  }
}

static void syscall_run_shell_command(x86_interrupt_frame_t *frame) {
  size_t command_length;
  char *command = copy_user_string(frame->edx, &command_length);
  if (command == NULL) {
    frame->eax = -1;
    return;
  }

  frame->eax = run_shell_command(command, command_length);
  free(command);
}

enum {
  LIST_DIRECTORY_ERROR = -1,
  LIST_DIRECTORY_RETRY = -2,
};

static void syscall_file_operation(x86_interrupt_frame_t *frame) {
  switch (frame->ebx) {
  case 0x01: {
    size_t length;
    char *path = copy_user_string(frame->edx, &length);
    frame->edx = path == NULL ? (uint32_t)-1 : vfs_filesize(path);
    free(path);
    break;
  }
  case 0x02: {
    size_t length;
    char *path = copy_user_string(frame->edx, &length);
    int file_size = path == NULL ? -1 : (int)vfs_filesize(path);
    if (file_size < 0 ||
        (file_size != 0 && !user_range_ok(frame->esi, file_size))) {
      frame->eax = 0;
    } else {
      frame->eax = vfs_readfile(path, (char *)(uintptr_t)frame->esi);
    }
    free(path);
    break;
  }
  case 0x03: {
    size_t length;
    char *path = copy_user_string(frame->edx, &length);
    frame->eax = path != NULL && vfs_createfile(path);
    free(path);
    break;
  }
  case 0x04: {
    size_t length;
    char *path = copy_user_string(frame->edx, &length);
    frame->eax = path != NULL && vfs_createdict(path);
    free(path);
    break;
  }
  case 0x05: {
    size_t length;
    char *path = copy_user_string(frame->edx, &length);
    frame->eax = path != NULL && frame->ecx <= INT_MAX &&
                 (frame->ecx == 0 || user_range_ok(frame->esi, frame->ecx)) &&
                 EDIT_FILE(path, (char *)(uintptr_t)frame->esi, frame->ecx,
                           frame->edi);
    free(path);
    break;
  }
  case 0x06: {
    size_t path_length;
    char *path = copy_user_string(frame->edx, &path_length);
    if (path == NULL || frame->esi > INT_MAX / sizeof(vfs_file) ||
        (frame->ecx == 0 && frame->esi != 0) ||
        (frame->ecx != 0 &&
         !user_range_ok(frame->ecx, frame->esi * sizeof(vfs_file)))) {
      free(path);
      frame->eax = LIST_DIRECTORY_ERROR;
      break;
    }

    struct List *file_list = vfs_listfile(path);
    free(path);
    if (file_list == NULL) {
      frame->eax = LIST_DIRECTORY_ERROR;
      break;
    }

    size_t count = file_list->ctl->all;
    if (count > INT_MAX) {
      frame->eax = LIST_DIRECTORY_ERROR;
    } else if (frame->ecx == 0) {
      frame->eax = count;
    } else if (frame->esi < count) {
      frame->eax = LIST_DIRECTORY_RETRY;
    } else {
      vfs_file *files = (vfs_file *)(uintptr_t)frame->ecx;
      frame->eax = count;
      for (size_t i = 0; i < count; i++) {
        struct List *entry = FindForCount(i + 1, file_list);
        if (entry == NULL || entry->val == 0) {
          frame->eax = LIST_DIRECTORY_ERROR;
          break;
        }
        memcpy(&files[i], (void *)(uintptr_t)entry->val, sizeof(vfs_file));
      }
    }
    for (size_t i = 1; i <= count; i++) {
      struct List *entry = FindForCount(i, file_list);
      if (entry != NULL) {
        free((void *)(uintptr_t)entry->val);
      }
    }
    DeleteList(file_list);
    break;
  }
  }
}

static void syscall_command_line(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  if (task->line == NULL) {
    frame->eax = -1;
    return;
  }
  size_t length = strlen((const char *)task->line);
  if (length >= INT_MAX || (frame->edx == 0 && frame->ecx != 0) ||
      (frame->edx != 0 &&
       !user_range_ok(frame->edx, frame->ecx))) {
    frame->eax = -1;
    return;
  }
  if (frame->edx == 0) {
    frame->eax = length;
    return;
  }
  if (frame->ecx <= length) {
    frame->eax = -2;
    return;
  }
  memcpy((void *)(uintptr_t)frame->edx, (const void *)task->line, length + 1);
  frame->eax = length;
}

static void syscall_copy(x86_interrupt_frame_t *frame) {
  size_t source_length;
  size_t destination_length;
  char *source = copy_user_string(frame->edx, &source_length);
  char *destination = copy_user_string(frame->esi, &destination_length);
  frame->eax = source == NULL || destination == NULL
                   ? -1
                   : Copy(source, destination);
  free(source);
  free(destination);
}

static void syscall_keyboard_hit(x86_interrupt_frame_t *frame) {
  frame->eax = kbhit();
}

static void syscall_exit(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  unsigned status = frame->ebx;
  if (!*(unsigned char *)USER_HEAP_END) {
    extern mtask *mouse_use_task;
    if (mouse_use_task == task) {
      mouse_sleep(&mdec);
    }
  } else {
    mtask *parent = task->ptid == 0 || task->ptid == (uint32_t)-1
                        ? NULL
                        : get_task(task->ptid);
    if (parent && parent->kind == TASK_PROCESS && parent->state != DIED) {
      if (!vfs_clone_for_task(task, parent)) {
        WARNING_K("failed to transfer child VFS state");
        status = (unsigned)-1;
      }
    }
  }
  task_exit(status);
  for (;;) {
  }
}

static void syscall_vbe_control(x86_interrupt_frame_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }

  if (frame->ebx == 0x01) {
    frame->eax = SwitchVBEMode(frame->ecx);
  } else if (frame->ebx == 0x02) {
    frame->eax = check_vbe_mode(frame->ecx, (struct VBEINFO *)VBEINFO_ADDRESS);
  } else if (frame->ebx == 0x05) {
    unsigned framebuffer = set_mode(frame->ecx, frame->edx, 32);
    frame->eax = framebuffer;
    unsigned count = div_round_up(frame->ecx * frame->edx * 4, 0x1000);
    for (unsigned i = 0; i < count; i++) {
      unsigned address = framebuffer + i * 0x1000;
      page_set_physics_attr(address, (void *)(uintptr_t)address,
                            PG_P | PG_USU | PG_RWW | PG_SHARED);
    }
  }
}

static void syscall_bios_video(x86_interrupt_frame_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }
  if (frame->ebx == 0x01) {
    SwitchToText8025_BIOS();
    clear();
  } else if (frame->ebx == 0x02) {
    SwitchTo320X200X256_BIOS();
  }
}

static void syscall_task_control(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  switch (frame->ebx) {
  case 0x04:
    send_ipc_message(frame->ecx, (void *)(uintptr_t)frame->edx, frame->esi,
                     asynchronous);
    break;
  case 0x05:
    get_ipc_message((void *)(uintptr_t)frame->edx, frame->ecx);
    break;
  case 0x06:
    frame->eax = ipc_message_len(frame->ecx);
    break;
  case 0x07:
    frame->eax = get_tid(task);
    break;
  case 0x08:
    frame->eax = have_msg();
    break;
  case 0x09:
    get_msg_all((void *)(uintptr_t)frame->edx);
    break;
  case 0x0a: {
    mtask *thread = create_thread_task((uintptr_t)user_thread_entry, 0, 1, 1);
    if (thread == NULL) {
      frame->eax = -1;
      return;
    }
    thread->alloc_addr = task->alloc_addr;
    thread->alloc_size = task->alloc_size;
    thread->TTY = task->TTY;
    thread->ptid = task->ptid;
    thread->tgid = task->tgid;
    thread->kind = TASK_THREAD;
    thread->mx = 0;
    thread->my = 0;
    unsigned *request = page_malloc_one_no_mark();
    if (request == NULL) {
      task_abort_creation(thread);
      frame->eax = -1;
      return;
    }
    request[0] = frame->esi;
    request[1] = frame->edx;
    thread->line = (char *)request;
    if (!task_publish(thread)) {
      task_abort_creation(thread);
      page_free_one(request);
      frame->eax = -1;
      return;
    }
    frame->eax = thread->tid;
    break;
  }
  case 0x0b:
    task_lock();
    break;
  case 0x0c:
    task_unlock();
    break;
  case 0x0d: {
    mtask *target = get_task(frame->ecx);
    if (target && target->kind == TASK_THREAD && target->tgid == task->tgid) {
      task_kill(frame->ecx);
    }
    break;
  }
  }
}

static void syscall_tty_color(x86_interrupt_frame_t *frame) {
  if (frame->ebx == 0x01) {
    frame->eax = current_task()->TTY->color;
  } else if (frame->ebx == 0x02) {
    current_task()->TTY->color = frame->ecx;
  }
}

static void syscall_timer_control(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  frame->eax = -1;
  switch (frame->ebx) {
  case 0x00: {
    if (task->timer != NULL) {
      break;
    }
    struct TIMER *timer = timer_alloc();
    struct FIFO8 *fifo = page_malloc(sizeof(struct FIFO8));
    unsigned char *buffer = page_malloc(50 * sizeof(unsigned char));
    if (timer == NULL || fifo == NULL || buffer == NULL) {
      if (buffer != NULL) {
        page_free(buffer, 50 * sizeof(unsigned char));
      }
      if (fifo != NULL) {
        page_free(fifo, sizeof(struct FIFO8));
      }
      timer_free(timer);
      break;
    }
    fifo8_init(fifo, 50, buffer);
    timer_init(timer, fifo, 1);
    timer->waiter = task;
    task->timer = timer;
    frame->eax = 0;
    break;
  }
  case 0x01:
    if (task->timer == NULL) {
      break;
    }
    timer_settime(task->timer, frame->ecx);
    frame->eax = 0;
    break;
  case 0x02:
    if (task->timer == NULL || task->timer->fifo == NULL) {
      break;
    }
    frame->eax = fifo8_status(task->timer->fifo) != 0 &&
                 fifo8_get(task->timer->fifo) == 1;
    break;
  case 0x03:
    if (task->timer == NULL) {
      break;
    }
    timer_cancel(task->timer);
    page_free(task->timer->fifo->buf, 50 * sizeof(unsigned char));
    page_free(task->timer->fifo, sizeof(struct FIFO8));
    timer_free(task->timer);
    task->timer = NULL;
    frame->eax = 0;
    break;
  }
}

static void syscall_format(x86_interrupt_frame_t *frame) {
  frame->eax = vfs_format(frame->ebx, (char *)(uintptr_t)frame->ecx);
}

static void syscall_rtc(x86_interrupt_frame_t *frame) {
  switch (frame->ebx) {
  case 0x00:
    frame->eax = get_hour_hex();
    break;
  case 0x01:
    frame->eax = get_min_hex();
    break;
  case 0x02:
    frame->eax = get_sec_hex();
    break;
  case 0x03:
    frame->eax = get_day_of_month();
    break;
  case 0x04:
    frame->eax = get_day_of_week();
    break;
  case 0x05:
    frame->eax = get_mon_hex();
    break;
  case 0x06:
    frame->eax = get_year();
    break;
  }
}

static void syscall_framebuffer(x86_interrupt_frame_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }

  struct VBEINFO *vbe = (struct VBEINFO *)VBEINFO_ADDRESS;
  vram_t *framebuffer = (vram_t *)vbe->vram;
  switch (frame->eax) {
  case SYSCALL_DRAW_PIXEL:
    SDraw_Px(framebuffer, frame->ebx, frame->ecx, frame->edx, vbe->xsize);
    break;
  case SYSCALL_READ_PIXEL:
    frame->eax = framebuffer[frame->ebx * vbe->xsize + frame->ecx];
    break;
  case SYSCALL_COPY_FRAMEBUFFER:
    memcpy((void *)(uintptr_t)frame->ebx, framebuffer,
           vbe->xsize * vbe->ysize * sizeof(vram_t));
    break;
  case SYSCALL_DRAW_BUFFER: {
    int x = frame->ebx;
    int y = frame->ecx;
    int width = frame->edx;
    int height = frame->esi;
    unsigned *buffer = (unsigned *)(uintptr_t)frame->edi;
    for (int i = x; i < x + width; i++) {
      for (int j = y; j < y + height; j++) {
        framebuffer[j * vbe->xsize + i] =
            buffer[(j - y) * width + (i - x)];
      }
    }
    break;
  }
  case SYSCALL_SCROLL_FRAMEBUFFER: {
    int destination_row = 0;
    int source_row = frame->ebx;
    for (; source_row < vbe->ysize; source_row++, destination_row++) {
      for (int x = 0; x < vbe->xsize; x++) {
        framebuffer[destination_row * vbe->xsize + x] =
            framebuffer[source_row * vbe->xsize + x];
      }
    }
    SDraw_Box(framebuffer, 0, destination_row, vbe->xsize, vbe->ysize, 0,
              vbe->xsize);
    break;
  }
  case SYSCALL_DRAW_BOX:
    SDraw_Box(framebuffer, frame->ebx, frame->ecx, frame->edx, frame->esi,
              frame->edi, vbe->xsize);
    break;
  }
}

static void syscall_timestamp(x86_interrupt_frame_t *frame) {
  frame->eax = calendar_to_unix_timestamp(
      get_year(), get_mon_hex(), get_day_of_month(), get_hour_hex(),
      get_min_hex(), get_sec_hex());
}

static void syscall_uptime(x86_interrupt_frame_t *frame) {
  frame->eax = timerctl.count * 10;
}

static void syscall_reset_fpu(x86_interrupt_frame_t *frame) {
  (void)frame;
  current_task()->fpu_flag = 0;
}

static void syscall_keyboard_setup(x86_interrupt_frame_t *frame) {
  (void)frame;
  mtask *task = current_task();
  task->Pkeyfifo = malloc(sizeof(struct FIFO8));
  task->Ukeyfifo = malloc(sizeof(struct FIFO8));
  unsigned char *press_buffer = page_malloc(4096);
  unsigned char *release_buffer = page_malloc(4096);
  fifo8_init(task->Pkeyfifo, 4096, press_buffer);
  fifo8_init(task->Ukeyfifo, 4096, release_buffer);
  task->keyboard_press = keyboard_press;
  task->keyboard_release = keyboard_release;
}

static void syscall_keyboard_fifo(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  switch (frame->eax) {
  case SYSCALL_KEY_PRESS_PENDING:
    frame->eax = fifo8_status(task->Pkeyfifo);
    break;
  case SYSCALL_KEY_RELEASE_PENDING:
    frame->eax = fifo8_status(task->Ukeyfifo);
    break;
  case SYSCALL_KEY_PRESS_READ:
    frame->eax = fifo8_get(task->Pkeyfifo);
    break;
  case SYSCALL_KEY_RELEASE_READ:
    frame->eax = fifo8_get(task->Ukeyfifo);
    break;
  }
}

static void syscall_grow_heap(x86_interrupt_frame_t *frame) {
  mtask *task = current_task();
  uint32_t old_size = task->alloc_size ? *task->alloc_size : 0;
  uint32_t start_addr = task->alloc_addr + old_size;
  uint32_t request = (frame->ebx + 0xfffu) & 0xfffff000u;
  if (task->alloc_size == NULL || (int32_t)frame->ebx <= 0 ||
      request < frame->ebx || start_addr < task->alloc_addr ||
      start_addr >= USER_HEAP_END || request > USER_HEAP_END - start_addr) {
    frame->eax = -1;
    return;
  }

  for (uint32_t offset = 0; offset < request; offset += 0x1000) {
    if (!page_link(start_addr + offset)) {
      frame->eax = -1;
      return;
    }
  }
  *task->alloc_size = old_size + request;
  frame->eax = 0;
}

static void syscall_read_env(x86_interrupt_frame_t *frame) {
  char *value = env_read((char *)(uintptr_t)frame->ebx);
  if (value) {
    strcpy((char *)(uintptr_t)frame->ecx, value);
    frame->eax = 1;
  } else {
    frame->eax = 0;
  }
}

static void syscall_path_without_drive(x86_interrupt_frame_t *frame) {
  vfs_getPath_no_drive((char *)(uintptr_t)frame->ebx);
}

static void syscall_current_drive(x86_interrupt_frame_t *frame) {
  frame->eax = current_task()->nfs->drive;
}

static void syscall_execute(x86_interrupt_frame_t *frame) {
  frame->eax = os_execute((char *)(uintptr_t)frame->ebx,
                          (char *)(uintptr_t)frame->ecx);
}

static void syscall_clear(x86_interrupt_frame_t *frame) {
  (void)frame;
  clear();
}

static void syscall_mount_operation(x86_interrupt_frame_t *frame) {
  switch (frame->eax) {
  case SYSCALL_MOUNT_CHECK:
    frame->eax = vfs_check_mount(frame->ebx);
    break;
  case SYSCALL_MOUNT:
    frame->eax = vfs_mount_disk(frame->ebx, frame->ecx);
    break;
  case SYSCALL_CHANGE_DISK:
    frame->eax = vfs_change_disk(frame->ebx);
    break;
  case SYSCALL_UNMOUNT:
    frame->eax = vfs_unmount_disk(frame->ebx);
    break;
  }
}

static void syscall_memory_info(x86_interrupt_frame_t *frame) {
  if (frame->eax == SYSCALL_MEMORY_SIZE) {
    frame->eax = memsize;
    return;
  }

  frame->eax = page_used_count(memsize);
}

static void syscall_delete_file(x86_interrupt_frame_t *frame) {
  frame->eax = vfs_delfile((char *)(uintptr_t)frame->edx);
}

static void syscall_change_path(x86_interrupt_frame_t *frame) {
  frame->eax = vfs_change_path((char *)(uintptr_t)frame->edx);
}

static void syscall_tty_cursor(x86_interrupt_frame_t *frame) {
  if (frame->eax == SYSCALL_CURSOR_START) {
    tty_start_curor_moving(current_task()->TTY);
  } else {
    tty_stop_cursor_moving(current_task()->TTY);
  }
}

static void syscall_tty_size(x86_interrupt_frame_t *frame) {
  if (frame->eax == SYSCALL_TTY_WIDTH) {
    frame->eax = current_task()->TTY->xsize;
  } else {
    frame->eax = current_task()->TTY->ysize;
  }
}

static void syscall_rename(x86_interrupt_frame_t *frame) {
  vfs_renamefile((char *)(uintptr_t)frame->ebx,
                 (char *)(uintptr_t)frame->ecx);
}

static void syscall_log(x86_interrupt_frame_t *frame) {
  logk((char *)(uintptr_t)frame->ebx);
}

static void syscall_signal_handler(x86_interrupt_frame_t *frame) {
  if (frame->ebx >= sizeof(current_task()->handler) /
                        sizeof(current_task()->handler[0])) {
    frame->eax = -1;
    return;
  }
  unsigned old_handler = current_task()->handler[frame->ebx];
  set_signal_handler(frame->ebx, frame->ecx);
  frame->eax = old_handler;
}

static void syscall_fork(x86_interrupt_frame_t *frame) {
  frame->eax = task_fork();
}

static void syscall_wait(x86_interrupt_frame_t *frame) {
  frame->eax = waittid(frame->ebx);
}

static void syscall_set_rt(x86_interrupt_frame_t *frame) {
  extern unsigned m_eip, m_cr3;
  m_eip = frame->ebx;
  m_cr3 = current_task()->pde;
}

static void syscall_mouse_enable(x86_interrupt_frame_t *frame) {
  (void)frame;
  mouse_ready(&mdec);
}

static void syscall_mouse_data(x86_interrupt_frame_t *frame) {
  if (frame->eax == SYSCALL_MOUSE_PENDING) {
    frame->eax = fifo8_status(task_get_mouse_fifo(current_task()));
  } else {
    frame->eax = fifo8_get(task_get_mouse_fifo(current_task()));
  }
}

static void syscall_yield(x86_interrupt_frame_t *frame) {
  (void)frame;
  irq_state_t state = irq_save();
  if (current_task()->ready == 0) {
    task_next();
  } else {
    current_task()->ready = 0;
  }
  irq_restore(state);
}

static void syscall_tty_object(x86_interrupt_frame_t *frame) {
  switch (frame->eax) {
  case SYSCALL_TTY_ALLOC:
    frame->eax = (uintptr_t)fartty_alloc(
        (void *)(uintptr_t)frame->ebx, frame->ecx, current_task()->pde,
        frame->edx, frame->esi);
    break;
  case SYSCALL_TTY_SET:
    tty_set(get_task(frame->ebx), (struct tty *)(uintptr_t)frame->ecx);
    break;
  case SYSCALL_TTY_FREE:
    tty_free((struct tty *)(uintptr_t)frame->ebx);
    break;
  }
}

static void syscall_return_to_app(x86_interrupt_frame_t *frame) {
  current_task()->ret_to_app = frame->ebx;
}

static void syscall_use_keyboard(x86_interrupt_frame_t *frame) {
  (void)frame;
  extern int disable_flag;
  extern mtask *keyboard_use_task;
  disable_flag = 1;
  keyboard_use_task = current_task();
}

static void syscall_custom_handler(x86_interrupt_frame_t *frame) {
  if (!custom_handler) {
    custom_handler = frame->ebx;
    custom_handler_pde = current_task()->pde;
    custom_handler_owner = current_task();
  }
}

static void syscall_map_memory(x86_interrupt_frame_t *frame) {
  unsigned target = frame->ebx & 0xfffff000;
  unsigned size = frame->ecx;
  unsigned target_pde = frame->edx;
  unsigned source = frame->esi & 0xfffff000;
  unsigned source_pde = frame->edi;
  unsigned count = div_round_up(size, 0x1000);

  irq_state_t state = irq_save();
  for (unsigned i = 0; i < count; i++) {
    unsigned physical = page_get_phy_pde(source + i * 0x1000, source_pde);
    page_set_physics_attr_pde(
        target + i * 0x1000, (void *)(uintptr_t)physical,
        PG_P | PG_USU | PG_RWW | PG_SHARED, target_pde);
  }
  irq_restore(state);
}

static void syscall_task_level(x86_interrupt_frame_t *frame) {
  irq_state_t state = irq_save();
  mtask *task = get_task(frame->ebx);
  if (task == NULL) {
    irq_restore(state);
    return;
  }

  if (frame->eax == SYSCALL_TASK_LEVEL_HIGH) {
    if (task->urgent) {
      irq_restore(state);
      return;
    }
    task->urgent = 1;
    task->timeout = 5;
  } else {
    task->urgent = 0;
    task->timeout = 1;
  }
  task->running = 0;
  irq_restore(state);
}

static void syscall_module(x86_interrupt_frame_t *frame) {
  switch (frame->eax) {
  case SYSCALL_MODULE_LOAD:
    frame->eax = module_load((const char *)(uintptr_t)frame->ebx);
    break;
  case SYSCALL_MODULE_UNLOAD:
    frame->eax = module_unload((const char *)(uintptr_t)frame->ebx);
    break;
  case SYSCALL_MODULE_LIST:
    frame->eax = module_list((module_handle_t *)(uintptr_t)frame->ebx,
                             frame->ecx);
    break;
  }
}

static void syscall_ipc(x86_interrupt_frame_t *frame) {
  frame->eax = ipc_syscall_dispatch(frame->ebx, frame->ecx, frame->edx);
}

static void syscall_network(x86_interrupt_frame_t *frame) {
  uint32_t owner_group = current_task()->tgid;
  switch (frame->ebx) {
  case NET_SYSCALL_OPEN:
    frame->eax = frame->ecx > 0xffu
                     ? -1
                     : net_socket_open(owner_group, (uint8_t)frame->ecx);
    break;
  case NET_SYSCALL_CLOSE:
    frame->eax = net_socket_close(owner_group, (int)frame->ecx);
    break;
  case NET_SYSCALL_CONFIGURE:
    frame->eax = frame->esi > 0xffffu || frame->ebp > 0xffffu
                     ? -1
                     : net_socket_configure(
                           owner_group, (int)frame->ecx, frame->edx,
                           (uint16_t)frame->esi, frame->edi,
                           (uint16_t)frame->ebp);
    break;
  case NET_SYSCALL_SEND:
    frame->eax =
        frame->esi > 0xffffu || !user_range_ok(frame->edx, frame->esi)
            ? -1
            : net_socket_send(owner_group, (int)frame->ecx,
                              (const void *)(uintptr_t)frame->edx,
                              frame->esi);
    break;
  case NET_SYSCALL_RECV:
    frame->eax =
        frame->esi == 0 || frame->esi > 0xffffu ||
                !user_range_ok(frame->edx, frame->esi)
            ? -1
            : net_socket_recv(owner_group, (int)frame->ecx,
                              (void *)(uintptr_t)frame->edx, frame->esi);
    break;
  case NET_SYSCALL_CONNECT:
    frame->eax = net_socket_connect(owner_group, (int)frame->ecx);
    break;
  case NET_SYSCALL_LISTEN:
    frame->eax = net_socket_listen(owner_group, (int)frame->ecx);
    break;
  case NET_SYSCALL_GET_IP:
    frame->eax = net_stack_ip();
    break;
  case NET_SYSCALL_PING:
    frame->eax = net_stack_ping(frame->ecx);
    break;
  default:
    frame->eax = -1;
    break;
  }
}

static const syscall_handler_t syscall_handlers[SYSCALL_COUNT] = {
    [SYSCALL_VERSION] = syscall_version,
    [SYSCALL_PRINT_CHARACTER] = syscall_print_character,
    [SYSCALL_LEGACY_GRAPHICS] = syscall_legacy_graphics,
    [SYSCALL_SET_CURSOR] = syscall_set_cursor,
    [SYSCALL_PRINT_STRING] = syscall_print_string,
    [SYSCALL_SLEEP] = syscall_sleep,
    [SYSCALL_HEAP_ADDRESS] = syscall_heap_info,
    [SYSCALL_HEAP_SIZE] = syscall_heap_info,
    [SYSCALL_TEXT_BOX] = syscall_text_box,
    [SYSCALL_BEEP] = syscall_beep,
    [SYSCALL_CURSOR_POSITION] = syscall_cursor_position,
    [SYSCALL_MOUSE_EVENT] = syscall_mouse_event,
    [SYSCALL_MOUSE_SUPPORTED] = syscall_mouse_supported,
    [SYSCALL_INPUT] = syscall_input,
    [SYSCALL_RUN_COMMAND] = syscall_run_shell_command,
    [SYSCALL_FILE_OPERATION] = syscall_file_operation,
    [SYSCALL_COMMAND_LINE] = syscall_command_line,
    [SYSCALL_COPY] = syscall_copy,
    [SYSCALL_KEYBOARD_HIT] = syscall_keyboard_hit,
    [SYSCALL_EXIT] = syscall_exit,
    [SYSCALL_VBE_CONTROL] = syscall_vbe_control,
    [SYSCALL_BIOS_VIDEO] = syscall_bios_video,
    [SYSCALL_TASK_CONTROL] = syscall_task_control,
    [SYSCALL_TTY_COLOR] = syscall_tty_color,
    [SYSCALL_TIMER_CONTROL] = syscall_timer_control,
    [SYSCALL_FORMAT] = syscall_format,
    [SYSCALL_RTC] = syscall_rtc,
    [SYSCALL_DRAW_PIXEL] = syscall_framebuffer,
    [SYSCALL_READ_PIXEL] = syscall_framebuffer,
    [SYSCALL_COPY_FRAMEBUFFER] = syscall_framebuffer,
    [SYSCALL_DRAW_BUFFER] = syscall_framebuffer,
    [SYSCALL_SCROLL_FRAMEBUFFER] = syscall_framebuffer,
    [SYSCALL_DRAW_BOX] = syscall_framebuffer,
    [SYSCALL_TIMESTAMP] = syscall_timestamp,
    [SYSCALL_UPTIME] = syscall_uptime,
    [SYSCALL_RESET_FPU] = syscall_reset_fpu,
    [SYSCALL_KEYBOARD_SETUP] = syscall_keyboard_setup,
    [SYSCALL_KEY_PRESS_PENDING] = syscall_keyboard_fifo,
    [SYSCALL_KEY_RELEASE_PENDING] = syscall_keyboard_fifo,
    [SYSCALL_KEY_PRESS_READ] = syscall_keyboard_fifo,
    [SYSCALL_KEY_RELEASE_READ] = syscall_keyboard_fifo,
    [SYSCALL_GROW_HEAP] = syscall_grow_heap,
    [SYSCALL_READ_ENV] = syscall_read_env,
    [SYSCALL_PATH_WITHOUT_DRIVE] = syscall_path_without_drive,
    [SYSCALL_CURRENT_DRIVE] = syscall_current_drive,
    [SYSCALL_EXECUTE] = syscall_execute,
    [SYSCALL_CLEAR] = syscall_clear,
    [SYSCALL_MOUNT_CHECK] = syscall_mount_operation,
    [SYSCALL_MOUNT] = syscall_mount_operation,
    [SYSCALL_CHANGE_DISK] = syscall_mount_operation,
    [SYSCALL_MEMORY_SIZE] = syscall_memory_info,
    [SYSCALL_USED_PAGES] = syscall_memory_info,
    [SYSCALL_DELETE_FILE] = syscall_delete_file,
    [SYSCALL_CHANGE_PATH] = syscall_change_path,
    [SYSCALL_CURSOR_START] = syscall_tty_cursor,
    [SYSCALL_CURSOR_STOP] = syscall_tty_cursor,
    [SYSCALL_UNMOUNT] = syscall_mount_operation,
    [SYSCALL_TTY_WIDTH] = syscall_tty_size,
    [SYSCALL_TTY_HEIGHT] = syscall_tty_size,
    [SYSCALL_RENAME] = syscall_rename,
    [SYSCALL_LOG] = syscall_log,
    [SYSCALL_SIGNAL_HANDLER] = syscall_signal_handler,
    [SYSCALL_FORK] = syscall_fork,
    [SYSCALL_WAIT] = syscall_wait,
    [SYSCALL_SET_RT] = syscall_set_rt,
    [SYSCALL_MOUSE_ENABLE] = syscall_mouse_enable,
    [SYSCALL_MOUSE_PENDING] = syscall_mouse_data,
    [SYSCALL_MOUSE_READ] = syscall_mouse_data,
    [SYSCALL_YIELD] = syscall_yield,
    [SYSCALL_TTY_ALLOC] = syscall_tty_object,
    [SYSCALL_TTY_SET] = syscall_tty_object,
    [SYSCALL_TTY_FREE] = syscall_tty_object,
    [SYSCALL_RETURN_TO_APP] = syscall_return_to_app,
    [SYSCALL_USE_KEYBOARD] = syscall_use_keyboard,
    [SYSCALL_CUSTOM_HANDLER] = syscall_custom_handler,
    [SYSCALL_MAP_MEMORY] = syscall_map_memory,
    [SYSCALL_TASK_LEVEL_HIGH] = syscall_task_level,
    [SYSCALL_TASK_LEVEL_NORMAL] = syscall_task_level,
    [SYSCALL_MODULE_LOAD] = syscall_module,
    [SYSCALL_MODULE_UNLOAD] = syscall_module,
    [SYSCALL_MODULE_LIST] = syscall_module,
    [SYSCALL_IPC] = syscall_ipc,
    [SYSCALL_NETWORK] = syscall_network,
};

void x86_syscall_dispatch(x86_interrupt_frame_t *frame) {
  irq_enable();
  if (frame->eax >= SYSCALL_COUNT || syscall_handlers[frame->eax] == NULL) {
    return;
  }
  syscall_handlers[frame->eax](frame);
}

void x86_custom_syscall_dispatch(x86_interrupt_frame_t *frame) {
  if (!custom_handler || custom_handler_owner == NULL) {
    return;
  }

  mtask *task = current_task();
  unsigned *alloc_size = task->alloc_size;
  unsigned alloc_addr = task->alloc_addr;
  unsigned tid = task->tid;
  task->alloc_size = custom_handler_owner->alloc_size;
  task->alloc_addr = custom_handler_owner->alloc_addr;
  task->tid = custom_handler_owner->tid;

  unsigned args[] = {
      frame->edi,         frame->esi, frame->ebp, frame->esp_dummy,
      frame->ebx,         frame->edx, frame->ecx, frame->eax,
      custom_handler_pde, task->pde,  tid,
  };
  char *argument_copy = NULL;
  if (frame->ebx) {
    char *source = (char *)(uintptr_t)frame->ebx;
    argument_copy = malloc(strlen(source) + 1);
    strcpy(argument_copy, source);
    args[4] = (uintptr_t)argument_copy;
  }

  call_across_page((uint32_t(*)(void *))(uintptr_t)custom_handler,
                   custom_handler_pde, args);
  if (argument_copy) {
    free(argument_copy);
    args[4] = frame->ebx;
  }

  frame->edi = args[0];
  frame->esi = args[1];
  frame->ebp = args[2];
  frame->esp_dummy = args[3];
  frame->ebx = args[4];
  frame->edx = args[5];
  frame->ecx = args[6];
  frame->eax = args[7];
  task->alloc_size = alloc_size;
  task->alloc_addr = alloc_addr;
  task->tid = tid;
}
