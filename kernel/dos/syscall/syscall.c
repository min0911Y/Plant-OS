#include <scheduler.h>
#include <cmd.h>
#include <dos.h>
#include <executable.h>
#include <fcntl.h>
#include <framebuffer.h>
#include <futex.h>
#include <input_device.h>
#include <irq.h>
#include <limits.h>
#include <math_util.h>
#include <platform.h>
#include <stdint.h>
#include <syscall.h>
#include <tty_rpc.h>
#include <user_space.h>
#include <user_thread.h>
#include <user_vm.h>
#if defined(KERNEL_ARCH_X86_64)
#include <arch/x86/x86_64/cpu.h>
#endif

static void keyboard_press(uint8_t data, uint32_t tid) {
  fifo8_put(get_task(tid)->Pkeyfifo, data);
}

static void keyboard_release(uint8_t data, uint32_t tid) {
  fifo8_put(get_task(tid)->Ukeyfifo, data);
}

static int user_range_ok(uintptr_t addr, size_t size) {
#if defined(KERNEL_ARCH_X86_64)
  return x64_user_access(addr, size, false);
#else
  return addr >= USER_SPACE_START && addr <= USER_HEAP_END &&
         size <= USER_HEAP_END - addr;
#endif
}

static char *copy_user_string(uintptr_t address, size_t *length_out) {
  irq_state_t state = irq_save();
  char *copy = NULL;
  if (address < USER_SPACE_START || address >= USER_HEAP_END)
    goto finished;
  const char *source = (const char *)address;
  size_t available = USER_HEAP_END - address;
  if (available > INT_MAX)
    available = INT_MAX;
  size_t length = 0;
  while (length < available) {
    size_t count = VM_PAGE_SIZE - ((address + length) & (VM_PAGE_SIZE - 1));
    if (count > available - length)
      count = available - length;
    if (!user_vm_readable(address + length, count))
      break;
    size_t end = length + count;
    for (; length < end; length++) {
      if (source[length])
        continue;
      copy = malloc(length + 1);
      if (copy) {
        memcpy(copy, source, length + 1);
        *length_out = length;
      }
      goto finished;
    }
  }
finished:
  irq_restore(state);
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

typedef int (*ipc_syscall_handler_t)(uintptr_t arg1, uintptr_t arg2);

static int ipc_syscall_send(uintptr_t arg1, uintptr_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, sizeof(ipc_user_msg_t))) {
    return IPC_ERR_INVAL;
  }

  ipc_user_msg_t *message = (ipc_user_msg_t *)(uintptr_t)arg1;
  if ((message->id & RPC_KERNEL_CALL) &&
      (message->type == RPC_TYPE_REQUEST || message->type == RPC_TYPE_NOTIFY))
    return IPC_ERR_INVAL;
  if (message->size &&
      !user_range_ok((uintptr_t)message->data, message->size)) {
    return IPC_ERR_INVAL;
  }
  return ipc_send(message->peer_tid, message->peer_generation, message->type,
                  message->id, message->data, message->size, message->flags,
                  message->timeout_ms);
}

static int ipc_syscall_receive(uintptr_t arg1, uintptr_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, sizeof(ipc_user_msg_t))) {
    return IPC_ERR_INVAL;
  }

  ipc_user_msg_t *message = (ipc_user_msg_t *)(uintptr_t)arg1;
  if (message->size &&
      !user_range_ok((uintptr_t)message->data, message->size)) {
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

static int ipc_syscall_peek(uintptr_t arg1, uintptr_t arg2) {
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

static int ipc_syscall_pending(uintptr_t arg1, uintptr_t arg2) {
  (void)arg1;
  (void)arg2;
  return ipc_pending();
}

static int ipc_syscall_register(uintptr_t arg1, uintptr_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, IPC_NAME_MAX)) {
    return IPC_ERR_INVAL;
  }
  return ipc_service_register((const char *)(uintptr_t)arg1);
}

static int ipc_syscall_unregister(uintptr_t arg1, uintptr_t arg2) {
  (void)arg2;
  if (!user_range_ok(arg1, IPC_NAME_MAX)) {
    return IPC_ERR_INVAL;
  }
  return ipc_service_unregister((const char *)(uintptr_t)arg1);
}

