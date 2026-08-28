#include <arch/x86/interrupt.h>
#include <calendar.h>
#include <cmd.h>
#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <user_space.h>

unsigned div_round_up(unsigned num, unsigned size);

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
  SYSCALL_RESERVED_CUSTOM_HANDLER = 0x56,
  SYSCALL_SHARED_MEMORY = 0x57,
  SYSCALL_TASK_LEVEL_HIGH = 0x58,
  SYSCALL_TASK_LEVEL_NORMAL = 0x59,
  SYSCALL_MODULE_LOAD = 0x5a,
  SYSCALL_MODULE_UNLOAD = 0x5b,
  SYSCALL_MODULE_LIST = 0x5c,
  SYSCALL_IPC = 0x5d,
  SYSCALL_SOCKET = 0x5e,
  SYSCALL_MONOTONIC_NS = 0x5f,
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

static void syscall_monotonic_ns(x86_interrupt_frame_t *frame) {
  uint64_t nanoseconds = monotonic_time_ns();
  frame->eax = (uint32_t)nanoseconds;
  frame->edx = (uint32_t)(nanoseconds >> 32);
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

enum shared_memory_operation {
  SHARED_MEMORY_MAP_TO = 0x01,
  SHARED_MEMORY_UNMAP = 0x02,
};

static bool shared_memory_range_ok(uint32_t address, uint32_t size,
                                   uint32_t lower, uint32_t upper) {
  if (size == 0 || size > upper - lower ||
      ((address | size) & 0xfffu) != 0 || address < lower) {
    return false;
  }
  return address <= upper - size;
}

static void syscall_shared_memory(x86_interrupt_frame_t *frame) {
  bool success = false;
  if (frame->ebx == SHARED_MEMORY_MAP_TO) {
    if (!shared_memory_range_ok(frame->edi, frame->ebp, USER_SPACE_START,
                                USER_HEAP_END) ||
        !shared_memory_range_ok(frame->esi, frame->ebp, USER_HEAP_END,
                                USER_SHARED_END)) {
      frame->eax = -1;
      return;
    }

    irq_state_t state = irq_save();
    mtask *target = get_task(frame->ecx);
    if (target != NULL && target->state != DIED &&
        target->generation == frame->edx && target->pde != 0) {
      success = page_share_range_pde(frame->edi, frame->esi, frame->ebp,
                                     current_task()->pde, target->pde);
    }
    irq_restore(state);
  } else if (frame->ebx == SHARED_MEMORY_UNMAP) {
    if (!shared_memory_range_ok(frame->esi, frame->ebp, USER_HEAP_END,
                                USER_SHARED_END)) {
      frame->eax = -1;
      return;
    }
    irq_state_t state = irq_save();
    success = page_unmap_shared_range_pde(frame->esi, frame->ebp,
                                           current_task()->pde);
    irq_restore(state);
  }
  frame->eax = success ? 0 : -1;
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

typedef struct {
  uint16_t family;
  uint8_t data[14];
} socket_user_sockaddr_t;

typedef struct {
  uint16_t family;
  uint16_t port;
  uint32_t address;
  uint8_t zero[8];
} socket_user_sockaddr_in_t;

typedef struct {
  uint16_t family;
  char path[NET_SOCKET_LOCAL_PATH_MAX];
} socket_user_sockaddr_un_t;

static int socket_parse_user_address(uint32_t pointer, uint32_t length,
                                     net_socket_address_t *address) {
  if (address == NULL || length < sizeof(uint16_t) ||
      !user_range_ok(pointer, length)) {
    return NET_SOCKET_ERR_INVAL;
  }

  const socket_user_sockaddr_t *source =
      (const socket_user_sockaddr_t *)(uintptr_t)pointer;
  if (source->family == NET_SOCKET_AF_INET) {
    if (length < sizeof(socket_user_sockaddr_in_t)) {
      return NET_SOCKET_ERR_INVAL;
    }
    const socket_user_sockaddr_in_t *inet =
        (const socket_user_sockaddr_in_t *)(uintptr_t)pointer;
    memset(address, 0, sizeof(*address));
    address->family = NET_SOCKET_AF_INET;
    address->value.inet.address = inet->address;
    address->value.inet.port = inet->port;
    return 0;
  }
  if (source->family != NET_SOCKET_AF_LOCAL) {
    return NET_SOCKET_ERR_INVAL;
  }

  const socket_user_sockaddr_un_t *local =
      (const socket_user_sockaddr_un_t *)(uintptr_t)pointer;
  uint32_t capacity = length - sizeof(local->family);
  if (capacity > NET_SOCKET_LOCAL_PATH_MAX) {
    capacity = NET_SOCKET_LOCAL_PATH_MAX;
  }
  uint32_t path_length = 0;
  while (path_length < capacity && local->path[path_length] != '\0') {
    path_length++;
  }
  if (path_length == capacity) {
    return NET_SOCKET_ERR_INVAL;
  }
  memset(address, 0, sizeof(*address));
  address->family = NET_SOCKET_AF_LOCAL;
  address->value.local.length = (uint16_t)path_length;
  memcpy(address->value.local.path, local->path, path_length);
  return 0;
}

static int socket_write_user_address(net_socket_syscall_request_t *request,
                                     const net_socket_address_t *address) {
  if (request == NULL || address == NULL) {
    return NET_SOCKET_ERR_INVAL;
  }
  uint32_t required = address->family == NET_SOCKET_AF_INET
                          ? sizeof(socket_user_sockaddr_in_t)
                          : sizeof(socket_user_sockaddr_un_t);
  if (request->address == 0) {
    request->address_length = required;
    return 0;
  }
  if (request->address_length < required ||
      !user_range_ok(request->address, required)) {
    return NET_SOCKET_ERR_INVAL;
  }

  if (address->family == NET_SOCKET_AF_INET) {
    socket_user_sockaddr_in_t *inet =
        (socket_user_sockaddr_in_t *)(uintptr_t)request->address;
    memset(inet, 0, sizeof(*inet));
    inet->family = NET_SOCKET_AF_INET;
    inet->port = address->value.inet.port;
    inet->address = address->value.inet.address;
  } else if (address->family == NET_SOCKET_AF_LOCAL) {
    socket_user_sockaddr_un_t *local =
        (socket_user_sockaddr_un_t *)(uintptr_t)request->address;
    memset(local, 0, sizeof(*local));
    local->family = NET_SOCKET_AF_LOCAL;
    memcpy(local->path, address->value.local.path,
           address->value.local.length);
  } else {
    return NET_SOCKET_ERR_INVAL;
  }
  request->address_length = required;
  return 0;
}

typedef int (*socket_syscall_handler_t)(uint32_t owner_group,
                                        net_socket_syscall_request_t *request);

static int socket_syscall_create(uint32_t owner_group,
                                 net_socket_syscall_request_t *request) {
  return net_socket_create(owner_group, request->domain, request->type,
                           request->protocol);
}

static int socket_syscall_close(uint32_t owner_group,
                                net_socket_syscall_request_t *request) {
  return net_socket_close(owner_group, request->socket);
}

static int socket_syscall_bind(uint32_t owner_group,
                               net_socket_syscall_request_t *request) {
  net_socket_address_t address;
  int result = socket_parse_user_address(request->address,
                                         request->address_length, &address);
  return result == 0 ? net_socket_bind(owner_group, request->socket, &address)
                     : result;
}

static int socket_syscall_connect(uint32_t owner_group,
                                  net_socket_syscall_request_t *request) {
  net_socket_address_t address;
  int result = socket_parse_user_address(request->address,
                                         request->address_length, &address);
  return result == 0 ? net_socket_connect(owner_group, request->socket, &address)
                     : result;
}

static int socket_syscall_listen(uint32_t owner_group,
                                 net_socket_syscall_request_t *request) {
  return net_socket_listen(owner_group, request->socket, request->backlog);
}

static int socket_syscall_accept(uint32_t owner_group,
                                 net_socket_syscall_request_t *request) {
  net_socket_address_t address;
  int accepted = net_socket_accept(owner_group, request->socket, &address);
  if (accepted < 0) {
    return accepted;
  }
  int result = socket_write_user_address(request, &address);
  if (result != 0) {
    (void)net_socket_close(owner_group, accepted);
    return result;
  }
  return accepted;
}

static int socket_syscall_sendto(uint32_t owner_group,
                                 net_socket_syscall_request_t *request) {
  if (request->length != 0 &&
      !user_range_ok(request->buffer, request->length)) {
    return NET_SOCKET_ERR_INVAL;
  }
  net_socket_address_t address;
  const net_socket_address_t *destination = NULL;
  if (request->address != 0 || request->address_length != 0) {
    int result = socket_parse_user_address(request->address,
                                           request->address_length, &address);
    if (result != 0) {
      return result;
    }
    destination = &address;
  }
  return net_socket_sendto(owner_group, request->socket,
                           (const void *)(uintptr_t)request->buffer,
                           request->length, request->flags, destination);
}

static int socket_syscall_recvfrom(uint32_t owner_group,
                                   net_socket_syscall_request_t *request) {
  if (request->length != 0 &&
      !user_range_ok(request->buffer, request->length)) {
    return NET_SOCKET_ERR_INVAL;
  }
  if (request->address == 0 && request->address_length != 0) {
    return NET_SOCKET_ERR_INVAL;
  }
  net_socket_address_t address;
  net_socket_address_t *source = request->address == 0 ? NULL : &address;
  int received = net_socket_recvfrom(owner_group, request->socket,
                                     (void *)(uintptr_t)request->buffer,
                                     request->length, request->flags, source);
  if (received < 0 || source == NULL) {
    return received;
  }
  int result = socket_write_user_address(request, source);
  return result == 0 ? received : result;
}

static int socket_syscall_getname(uint32_t owner_group,
                                  net_socket_syscall_request_t *request,
                                  bool peer) {
  net_socket_address_t address;
  int result = net_socket_getname(owner_group, request->socket, peer, &address);
  return result == 0 ? socket_write_user_address(request, &address) : result;
}

static int socket_syscall_getsockname(uint32_t owner_group,
                                      net_socket_syscall_request_t *request) {
  return socket_syscall_getname(owner_group, request, false);
}

static int socket_syscall_getpeername(uint32_t owner_group,
                                      net_socket_syscall_request_t *request) {
  return socket_syscall_getname(owner_group, request, true);
}

static int socket_syscall_resolve(uint32_t owner_group,
                                  net_socket_syscall_request_t *request) {
  (void)owner_group;
  if (request->length == 0 || request->length > 255 ||
      !user_range_ok(request->buffer, request->length) ||
      !user_range_ok(request->address, sizeof(uint32_t)) ||
      ((const char *)(uintptr_t)request->buffer)[request->length - 1] != '\0') {
    return NET_SOCKET_ERR_INVAL;
  }
  return net_socket_resolve((const char *)(uintptr_t)request->buffer,
                            request->length,
                            (uint32_t *)(uintptr_t)request->address);
}

static int socket_syscall_interface_address(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  (void)owner_group;
  if (!user_range_ok(request->address, sizeof(uint32_t))) {
    return NET_SOCKET_ERR_INVAL;
  }
  uint32_t address = net_stack_ipv4();
  if (address == 0) {
    return NET_SOCKET_ERR_AGAIN;
  }
  *(uint32_t *)(uintptr_t)request->address = address;
  return 0;
}

static int socket_syscall_set_option(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  return net_socket_set_option(owner_group, request->socket, request->domain,
                               request->type, request->length);
}

static const socket_syscall_handler_t
    socket_syscall_handlers[NET_SOCKET_SYSCALL_COUNT] = {
        [NET_SOCKET_SYSCALL_CREATE] = socket_syscall_create,
        [NET_SOCKET_SYSCALL_CLOSE] = socket_syscall_close,
        [NET_SOCKET_SYSCALL_BIND] = socket_syscall_bind,
        [NET_SOCKET_SYSCALL_CONNECT] = socket_syscall_connect,
        [NET_SOCKET_SYSCALL_LISTEN] = socket_syscall_listen,
        [NET_SOCKET_SYSCALL_ACCEPT] = socket_syscall_accept,
        [NET_SOCKET_SYSCALL_SENDTO] = socket_syscall_sendto,
        [NET_SOCKET_SYSCALL_RECVFROM] = socket_syscall_recvfrom,
        [NET_SOCKET_SYSCALL_GETSOCKNAME] = socket_syscall_getsockname,
        [NET_SOCKET_SYSCALL_GETPEERNAME] = socket_syscall_getpeername,
        [NET_SOCKET_SYSCALL_RESOLVE] = socket_syscall_resolve,
        [NET_SOCKET_SYSCALL_INTERFACE_ADDRESS] = socket_syscall_interface_address,
        [NET_SOCKET_SYSCALL_SET_OPTION] = socket_syscall_set_option,
};

static void syscall_socket(x86_interrupt_frame_t *frame) {
  if (frame->ebx >= NET_SOCKET_SYSCALL_COUNT ||
      socket_syscall_handlers[frame->ebx] == NULL ||
      !user_range_ok(frame->ecx, sizeof(net_socket_syscall_request_t))) {
    frame->eax = NET_SOCKET_ERR_INVAL;
    return;
  }
  net_socket_syscall_request_t *request =
      (net_socket_syscall_request_t *)(uintptr_t)frame->ecx;
  frame->eax = socket_syscall_handlers[frame->ebx](current_task()->tgid,
                                                    request);
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
    [SYSCALL_SHARED_MEMORY] = syscall_shared_memory,
    [SYSCALL_TASK_LEVEL_HIGH] = syscall_task_level,
    [SYSCALL_TASK_LEVEL_NORMAL] = syscall_task_level,
    [SYSCALL_MODULE_LOAD] = syscall_module,
    [SYSCALL_MODULE_UNLOAD] = syscall_module,
    [SYSCALL_MODULE_LIST] = syscall_module,
    [SYSCALL_IPC] = syscall_ipc,
    [SYSCALL_SOCKET] = syscall_socket,
    [SYSCALL_MONOTONIC_NS] = syscall_monotonic_ns,
};

void x86_syscall_dispatch(x86_interrupt_frame_t *frame) {
  irq_enable();
  if (frame->eax >= SYSCALL_COUNT || syscall_handlers[frame->eax] == NULL) {
    return;
  }
  syscall_handlers[frame->eax](frame);
}