static int ipc_syscall_lookup(uintptr_t arg1, uintptr_t arg2) {
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

static int ipc_syscall_generation(uintptr_t arg1, uintptr_t arg2) {
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

static int ipc_syscall_dispatch(uintptr_t id, uintptr_t arg1, uintptr_t arg2) {
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
  SYSCALL_VFS = 0x1a,
  SYSCALL_COMMAND_LINE = 0x1b,
  SYSCALL_KEYBOARD_HIT = 0x1d,
  SYSCALL_EXIT = 0x1e,
  SYSCALL_VIDEO_CONTROL = 0x20,
  SYSCALL_BIOS_VIDEO = 0x21,
  SYSCALL_TASK_CONTROL = 0x22,
  SYSCALL_TTY_COLOR = 0x23,
  SYSCALL_TIMER_CONTROL = 0x24,
  SYSCALL_RTC = 0x26,
  SYSCALL_DRAW_PIXEL = 0x27,
  SYSCALL_READ_PIXEL = 0x28,
  SYSCALL_COPY_FRAMEBUFFER = 0x29,
  SYSCALL_DRAW_BUFFER = 0x2a,
  SYSCALL_SCROLL_FRAMEBUFFER = 0x2b,
  SYSCALL_DRAW_BOX = 0x2c,
  SYSCALL_TIMESTAMP = 0x2d,
  SYSCALL_UPTIME = 0x2e,
  SYSCALL_RESET_FPU = SYSCALL_ARCH_RESET_FPU,
  SYSCALL_KEYBOARD_SETUP = 0x30,
  SYSCALL_KEY_PRESS_PENDING = 0x31,
  SYSCALL_KEY_RELEASE_PENDING = 0x32,
  SYSCALL_KEY_PRESS_READ = 0x33,
  SYSCALL_KEY_RELEASE_READ = 0x34,
  SYSCALL_GROW_HEAP = 0x35,
  SYSCALL_READ_ENV = 0x36,
  SYSCALL_EXECUTE = 0x39,
  SYSCALL_CLEAR = 0x3a,
  SYSCALL_MEMORY_SIZE = 0x3e,
  SYSCALL_USED_PAGES = 0x3f,
  SYSCALL_CURSOR_START = 0x42,
  SYSCALL_CURSOR_STOP = 0x43,
  SYSCALL_TTY_WIDTH = 0x45,
  SYSCALL_TTY_HEIGHT = 0x46,
  SYSCALL_LOG = 0x48,
  SYSCALL_SIGNAL_HANDLER = 0x49,
  SYSCALL_FORK = 0x4a,
  SYSCALL_WAIT = 0x4b,
  SYSCALL_RESERVED_LEGACY_TEST = 0x4c,
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
  SYSCALL_TASK_SNAPSHOT = 0x60,
  SYSCALL_CPU_INFO = 0x61,
  SYSCALL_TTY_INPUT_NOTIFY = 0x62,
  SYSCALL_PERF_CONTROL = 0x63,
  SYSCALL_INPUT_WAIT = 0x64,
  SYSCALL_SIGNAL_RETURN = SYSCALL_ARCH_SIGNAL_RETURN,
  SYSCALL_VIRTUAL_MEMORY = SYSCALL_VM,
  SYSCALL_USER_FUTEX = SYSCALL_FUTEX,
  SYSCALL_NATIVE_THREAD = SYSCALL_THREAD,
  SYSCALL_TTY_POINTER = 0x69,
  SYSCALL_POWER_OFF = 0x6a,
  SYSCALL_COUNT,
};

typedef void (*syscall_handler_t)(syscall_context_t *frame);

static void syscall_version(syscall_context_t *frame) {
  frame->argument2 = 0x302e3762;
}

static void syscall_print_character(syscall_context_t *frame) {
  printchar(frame->argument2 & 0xff);
}

static void syscall_legacy_graphics(syscall_context_t *frame) {
#if defined(KERNEL_ARCH_I386)
  frame->value = (syscall_word_t)-1;
  if (running_mode != POWERINTDOS) {
    return;
  }

  switch (frame->argument0) {
  case 0x01:
    platform_video_legacy_text_mode();
    break;
  case 0x02:
    platform_video_legacy_graphics_mode();
    break;
  case 0x03:
    Draw_Char(frame->argument1, frame->argument2, frame->argument3, frame->argument4);
    break;
  case 0x04:
    PrintChineseChar(frame->argument1, frame->argument2, frame->argument4, frame->argument3);
    break;
  case 0x05:
    Draw_Box(frame->argument1, frame->argument2, frame->argument3, frame->argument4, frame->argument5);
    break;
  case 0x06:
    Draw_Px(frame->argument1, frame->argument2, frame->argument3);
    break;
  case 0x07:
    Draw_Str(frame->argument1, frame->argument2, (char *)(uintptr_t)frame->argument3,
             frame->argument4);
    break;
  case 0x08:
    PrintChineseStr(frame->argument1, frame->argument2, frame->argument4,
                    (unsigned char *)(uintptr_t)frame->argument3);
    break;
  default:
    return;
  }
  frame->value = 0;
#else
  frame->value = (syscall_word_t)-1;
#endif
}

static void syscall_set_cursor(syscall_context_t *frame) {
  gotoxy(frame->argument2, frame->argument1);
}

static void syscall_print_string(syscall_context_t *frame) {
  print((char *)(uintptr_t)frame->argument2);
}

static void syscall_sleep(syscall_context_t *frame) {
  sleep(frame->argument2);
}

static void syscall_heap_info(syscall_context_t *frame) {
  mtask *task = current_task();
  if (frame->value == SYSCALL_HEAP_ADDRESS) {
    frame->argument2 = task->alloc_addr;
  } else {
    frame->argument2 = task->alloc_size ? *task->alloc_size : 0;
  }
}

static void syscall_text_box(syscall_context_t *frame) {
  Text_Draw_Box(frame->argument1, frame->argument0, frame->argument3, frame->argument2,
                (unsigned char)frame->argument4);
}

static void syscall_beep(syscall_context_t *frame) {
  beep(frame->argument0, frame->argument1, frame->argument2);
}

static void syscall_cursor_position(syscall_context_t *frame) {
  frame->argument1 = get_y();
  frame->argument2 = get_x();
}

static void syscall_mouse_event(syscall_context_t *frame) {
#if defined(KERNEL_ARCH_I386)
  if (running_mode != POWERINTDOS) {
    return;
  }

  mtask *task = current_task();
  int old_mouse_x = task->mx;
  int old_mouse_y = task->my;
  int64_t buffer_x = (int64_t)task->mx * 8;
  int64_t buffer_y = (int64_t)task->my * 16;
  int background = *(char *)(task->TTY->vram + old_mouse_y *
                                                    task->TTY->xsize * 2 +
                            old_mouse_x * 2 + 1);

  if (mouse_use_task == NULL) {
    mouse_ready();
  }

  mouse_event_t event;
  for (;;) {
    if (!input_mouse_read(&event)) {
      task_fall_blocked_reason(WAITING, WAIT_REASON_INPUT);
      continue;
    }

    if (task->TTY != now_tty() && task->TTY->using1 == 1) {
      continue;
    }
    if (event.wheel != MOUSE_ROLL_NONE) {
      frame->argument1 = task->mx;
      frame->argument2 = task->my;
      frame->argument3 =
          3 + (event.wheel > 0 ? MOUSE_ROLL_UP : MOUSE_ROLL_DOWN);
      *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 +
                task->mx * 2 + 1) = background;
      task->mx = old_mouse_x;
      task->my = old_mouse_y;
      return;
    }

    old_mouse_x = task->mx;
    old_mouse_y = task->my;
    buffer_x += event.x;
    buffer_y += event.y;
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

    if (event.buttons & 0x01) {
      frame->argument3 = 1;
    } else if (event.buttons & 0x02) {
      frame->argument3 = 2;
    } else if (event.buttons & 0x04) {
      frame->argument3 = 3;
    } else {
      continue;
    }
    frame->argument1 = task->mx;
    frame->argument2 = task->my;
    break;
  }

  *(char *)(task->TTY->vram + task->my * task->TTY->xsize * 2 + task->mx * 2 +
            1) = background;
  task->mx = old_mouse_x;
  task->my = old_mouse_y;
#else
  frame->value = (syscall_word_t)-1;
#endif
}

static void syscall_mouse_supported(syscall_context_t *frame) {

  frame->value = running_mode == POWERINTDOS && mouse_use_task == NULL;
}

static void syscall_input(syscall_context_t *frame) {
  switch (frame->argument0) {
  case 0x01:
    frame->argument2 = getch();
    break;
  case 0x02:
    frame->argument2 = input_char_inSM();
    break;
  case 0x03:
    input((char *)(uintptr_t)frame->argument2, frame->argument1);
    break;
  }
}

static void syscall_run_shell_command(syscall_context_t *frame) {
  size_t command_length;
  char *command = copy_user_string(frame->argument2, &command_length);
  if (command == NULL) {
    frame->value = -1;
    return;
  }

  frame->value = run_shell_command(command, command_length);
  free(command);
}

typedef int (*vfs_syscall_handler_t)(const vfs_syscall_request_t *request);

static char *vfs_syscall_path(uintptr_t address) {
  size_t length;
  return copy_user_string(address, &length);
}

static int vfs_syscall_open(const vfs_syscall_request_t *request) {
  char *path = vfs_syscall_path(request->arguments.open.path);
  if (path == NULL) {
    return VFS_ERROR_INVALID;
  }
  int descriptor = vfs_fd_open(current_task()->fs_context, path,
                               request->arguments.open.flags);
  free(path);
  return descriptor;
}

static int vfs_syscall_close(const vfs_syscall_request_t *request) {
  return vfs_fd_close(current_task()->fs_context,
                      request->arguments.descriptor.descriptor);
}

static int vfs_syscall_fcntl(const vfs_syscall_request_t *request) {
  return vfs_fd_fcntl(current_task()->fs_context,
                      request->arguments.fcntl.descriptor,
                      request->arguments.fcntl.command,
                      request->arguments.fcntl.argument);
}

static int vfs_syscall_read(const vfs_syscall_request_t *request) {
  if (request->arguments.io.length != 0 &&
      !user_range_ok(request->arguments.io.buffer,
                     request->arguments.io.length)) {
    return VFS_ERROR_INVALID;
  }
  return vfs_fd_read(current_task()->fs_context,
                     request->arguments.io.descriptor,
                     (void *)(uintptr_t)request->arguments.io.buffer,
                     request->arguments.io.length);
}

static int vfs_syscall_pread(const vfs_syscall_request_t *request) {
  if (request->arguments.positioned_io.length != 0 &&
      !user_range_ok(request->arguments.positioned_io.buffer,
                     request->arguments.positioned_io.length)) {
    return VFS_ERROR_INVALID;
  }
  return vfs_fd_pread(current_task()->fs_context,
                      request->arguments.positioned_io.descriptor,
                      (void *)(uintptr_t)
                          request->arguments.positioned_io.buffer,
                      request->arguments.positioned_io.length,
                      request->arguments.positioned_io.offset);
}

static int vfs_syscall_write(const vfs_syscall_request_t *request) {
  if (request->arguments.io.length != 0 &&
      !user_range_ok(request->arguments.io.buffer,
                     request->arguments.io.length)) {
    return VFS_ERROR_INVALID;
  }
  return vfs_fd_write(current_task()->fs_context,
                      request->arguments.io.descriptor,
                      (const void *)(uintptr_t)request->arguments.io.buffer,
                      request->arguments.io.length);
}

static int vfs_syscall_seek(const vfs_syscall_request_t *request) {
  return vfs_fd_seek(current_task()->fs_context,
                     request->arguments.seek.descriptor,
                     request->arguments.seek.offset,
                     request->arguments.seek.whence);
}

static int vfs_syscall_sync(const vfs_syscall_request_t *request) {
  return vfs_fd_sync(current_task()->fs_context,
                     request->arguments.descriptor.descriptor);
}

static int vfs_syscall_stat(const vfs_syscall_request_t *request) {
  char *path = vfs_syscall_path(request->arguments.stat.path);
  if (path == NULL) {
    return VFS_ERROR_INVALID;
  }
  vfs_stat_t result;
  int status = vfs_stat(current_task()->fs_context, path, &result);
  free(path);
  if (status == VFS_OK &&
      !user_vm_copy_to(request->arguments.stat.status, &result, sizeof(result)))
    return VM_ERROR_FAULT;
  return status;
}

static int vfs_syscall_fstat(const vfs_syscall_request_t *request) {
  vfs_stat_t result;
  int status = vfs_fd_stat(current_task()->fs_context,
                           request->arguments.fstat.descriptor, &result);
  if (status == VFS_OK && !user_vm_copy_to(request->arguments.fstat.status,
                                           &result, sizeof(result)))
    return VM_ERROR_FAULT;
  return status;
}

static int vfs_syscall_list(const vfs_syscall_request_t *request) {
  if (request->arguments.list.capacity > INT_MAX / sizeof(vfs_file) ||
      (request->arguments.list.entries == 0 &&
       request->arguments.list.capacity != 0) ||
      (request->arguments.list.entries != 0 &&
       !user_range_ok(request->arguments.list.entries,
                      request->arguments.list.capacity * sizeof(vfs_file)))) {
    return VFS_ERROR_INVALID;
  }
  char *path = vfs_syscall_path(request->arguments.list.path);
  if (path == NULL) {
    return VFS_ERROR_INVALID;
  }
  size_t count = 0;
  int status = vfs_list_directory(
      current_task()->fs_context, path,
      (vfs_file *)(uintptr_t)request->arguments.list.entries,
      request->arguments.list.capacity, &count);
  free(path);
  if (status == VFS_ERROR_OVERFLOW) {
    return -2;
  }
  return status < 0 || count > INT_MAX ? status < 0 ? status
                                                    : VFS_ERROR_OVERFLOW
                                         : (int)count;
}

static int vfs_syscall_single_path(const vfs_syscall_request_t *request,
                                   int (*operation)(vfs_context_t *,
                                                    const char *)) {
  char *path = vfs_syscall_path(request->arguments.path.path);
  if (path == NULL) {
    return VFS_ERROR_INVALID;
  }
  int status = operation(current_task()->fs_context, path);
  free(path);
  return status;
}

static int vfs_syscall_mkdir(const vfs_syscall_request_t *request) {
  return vfs_syscall_single_path(request, vfs_mkdir);
}

static int vfs_syscall_unlink(const vfs_syscall_request_t *request) {
  return vfs_syscall_single_path(request, vfs_unlink);
}

static int vfs_syscall_rmdir(const vfs_syscall_request_t *request) {
  return vfs_syscall_single_path(request, vfs_rmdir);
}

static int vfs_syscall_chdir(const vfs_syscall_request_t *request) {
  return vfs_syscall_single_path(request, vfs_context_chdir);
}

static int vfs_syscall_fchdir(const vfs_syscall_request_t *request) {
  return vfs_context_fchdir(current_task()->fs_context,
                            request->arguments.descriptor.descriptor);
}

static int vfs_syscall_rename(const vfs_syscall_request_t *request) {
  char *source = vfs_syscall_path(request->arguments.rename.source);
  char *destination =
      vfs_syscall_path(request->arguments.rename.destination);
  if (source == NULL || destination == NULL) {
    free(source);
    free(destination);
    return VFS_ERROR_INVALID;
  }
  int status = vfs_rename(current_task()->fs_context, source, destination);
  free(source);
  free(destination);
  return status;
}

static int vfs_syscall_getcwd(const vfs_syscall_request_t *request) {
  if (request->arguments.cwd.buffer != 0 &&
      !user_range_ok(request->arguments.cwd.buffer,
                     request->arguments.cwd.capacity)) {
    return VFS_ERROR_INVALID;
  }
  return vfs_context_getcwd(
      current_task()->fs_context,
      (char *)(uintptr_t)request->arguments.cwd.buffer,
      request->arguments.cwd.capacity);
}

static int vfs_syscall_current_drive(const vfs_syscall_request_t *request) {
  (void)request;
  return vfs_context_drive(current_task()->fs_context);
}

static int vfs_syscall_mount_check(const vfs_syscall_request_t *request) {
  return vfs_check_mount(request->arguments.mount.drive);
}

static int vfs_syscall_mount(const vfs_syscall_request_t *request) {
  return vfs_mount_disk(request->arguments.mount.disk,
                        request->arguments.mount.drive);
}

static int vfs_syscall_unmount(const vfs_syscall_request_t *request) {
  return vfs_unmount_disk(request->arguments.mount.drive);
}

static int vfs_syscall_change_drive(const vfs_syscall_request_t *request) {
  return vfs_context_change_drive(current_task()->fs_context,
                                  request->arguments.mount.drive);
}

static int vfs_syscall_format(const vfs_syscall_request_t *request) {
  char *name = vfs_syscall_path(request->arguments.format.filesystem);
  if (name == NULL) {
    return VFS_ERROR_INVALID;
  }
  int status = vfs_format(request->arguments.format.disk, name);
  free(name);
  return status;
}

static int vfs_syscall_truncate(const vfs_syscall_request_t *request) {
  return vfs_fd_truncate(current_task()->fs_context,
                         request->arguments.truncate.descriptor,
                         request->arguments.truncate.length);
}

static int vfs_syscall_realpath(const vfs_syscall_request_t *request) {
  char *path = vfs_syscall_path(request->arguments.canonical.path);
  if (!path)
    return VFS_ERROR_INVALID;
  char *result;
  int length = vfs_realpath(current_task()->fs_context, path, &result);
  free(path);
  if (length < 0)
    return length;
  int status = length;
  if (request->arguments.canonical.capacity) {
    if (request->arguments.canonical.capacity <= (uint32_t)length)
      status = VFS_ERROR_OVERFLOW;
    else if (!user_vm_copy_to(request->arguments.canonical.buffer, result,
                              (size_t)length + 1))
      status = VM_ERROR_FAULT;
  }
  free(result);
  return status;
}

static const vfs_syscall_handler_t vfs_syscall_handlers[VFS_SYSCALL_COUNT] = {
    [VFS_SYSCALL_OPEN] = vfs_syscall_open,
    [VFS_SYSCALL_CLOSE] = vfs_syscall_close,
    [VFS_SYSCALL_FCNTL] = vfs_syscall_fcntl,
    [VFS_SYSCALL_READ] = vfs_syscall_read,
    [VFS_SYSCALL_WRITE] = vfs_syscall_write,
    [VFS_SYSCALL_SEEK] = vfs_syscall_seek,
    [VFS_SYSCALL_SYNC] = vfs_syscall_sync,
    [VFS_SYSCALL_STAT] = vfs_syscall_stat,
    [VFS_SYSCALL_FSTAT] = vfs_syscall_fstat,
    [VFS_SYSCALL_LIST_DIRECTORY] = vfs_syscall_list,
    [VFS_SYSCALL_MKDIR] = vfs_syscall_mkdir,
    [VFS_SYSCALL_UNLINK] = vfs_syscall_unlink,
    [VFS_SYSCALL_RMDIR] = vfs_syscall_rmdir,
    [VFS_SYSCALL_RENAME] = vfs_syscall_rename,
    [VFS_SYSCALL_CHDIR] = vfs_syscall_chdir,
    [VFS_SYSCALL_GETCWD] = vfs_syscall_getcwd,
    [VFS_SYSCALL_CURRENT_DRIVE] = vfs_syscall_current_drive,
    [VFS_SYSCALL_MOUNT_CHECK] = vfs_syscall_mount_check,
    [VFS_SYSCALL_MOUNT] = vfs_syscall_mount,
    [VFS_SYSCALL_UNMOUNT] = vfs_syscall_unmount,
    [VFS_SYSCALL_CHANGE_DRIVE] = vfs_syscall_change_drive,
    [VFS_SYSCALL_FORMAT] = vfs_syscall_format,
    [VFS_SYSCALL_TRUNCATE] = vfs_syscall_truncate,
    [VFS_SYSCALL_REALPATH] = vfs_syscall_realpath,
    [VFS_SYSCALL_PREAD] = vfs_syscall_pread,
    [VFS_SYSCALL_FCHDIR] = vfs_syscall_fchdir,
};

static void syscall_vfs(syscall_context_t *frame) {
  if (frame->argument0 >= VFS_SYSCALL_COUNT ||
      vfs_syscall_handlers[frame->argument0] == NULL ||
      !user_range_ok(frame->argument1, sizeof(vfs_syscall_request_t))) {
    frame->value = VFS_ERROR_INVALID;
    return;
  }
  vfs_syscall_request_t request;
  if (!user_vm_copy_from(&request, frame->argument1, sizeof(request)) ||
      request.size != sizeof(request)) {
    frame->value = VFS_ERROR_INVALID;
    return;
  }
  frame->value = vfs_syscall_handlers[frame->argument0](&request);
}

static void syscall_command_line(syscall_context_t *frame) {
  mtask *task = current_task();
  if (task->line == NULL) {
    frame->value = -1;
    return;
  }
  size_t length = strlen((const char *)task->line);
  if (length >= INT_MAX || (frame->argument2 == 0 && frame->argument1 != 0) ||
      (frame->argument2 != 0 &&
       !user_range_ok(frame->argument2, frame->argument1))) {
    frame->value = -1;
    return;
  }
  if (frame->argument2 == 0) {
    frame->value = length;
    return;
  }
  if (frame->argument1 <= length) {
    frame->value = -2;
    return;
  }
  memcpy((void *)(uintptr_t)frame->argument2, (const void *)task->line, length + 1);
  frame->value = length;
}

static void syscall_keyboard_hit(syscall_context_t *frame) {
  frame->value = kbhit();
}

static unsigned syscall_exit_status(mtask *task, unsigned status) {
  if (task && task->return_cwd) {
    mtask *parent = task->ptid == 0 || task->ptid == (uint32_t)-1
                        ? NULL
                        : get_task(task->ptid);
    if (parent && parent->kind == TASK_PROCESS && parent->state != DIED) {
      if (!vfs_context_transfer_cwd(task->fs_context, parent->fs_context)) {
        WARNING_K("failed to transfer child VFS state");
        status = (unsigned)-1;
      }
    }
  }
  return status;
}

static void syscall_exit(syscall_context_t *frame) {
  task_exit(syscall_exit_status(current_task(), frame->argument0));
}

static void syscall_video_control(syscall_context_t *frame) {
  frame->value = (syscall_word_t)-1;
  if (frame->argument0 == 6) {
    platform_video_info_t video;
    if (!user_range_ok(frame->argument1, sizeof(framebuffer_info_t)) ||
        !platform_video_current_info(&video))
      return;
#if defined(KERNEL_ARCH_X86_64)
    if (!x64_user_access(frame->argument1, sizeof(framebuffer_info_t), true))
      return;
    uintptr_t address =
        USER_FRAMEBUFFER_START + (video.physical_address & 4095);
#else
    uintptr_t address = video.framebuffer;
#endif
    *(framebuffer_info_t *)frame->argument1 = (framebuffer_info_t){
        address, video.width,     video.height,      video.pitch,
        32,      video.red_shift, video.green_shift, video.blue_shift,
        0};
    frame->value = 0;
    return;
  }
#if defined(KERNEL_ARCH_I386)
  if (running_mode != POWERINTDOS || !task_pin_current(0))
    return;
  if (frame->argument0 == 1) {
    frame->value = platform_video_switch_mode(frame->argument1);
    return;
  }
  if (frame->argument0 == 2) {
    frame->value = platform_video_check_mode(frame->argument1);
    return;
  }
#endif
  if (frame->argument0 != 5)
    return;
  platform_video_info_t info;
  if (!platform_video_set_mode(frame->argument1, frame->argument2, 32, &info))
    return;
  size_t bytes = (size_t)info.pitch * info.height;
  uintptr_t physical = info.physical_address;
#if defined(KERNEL_ARCH_X86_64)
  uintptr_t user = USER_FRAMEBUFFER_START;
#else
  uintptr_t user = physical & ~(uintptr_t)4095;
#endif
  size_t displacement = physical & 4095;
  if (!bytes || bytes > (size_t)-1 - displacement - 4095)
    return;
  size_t size = (bytes + displacement + 4095) & ~(size_t)4095;
  if (arch_address_space_map_user_device(user, physical & ~(uintptr_t)4095,
                                         size)) {
    frame->value = user + displacement;
  }
}

static void syscall_bios_video(syscall_context_t *frame) {
#if defined(KERNEL_ARCH_I386)
  frame->value = (syscall_word_t)-1;
  if (running_mode != POWERINTDOS || !task_pin_current(0)) {
    return;
  }
  if (frame->argument0 == 0x01) {
    platform_video_text_mode();
  } else if (frame->argument0 == 0x02) {
    platform_video_graphics_mode();
  } else {
    return;
  }
  frame->value = 0;
#else
  frame->value = (syscall_word_t)-1;
#endif
}

static void syscall_task_control(syscall_context_t *frame) {
  mtask *task = current_task();
  switch (frame->argument0) {
  case 0x04:
    send_ipc_message(frame->argument1, (void *)(uintptr_t)frame->argument2, frame->argument3,
                     asynchronous);
    break;
  case 0x05:
    get_ipc_message((void *)(uintptr_t)frame->argument2, frame->argument1);
    break;
  case 0x06:
    frame->value = ipc_message_len(frame->argument1);
    break;
  case 0x07:
    frame->value = get_tid(task);
    break;
  case 0x0e:
    frame->value = task->tgid;
    break;
  case 0x0f:
    frame->value = task->ptid;
    break;
  case 0x08:
    frame->value = have_msg();
    break;
  case 0x09:
    get_msg_all((void *)(uintptr_t)frame->argument2);
    break;
  case 0x0b:
    task_lock();
    break;
  case 0x0c:
    task_unlock();
    break;
  case 0x0d: {
    mtask *target = get_task(frame->argument1);
    if (target && target->kind == TASK_THREAD && target->tgid == task->tgid) {
      task_kill(frame->argument1);
    }
    break;
  }
  }
}

static void syscall_tty_color(syscall_context_t *frame) {
  struct tty *tty = current_task()->TTY;
  frame->value = (syscall_word_t)-1;
  if (tty == NULL)
    return;
  if (frame->argument0 == 0x01) {
    frame->value = tty->color;
  } else if (frame->argument0 == 0x02) {
    tty_set_color(tty, frame->argument1);
    frame->value = 0;
  }
}

static void syscall_timer_control(syscall_context_t *frame) {
  mtask *task = current_task();
  frame->value = -1;
  switch (frame->argument0) {
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
    frame->value = 0;
    break;
  }
  case 0x01:
    if (task->timer == NULL) {
      break;
    }
    timer_settime(task->timer, frame->argument1);
    frame->value = 0;
    break;
  case 0x02:
    if (task->timer == NULL || task->timer->fifo == NULL) {
      break;
    }
    frame->value = fifo8_status(task->timer->fifo) != 0 &&
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
    frame->value = 0;
    break;
  }
}

static void syscall_rtc(syscall_context_t *frame) {
  switch (frame->argument0) {
  case 0x00:
    frame->value = get_hour_hex();
    break;
  case 0x01:
    frame->value = get_min_hex();
    break;
  case 0x02:
    frame->value = get_sec_hex();
    break;
  case 0x03:
    frame->value = get_day_of_month();
    break;
  case 0x04:
    frame->value = get_day_of_week();
    break;
  case 0x05:
    frame->value = get_mon_hex();
    break;
  case 0x06:
    frame->value = get_year();
    break;
  }
}

static void syscall_framebuffer(syscall_context_t *frame) {
  if (running_mode != POWERINTDOS) {
    return;
  }

  platform_video_info_t info;
  if (!platform_video_current_info(&info)) {
    frame->value = UINT_MAX;
    return;
  }
  vram_t *framebuffer = (vram_t *)info.framebuffer;
  switch (frame->value) {
  case SYSCALL_DRAW_PIXEL:
    SDraw_Px(framebuffer, frame->argument0, frame->argument1, frame->argument2,
             info.width);
    break;
  case SYSCALL_READ_PIXEL:
    frame->value = framebuffer[frame->argument0 * info.width + frame->argument1];
    break;
  case SYSCALL_COPY_FRAMEBUFFER:
    memcpy((void *)(uintptr_t)frame->argument0, framebuffer,
           info.width * info.height * sizeof(vram_t));
    break;
  case SYSCALL_DRAW_BUFFER: {
    int x = frame->argument0;
    int y = frame->argument1;
    int width = frame->argument2;
    int height = frame->argument3;
    unsigned *buffer = (unsigned *)(uintptr_t)frame->argument4;
    for (int i = x; i < x + width; i++) {
      for (int j = y; j < y + height; j++) {
        framebuffer[j * info.width + i] =
            buffer[(j - y) * width + (i - x)];
      }
    }
    break;
  }
  case SYSCALL_SCROLL_FRAMEBUFFER: {
    int destination_row = 0;
    int source_row = frame->argument0;
    for (; source_row < info.height; source_row++, destination_row++) {
      for (int x = 0; x < info.width; x++) {
        framebuffer[destination_row * info.width + x] =
            framebuffer[source_row * info.width + x];
      }
    }
    SDraw_Box(framebuffer, 0, destination_row, info.width, info.height, 0,
              info.width);
    break;
  }
  case SYSCALL_DRAW_BOX:
    SDraw_Box(framebuffer, frame->argument0, frame->argument1, frame->argument2,
              frame->argument3, frame->argument4, info.width);
    break;
  }
}

static void syscall_timestamp(syscall_context_t *frame) {
  uint32_t timestamp;
  frame->value = platform_rtc_timestamp(&timestamp) ? timestamp : UINT32_MAX;
}

static void syscall_uptime(syscall_context_t *frame) {
  frame->value = timerctl.count * 10;
}

static void syscall_monotonic_ns(syscall_context_t *frame) {
  uint64_t nanoseconds = monotonic_time_ns();
#if defined(KERNEL_ARCH_X86_64)
  frame->value = nanoseconds;
#else
  frame->value = (uint32_t)nanoseconds;
#endif
  frame->argument2 = (uint32_t)(nanoseconds >> 32);
}

static void syscall_reset_fpu(syscall_context_t *frame) {
  (void)frame;
  arch_fpu_reset(current_task());
}

static void syscall_keyboard_setup(syscall_context_t *frame) {
  mtask *task = current_task();
  if (task->Pkeyfifo != NULL || task->Ukeyfifo != NULL) {
    frame->value = -1;
    return;
  }
  struct FIFO8 *press_fifo = malloc(sizeof(*press_fifo));
  struct FIFO8 *release_fifo = malloc(sizeof(*release_fifo));
  unsigned char *press_buffer = page_malloc(4096);
  unsigned char *release_buffer = page_malloc(4096);
  if (press_fifo == NULL || release_fifo == NULL || press_buffer == NULL ||
      release_buffer == NULL) {
    if (press_buffer != NULL) {
      page_free(press_buffer, 4096);
    }
    if (release_buffer != NULL) {
      page_free(release_buffer, 4096);
    }
    free(release_fifo);
    free(press_fifo);
    frame->value = -1;
    return;
  }
  fifo8_init(press_fifo, 4096, press_buffer);
  fifo8_init(release_fifo, 4096, release_buffer);
  task->Pkeyfifo = press_fifo;
  task->Ukeyfifo = release_fifo;
  task->keyboard_press = keyboard_press;
  task->keyboard_release = keyboard_release;
  frame->value = 0;
}

static void syscall_keyboard_fifo(syscall_context_t *frame) {
  mtask *task = current_task();
  switch (frame->value) {
  case SYSCALL_KEY_PRESS_PENDING:
    frame->value = fifo8_status(task->Pkeyfifo);
    break;
  case SYSCALL_KEY_RELEASE_PENDING:
    frame->value = fifo8_status(task->Ukeyfifo);
    break;
  case SYSCALL_KEY_PRESS_READ:
    frame->value = fifo8_get(task->Pkeyfifo);
    break;
  case SYSCALL_KEY_RELEASE_READ:
    frame->value = fifo8_get(task->Ukeyfifo);
    break;
  }
}

static void syscall_grow_heap(syscall_context_t *frame) {
  mtask *task = current_task();
  size_t old_size = task->alloc_size ? *task->alloc_size : 0;
  uintptr_t start_addr = task->alloc_addr + old_size;
  if (task->alloc_size == NULL || frame->argument0 == 0 ||
      frame->argument0 > INT_MAX) {
    frame->value = -1;
    return;
  }
  size_t requested = (size_t)frame->argument0;
  size_t request = (requested + 0xfffu) & ~(size_t)0xfffu;
  if (request < requested || start_addr < task->alloc_addr ||
      start_addr >= USER_HEAP_END || request > USER_HEAP_END - start_addr ||
      !user_vm_range_free(start_addr, request)) {
    frame->value = -1;
    return;
  }

  for (uintptr_t offset = 0; offset < request; offset += 0x1000u) {
    if (!page_link(start_addr + offset)) {
      if (offset)
        arch_user_unmap(start_addr, offset);
      frame->value = -1;
      return;
    }
  }
  *task->alloc_size = old_size + request;
  frame->value = 0;
}

static void syscall_virtual_memory(syscall_context_t *frame) {
  uintptr_t address = frame->argument1;
  if (frame->argument0 >= VM_OPERATION_COUNT ||
      frame->argument2 != sizeof(vm_request_t) || address < USER_SPACE_START ||
      address >= USER_HEAP_END ||
      sizeof(vm_request_t) > USER_HEAP_END - address) {
    frame->value = VM_ERROR_INVALID;
    return;
  }
  vm_request_t request;
  if (!user_vm_copy_from(&request, address, sizeof(request))) {
    frame->value = VM_ERROR_FAULT;
    return;
  }
  user_vm_lock();
  frame->value = user_vm_operation(frame->argument0, &request);
  user_vm_unlock();
}

static void syscall_futex(syscall_context_t *frame) {
  if (frame->argument0 >= FUTEX_OPERATION_COUNT ||
      frame->argument2 != sizeof(futex_request_t)) {
    frame->value = FUTEX_INVALID;
    return;
  }
  futex_request_t request;
  if (!user_vm_copy_from(&request, frame->argument1, sizeof(request))) {
    frame->value = FUTEX_FAULT;
    return;
  }
  frame->value = futex_operation(frame->argument0, &request);
}

static void syscall_thread(syscall_context_t *frame) {
  if (frame->argument0 == THREAD_EXIT_PROCESS)
    task_exit_process(syscall_exit_status(get_task(current_task()->tgid),
                                          (unsigned)frame->argument1));
  if (frame->argument0 == THREAD_WAIT_GROUP) {
    irq_state_t state = irq_save();
    task_wait_threads();
    frame->value = 0;
    irq_restore(state);
    return;
  }
  if (frame->argument0 == THREAD_GET_POINTER) {
    frame->value = current_task()->thread_pointer;
    return;
  }
  if (frame->argument0 >= THREAD_OPERATION_COUNT ||
      frame->argument2 != sizeof(native_thread_request_t)) {
    frame->value = -22;
    return;
  }
  irq_state_t state = irq_save();
  native_thread_request_t request;
  if (!user_vm_copy_from(&request, frame->argument1, sizeof(request)) ||
      ((frame->argument0 == THREAD_CREATE ||
        frame->argument0 == THREAD_GET_STACK) &&
       !user_vm_prepare_write(frame->argument1, sizeof(request)))) {
    frame->value = -14;
    irq_restore(state);
    return;
  }
  switch (frame->argument0) {
  case THREAD_CREATE:
    frame->value = user_thread_create(&request);
    if (!frame->value)
      memcpy((void *)frame->argument1, &request, sizeof(request));
    break;
  case THREAD_SET_POINTER:
    frame->value = user_thread_set_pointer(&request);
    break;
  case THREAD_JOIN:
    frame->value = task_join_thread(request.tid, request.generation);
    break;
  case THREAD_DETACH:
    frame->value = task_detach_thread(request.tid, request.generation);
    break;
  case THREAD_TERMINATE:
    frame->value = task_terminate_thread(request.tid, request.generation);
    break;
  case THREAD_GET_STACK:
    frame->value = user_thread_get_stack(&request);
    if (!frame->value)
      memcpy((void *)frame->argument1, &request, sizeof(request));
    break;
  }
  irq_restore(state);
}

static void syscall_read_env(syscall_context_t *frame) {
  irq_state_t state = irq_save();
  size_t name_size = frame->argument1;
  if (!name_size || !user_vm_readable(frame->argument0, name_size) ||
      ((const char *)frame->argument0)[name_size - 1]) {
    frame->value = -14;
    irq_restore(state);
    return;
  }
  const char *value = env_read((char *)frame->argument0);
  if (!value) {
    frame->value = -2;
    irq_restore(state);
    return;
  }
  size_t length = strlen(value);
  frame->value = length;
  if (frame->argument3) {
    if (frame->argument3 <= length)
      frame->value = -75;
    else if (!user_vm_prepare_write(frame->argument2, length + 1))
      frame->value = -14;
    else
      memcpy((void *)frame->argument2, value, length + 1);
  }
  irq_restore(state);
}

static void syscall_execute(syscall_context_t *frame) {
  frame->value = os_execute((char *)(uintptr_t)frame->argument0,
                          (char *)(uintptr_t)frame->argument1, EXECUTE_PROGRAM);
}

static void syscall_clear(syscall_context_t *frame) {
  (void)frame;
  clear();
}

static void syscall_memory_info(syscall_context_t *frame) {
  if (frame->value == SYSCALL_MEMORY_SIZE) {
    frame->value = memsize;
    return;
  }

  frame->value = page_used_count(memsize);
}

static void syscall_tty_cursor(syscall_context_t *frame) {
  if (frame->value == SYSCALL_CURSOR_START) {
    tty_start_curor_moving(current_task()->TTY);
  } else {
    tty_stop_cursor_moving(current_task()->TTY);
  }
}

static void syscall_tty_size(syscall_context_t *frame) {
  if (frame->value == SYSCALL_TTY_WIDTH) {
    frame->value = current_task()->TTY->xsize;
  } else {
    frame->value = current_task()->TTY->ysize;
  }
}

static void syscall_log(syscall_context_t *frame) {
  logk((char *)(uintptr_t)frame->argument0);
}

static void syscall_signal_handler(syscall_context_t *frame) {
  if (frame->argument0 >= SIGNAL_OPERATION_COUNT) {
    frame->value = -22;
    return;
  }
  frame->value = user_signal_operation(frame->argument0, frame->argument1,
                                       frame->argument2, frame->argument3);
}

static void syscall_fork(syscall_context_t *frame) {
  frame->value = task_fork();
}

static void syscall_wait(syscall_context_t *frame) {
  frame->value = waittid(frame->argument0);
}

static void syscall_mouse_enable(syscall_context_t *frame) {

  if (mouse_use_task != NULL && mouse_use_task != current_task()) {
    frame->value = -1;
    return;
  }
  mouse_ready();
  frame->value = 0;
}

static void syscall_mouse_data(syscall_context_t *frame) {
  struct FIFO8 *fifo = current_task()->mousefifo;
  if (frame->value == SYSCALL_MOUSE_PENDING) {
    frame->value =
        fifo == NULL ? 0 : fifo8_status(fifo) / sizeof(mouse_event_t);
    return;
  }
  if (!user_range_ok(frame->argument0, sizeof(mouse_event_t))) {
    frame->value = -1;
    return;
  }
  mouse_event_t event;
  frame->value = input_mouse_read(&event);
  if (frame->value != 0) {
    memcpy((void *)frame->argument0, &event, sizeof(event));
  }
}

static void syscall_yield(syscall_context_t *frame) {
  (void)frame;
  irq_state_t state = irq_save();
  if (current_task()->ready == 0) {
    task_next();
  } else {
    current_task()->ready = 0;
  }
  irq_restore(state);
}

static void syscall_tty_object(syscall_context_t *frame) {
  switch (frame->value) {
  case SYSCALL_TTY_ALLOC: {
    struct tty *tty = NULL;
    if (frame->argument0 <= UINT_MAX && frame->argument1 <= UINT_MAX &&
        frame->argument2 <= INT_MAX && frame->argument3 <= INT_MAX) {
      tty = fartty_alloc(get_task(frame->argument0), frame->argument1,
                         frame->argument2, frame->argument3);
    }
    frame->value = tty != NULL ? tty->remote.handle : 0;
    break;
  }
  case SYSCALL_TTY_SET: {
    extern struct tty *tty_default;
    mtask *task =
        frame->argument0 <= UINT_MAX ? get_task(frame->argument0) : NULL;
    struct tty *tty =
        frame->argument1 == 0 ? tty_default : fartty_lookup(frame->argument1);
    frame->value = -1;
    if (task != NULL && task->state != DIED && !task->terminate_pending &&
        task->tgid == current_task()->tgid && tty != NULL) {
      tty_set(task, tty);
      frame->value = 0;
    }
    break;
  }
  case SYSCALL_TTY_FREE: {
    struct tty *tty = fartty_lookup(frame->argument0);
    frame->value = tty != NULL ? 0 : -1;
    if (tty != NULL)
      tty_free(tty);
    break;
  }
  }
}

static void syscall_return_to_app(syscall_context_t *frame) {
  get_task(current_task()->tgid)->ret_to_app = frame->argument0;
}

static void syscall_tty_pointer(syscall_context_t *frame) {
  tty_pointer_t pointer;
  frame->value = -1;
  if (!user_range_ok(frame->argument0, sizeof(pointer)))
    return;
  if (fartty_get_pointer(current_task()->TTY, &pointer) == 0 &&
      user_vm_prepare_write(frame->argument0, sizeof(pointer))) {
    memcpy((void *)frame->argument0, &pointer, sizeof(pointer));
    frame->value = 0;
  }
}

static void syscall_use_keyboard(syscall_context_t *frame) {

  if (keyboard_use_task != NULL && keyboard_use_task != current_task()) {
    frame->value = -1;
    return;
  }

  keyboard_use_task = current_task();
  frame->value = 0;
}

enum input_wait_event {
  INPUT_WAIT_MOUSE = 1u << 0,
  INPUT_WAIT_KEY_PRESS = 1u << 1,
  INPUT_WAIT_KEY_RELEASE = 1u << 2,
  INPUT_WAIT_ALL = INPUT_WAIT_MOUSE | INPUT_WAIT_KEY_PRESS |
                   INPUT_WAIT_KEY_RELEASE,
};

static uint32_t input_pending_events(const mtask *task, uint32_t requested) {
  uint32_t pending = 0;
  if ((requested & INPUT_WAIT_MOUSE) && task->mousefifo != NULL &&
      fifo8_status(task->mousefifo) >= (int)sizeof(mouse_event_t)) {
    pending |= INPUT_WAIT_MOUSE;
  }
  if ((requested & INPUT_WAIT_KEY_PRESS) && task->Pkeyfifo != NULL &&
      fifo8_status(task->Pkeyfifo) != 0) {
    pending |= INPUT_WAIT_KEY_PRESS;
  }
  if ((requested & INPUT_WAIT_KEY_RELEASE) && task->Ukeyfifo != NULL &&
      fifo8_status(task->Ukeyfifo) != 0) {
    pending |= INPUT_WAIT_KEY_RELEASE;
  }
  return pending;
}

static void syscall_input_wait(syscall_context_t *frame) {

  uint32_t requested = frame->argument0;
  mtask *task = current_task();
  if (requested == 0 || (requested & ~INPUT_WAIT_ALL) != 0 ||
      ((requested & INPUT_WAIT_MOUSE) && mouse_use_task != task) ||
      ((requested & (INPUT_WAIT_KEY_PRESS | INPUT_WAIT_KEY_RELEASE)) &&
       keyboard_use_task != task) ||
      ((requested & INPUT_WAIT_MOUSE) && task->mousefifo == NULL) ||
      ((requested & INPUT_WAIT_KEY_PRESS) && task->Pkeyfifo == NULL) ||
      ((requested & INPUT_WAIT_KEY_RELEASE) && task->Ukeyfifo == NULL)) {
    frame->value = -1;
    return;
  }

  for (;;) {
    irq_state_t interrupt_state = irq_save();
    uint32_t pending = input_pending_events(task, requested);
    if (pending != 0) {
      frame->value = pending;
      irq_restore(interrupt_state);
      return;
    }
    task_fall_blocked_reason(WAITING, WAIT_REASON_INPUT);
    irq_restore(interrupt_state);
  }
}

enum shared_memory_operation {
  SHARED_MEMORY_MAP_TO = 0x01,
  SHARED_MEMORY_UNMAP = 0x02,
};

static bool shared_memory_range_ok(uintptr_t address, size_t size,
                                   uintptr_t lower, uintptr_t upper) {
  if (size == 0 || size > upper - lower ||
      ((address | size) & 0xfffu) != 0 || address < lower) {
    return false;
  }
  return address <= upper - size;
}

static void syscall_shared_memory(syscall_context_t *frame) {
  bool success = false;
  if (frame->argument0 == SHARED_MEMORY_MAP_TO) {
    if (!shared_memory_range_ok(frame->argument4, frame->argument5, USER_SPACE_START,
                                USER_HEAP_END) ||
        !shared_memory_range_ok(frame->argument3, frame->argument5, USER_HEAP_END,
                                USER_SHARED_END)) {
      frame->value = -1;
      return;
    }

    irq_state_t state = irq_save();
    mtask *target = get_task(frame->argument1);
    if (target != NULL && target->state != DIED && !target->on_cpu &&
        target->generation == frame->argument2 && target->address_space != 0) {
      success = arch_address_space_share(frame->argument4, frame->argument3, frame->argument5,
                                     current_task()->address_space, target->address_space);
    }
    irq_restore(state);
  } else if (frame->argument0 == SHARED_MEMORY_UNMAP) {
    if (!shared_memory_range_ok(frame->argument3, frame->argument5, USER_HEAP_END,
                                USER_SHARED_END)) {
      frame->value = -1;
      return;
    }
    irq_state_t state = irq_save();
    success = arch_address_space_unmap_shared(frame->argument3, frame->argument5,
                                           current_task()->address_space);
    irq_restore(state);
  }
  frame->value = success ? 0 : -1;
}

static void syscall_task_level(syscall_context_t *frame) {
  irq_state_t state = irq_save();
  mtask *task = get_task(frame->argument0);
  if (task == NULL) {
    irq_restore(state);
    return;
  }

  if (frame->value == SYSCALL_TASK_LEVEL_HIGH) {
    if (task->urgent) {
      irq_restore(state);
      return;
    }
    task->urgent = 1;
    task_set_weight(task, 5);
  } else {
    task->urgent = 0;
    task_set_weight(task, 1);
  }
  irq_restore(state);
}

static void syscall_module(syscall_context_t *frame) {
  switch (frame->value) {
  case SYSCALL_MODULE_LOAD:
    frame->value = module_load((const char *)(uintptr_t)frame->argument0);
    break;
  case SYSCALL_MODULE_UNLOAD:
    frame->value = module_unload((const char *)(uintptr_t)frame->argument0);
    break;
  case SYSCALL_MODULE_LIST:
    frame->value = module_list((module_handle_t *)(uintptr_t)frame->argument0,
                             frame->argument1);
    break;
  }
}

static void syscall_ipc(syscall_context_t *frame) {
  frame->value = ipc_syscall_dispatch(frame->argument0, frame->argument1, frame->argument2);
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

static int socket_parse_user_address(uintptr_t pointer, uint32_t length,
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
  uint8_t value[NET_SOCKET_OPTION_VALUE_MAX];
  if (request->length == 0 || request->length > sizeof(value) ||
      !user_range_ok(request->buffer, request->length) ||
      !user_vm_copy_from(value, request->buffer, request->length)) {
    return NET_SOCKET_ERR_INVAL;
  }
  return net_socket_set_option(owner_group, request->socket, request->domain,
                               request->type, value, request->length);
}

static int socket_syscall_available(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  return net_socket_bytes_available(owner_group, request->socket,
                                    &request->length);
}

static int socket_syscall_get_option(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  uint8_t value[NET_SOCKET_OPTION_VALUE_MAX];
  if (request->length == 0 || request->length > sizeof(value) ||
      !user_range_ok(request->buffer, request->length)) {
    return NET_SOCKET_ERR_INVAL;
  }
  uint32_t length = request->length;
  int result = net_socket_get_option(owner_group, request->socket,
                                     request->domain, request->type, value,
                                     &length);
  if (result == 0 && !user_vm_copy_to(request->buffer, value, length)) {
    return NET_SOCKET_ERR_INVAL;
  }
  request->length = length;
  return result;
}

static int socket_syscall_shutdown(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  return net_socket_shutdown(owner_group, request->socket, request->type);
}

static int socket_syscall_pair(uint32_t owner_group,
                               net_socket_syscall_request_t *request) {
  if (!user_range_ok(request->buffer, sizeof(int32_t) * 2)) {
    return NET_SOCKET_ERR_INVAL;
  }
  int handles[2];
  int result = net_socket_pair(owner_group, request->domain, request->type,
                              request->protocol, handles);
  if (result == 0 && !user_vm_copy_to(request->buffer, handles,
                                      sizeof(handles))) {
    (void)net_socket_close(owner_group, handles[0]);
    (void)net_socket_close(owner_group, handles[1]);
    return NET_SOCKET_ERR_INVAL;
  }
  return result;
}

static int socket_syscall_get_flags(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  int result = net_socket_get_flags(owner_group, request->socket);
  if (result >= 0) {
    request->flags = (uint32_t)result;
  }
  return result;
}

static int socket_syscall_set_flags(
    uint32_t owner_group, net_socket_syscall_request_t *request) {
  return net_socket_set_flags(owner_group, request->socket,
                              (int)request->flags);
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
        [NET_SOCKET_SYSCALL_AVAILABLE] = socket_syscall_available,
        [NET_SOCKET_SYSCALL_GET_OPTION] = socket_syscall_get_option,
        [NET_SOCKET_SYSCALL_SHUTDOWN] = socket_syscall_shutdown,
        [NET_SOCKET_SYSCALL_PAIR] = socket_syscall_pair,
        [NET_SOCKET_SYSCALL_GET_FLAGS] = socket_syscall_get_flags,
        [NET_SOCKET_SYSCALL_SET_FLAGS] = socket_syscall_set_flags,
};

static void syscall_socket(syscall_context_t *frame) {
  if (frame->argument0 >= NET_SOCKET_SYSCALL_COUNT ||
      socket_syscall_handlers[frame->argument0] == NULL ||
      !user_range_ok(frame->argument1, sizeof(net_socket_syscall_request_t))) {
    frame->value = NET_SOCKET_ERR_INVAL;
    return;
  }
  net_socket_syscall_request_t *request =
      (net_socket_syscall_request_t *)(uintptr_t)frame->argument1;
  frame->value = socket_syscall_handlers[frame->argument0](current_task()->tgid,
                                                    request);
}

static void syscall_task_snapshot(syscall_context_t *frame) {
  if (!user_range_ok(frame->argument2, sizeof(uint32_t)) ||
      (frame->argument1 != 0 &&
       (frame->argument1 > UINT_MAX / sizeof(task_info_t) ||
        !user_range_ok(frame->argument0, frame->argument1 * sizeof(task_info_t))))) {
    frame->value = -1;
    return;
  }
  frame->value = task_snapshot((task_info_t *)(uintptr_t)frame->argument0,
                             (uint32_t)frame->argument1,
                             (uint32_t *)(uintptr_t)frame->argument2);
}

static void syscall_cpu_info(syscall_context_t *frame) {
  frame->value = smp_online_cpu_count();
  frame->argument2 = smp_current_cpu();
}

static void syscall_tty_input_notify(syscall_context_t *frame) {
  frame->value = tty_notify_input(fartty_lookup(frame->argument0)) ? 0 : -1;
}

static void syscall_perf_control(syscall_context_t *frame) {
  if (!user_range_ok(frame->argument0, sizeof(perf_control_request_t))) {
    frame->value = PERF_ERR_INVALID;
    return;
  }

  perf_control_request_t *request =
      (perf_control_request_t *)(uintptr_t)frame->argument0;
  if (request->size != sizeof(*request) ||
      request->operation >= PERF_CONTROL_COUNT) {
    frame->value = PERF_ERR_INVALID;
    return;
  }

  switch ((perf_control_operation_t)request->operation) {
  case PERF_CONTROL_START:
    frame->value = perf_start(PERF_SESSION_MANUAL);
    break;
  case PERF_CONTROL_STOP:
    frame->value = perf_stop_and_dump("perf-stop");
    break;
  case PERF_CONTROL_STATUS:
    frame->value = PERF_OK;
    break;
  default:
    frame->value = PERF_ERR_INVALID;
    break;
  }
  perf_get_status(&request->status);
}

static void syscall_power_off(syscall_context_t *frame) {
  frame->value = platform_power_off();
}

static const syscall_handler_t syscall_handlers[SYSCALL_COUNT] = {
    [SYSCALL_POWER_OFF] = syscall_power_off,
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
    [SYSCALL_VFS] = syscall_vfs,
    [SYSCALL_COMMAND_LINE] = syscall_command_line,
    [SYSCALL_KEYBOARD_HIT] = syscall_keyboard_hit,
    [SYSCALL_EXIT] = syscall_exit,
    [SYSCALL_VIDEO_CONTROL] = syscall_video_control,
    [SYSCALL_BIOS_VIDEO] = syscall_bios_video,
    [SYSCALL_TASK_CONTROL] = syscall_task_control,
    [SYSCALL_TTY_COLOR] = syscall_tty_color,
    [SYSCALL_TIMER_CONTROL] = syscall_timer_control,
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
    [SYSCALL_EXECUTE] = syscall_execute,
    [SYSCALL_CLEAR] = syscall_clear,
    [SYSCALL_MEMORY_SIZE] = syscall_memory_info,
    [SYSCALL_USED_PAGES] = syscall_memory_info,
    [SYSCALL_CURSOR_START] = syscall_tty_cursor,
    [SYSCALL_CURSOR_STOP] = syscall_tty_cursor,
    [SYSCALL_TTY_WIDTH] = syscall_tty_size,
    [SYSCALL_TTY_HEIGHT] = syscall_tty_size,
    [SYSCALL_LOG] = syscall_log,
    [SYSCALL_SIGNAL_HANDLER] = syscall_signal_handler,
    [SYSCALL_FORK] = syscall_fork,
    [SYSCALL_WAIT] = syscall_wait,
    [SYSCALL_MOUSE_ENABLE] = syscall_mouse_enable,
    [SYSCALL_MOUSE_PENDING] = syscall_mouse_data,
    [SYSCALL_MOUSE_READ] = syscall_mouse_data,
    [SYSCALL_YIELD] = syscall_yield,
    [SYSCALL_TTY_ALLOC] = syscall_tty_object,
    [SYSCALL_TTY_SET] = syscall_tty_object,
    [SYSCALL_TTY_FREE] = syscall_tty_object,
    [SYSCALL_TTY_POINTER] = syscall_tty_pointer,
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
    [SYSCALL_TASK_SNAPSHOT] = syscall_task_snapshot,
    [SYSCALL_CPU_INFO] = syscall_cpu_info,
    [SYSCALL_TTY_INPUT_NOTIFY] = syscall_tty_input_notify,
    [SYSCALL_PERF_CONTROL] = syscall_perf_control,
    [SYSCALL_INPUT_WAIT] = syscall_input_wait,
    [SYSCALL_USER_FUTEX] = syscall_futex,
    [SYSCALL_NATIVE_THREAD] = syscall_thread,
    [SYSCALL_VIRTUAL_MEMORY] = syscall_virtual_memory,
};

void syscall_dispatch(syscall_context_t *frame) {
  if (frame->value >= SYSCALL_COUNT || syscall_handlers[frame->value] == NULL) {
    frame->value = (syscall_word_t)-1;
    return;
  }
  syscall_handlers[frame->value](frame);
}
