#include <framebuffer.h>
#include <gui.h>
#include <os_terminal.h>
#include <rpc.h>
#include <runtime_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>
#include <tty_rpc.h>

enum {
  BORDER = 4,
  TITLE_HEIGHT = 24,
  DEFAULT_WIDTH = 640,
  DEFAULT_HEIGHT = 400,
  FRAME_MS = 16,
};

/* Only track control-string boundaries for safe cursor reports. All bytes,
 * including incomplete sequences, are interpreted and rendered by os-terminal.
 */
typedef enum {
  STREAM_TEXT,
  STREAM_ESCAPE,
  STREAM_ESCAPE_INTERMEDIATE,
  STREAM_CSI,
  STREAM_OSC,
  STREAM_STRING,
} stream_state_t;

static struct {
  window_t window;
  void *emulator;
  tty_t tty;
  tty_rpc_state_t state;
  int width, height, columns, rows;
  stream_state_t stream;
  unsigned utf8_remaining;
  bool dirty, shell_finished;
  char *program, *command;
  int shell_status;
} term;

static void *terminal_allocate(size_t size) {
  void *memory = malloc(size);
  if (memory)
    return memory;
  // The library cannot unwind an allocation failure. Retire the session
  // before exiting so its panic handler never leaves a stalled GUI window.
  tty_t tty = term.tty;
  term.tty = 0;
  if (tty)
    tty_free(tty);
  close_window(term.window);
  logkf("TERM: out of memory\n");
  _exit(1);
  return NULL;
}

static void cursor_report(const uint8_t *data, size_t length) {
  char report[32];
  if (length >= sizeof(report))
    return;
  memcpy(report, data, length);
  report[length] = 0;
  unsigned row, column;
  int consumed = 0;
  if (sscanf(report, "\033[%u;%uR%n", &row, &column, &consumed) == 2 &&
      consumed == length && row > 0 && row <= (unsigned)term.rows &&
      column > 0 && column <= (unsigned)term.columns + 1) {
    term.state.x = column - 1;
    term.state.y = row - 1;
  }
}

static void terminal_write(const void *data, size_t length) {
  const uint8_t *bytes = data;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = bytes[i];
    if (term.utf8_remaining) {
      if ((byte & 0xc0) == 0x80) {
        term.utf8_remaining--;
        continue;
      }
      term.utf8_remaining = 0;
    }
    if (byte >= 0xc2 && byte <= 0xf4) {
      term.utf8_remaining = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
    } else if (byte == '\033') {
      term.stream = STREAM_ESCAPE;
    } else if (byte == 0x18 || byte == 0x1a) {
      term.stream = STREAM_TEXT;
    } else if (term.stream == STREAM_ESCAPE) {
      switch (byte) {
      case '[':
        term.stream = STREAM_CSI;
        break;
      case ']':
        term.stream = STREAM_OSC;
        break;
      case 'P':
      case 'X':
      case '^':
      case '_':
        term.stream = STREAM_STRING;
        break;
      default:
        if (byte >= 0x20 && byte <= 0x2f)
          term.stream = STREAM_ESCAPE_INTERMEDIATE;
        else if (byte >= 0x30 && byte <= 0x7e)
          term.stream = STREAM_TEXT;
        break;
      }
    } else if ((term.stream == STREAM_ESCAPE_INTERMEDIATE && byte >= 0x30 &&
                byte <= 0x7e) ||
               (term.stream == STREAM_CSI && byte >= 0x40 && byte <= 0x7e) ||
               (term.stream == STREAM_OSC && byte == '\a')) {
      term.stream = STREAM_TEXT;
    }
  }
  terminal_process(term.emulator, bytes, length);
  if (term.stream == STREAM_TEXT && term.utf8_remaining == 0)
    terminal_process(term.emulator, (const uint8_t *)"\033[6n", 4);
  term.dirty = true;
}

static void terminal_color(unsigned color) {
  static const unsigned ansi[8] = {0, 4, 2, 6, 1, 5, 3, 7};
  char sequence[24];
  int length = snprintf(sequence, sizeof(sequence), "\033[%u;%um",
                        ansi[color & 7] + (color & 8 ? 90 : 30),
                        ansi[(color >> 4) & 7] + (color & 128 ? 100 : 40));
  terminal_write(sequence, length);
}

static int terminal_dispatch(rpc_call_t *call) {
  if (!(call->call_id & RPC_KERNEL_CALL) ||
      call->arg_len < sizeof(tty_rpc_request_t) ||
      call->ret_cap < sizeof(tty_rpc_reply_t))
    return RPC_ERR_INVAL;
  const tty_rpc_request_t *request = call->arg;
  if (request->handle != term.tty || request->operation >= TTY_RPC_COUNT ||
      (request->operation != TTY_RPC_WRITE &&
       call->arg_len != sizeof(*request)) ||
      request->state.color > 255 || request->state.cursor_visible > 1)
    return RPC_ERR_INVAL;

  term.state.color = request->state.color;
  term.state.cursor_visible = request->state.cursor_visible;
  tty_rpc_reply_t reply = {0};
  char sequence[48];
  switch (request->operation) {
  case TTY_RPC_WRITE:
    terminal_write(request + 1, call->arg_len - sizeof(*request));
    break;
  case TTY_RPC_MOVE:
    if (request->args.cursor.x < 0 || request->args.cursor.x > term.columns ||
        request->args.cursor.y < 0 || request->args.cursor.y >= term.rows)
      return RPC_ERR_INVAL;
    snprintf(sequence, sizeof(sequence), "\033[%d;%dH",
             request->args.cursor.y + 1, request->args.cursor.x + 1);
    terminal_write(sequence, strlen(sequence));
    break;
  case TTY_RPC_CLEAR:
    terminal_write("\033[2J\033[H", 7);
    break;
  case TTY_RPC_SCROLL:
    snprintf(sequence, sizeof(sequence), "\033[S\033[%d;1H", term.rows);
    terminal_write(sequence, strlen(sequence));
    break;
  case TTY_RPC_DRAW_BOX:
    if (request->args.box.x < 0 || request->args.box.y < 0 ||
        request->args.box.x1 < request->args.box.x ||
        request->args.box.y1 < request->args.box.y ||
        request->args.box.x1 >= term.columns ||
        request->args.box.y1 >= term.rows || request->args.box.color > 255)
      return RPC_ERR_INVAL;
    terminal_write("\0337", 2);
    terminal_color(request->args.box.color);
    for (int row = request->args.box.y; row <= request->args.box.y1; row++) {
      snprintf(sequence, sizeof(sequence), "\033[%d;%dH\033[%dX", row + 1,
               request->args.box.x + 1,
               request->args.box.x1 - request->args.box.x + 1);
      terminal_write(sequence, strlen(sequence));
    }
    terminal_color(request->state.color);
    terminal_write("\0338", 2);
    break;
  case TTY_RPC_INPUT_STATUS:
    reply.value = window_get_key_press_status(term.window);
    break;
  case TTY_RPC_INPUT_GET:
    reply.value = window_get_key_press_data(term.window);
    break;
  default:
    return RPC_ERR_BAD_OPCODE;
  }
  reply.state = term.state;
  memcpy(call->ret, &reply, sizeof(reply));
  call->ret_len = sizeof(reply);
  return RPC_OK;
}

static void shell_task(uintptr_t unused) {
  (void)unused;
  int status = term.tty ? tty_set(NowTaskID(), term.tty) : -1;
  if (status == 0)
    status = exec(term.program, term.command);
  term.shell_status = status;
  __atomic_store_n(&term.shell_finished, true, __ATOMIC_RELEASE);
  _exit(status);
}

static int terminal_run(void) {
  unsigned last_frame = (unsigned)clock() - FRAME_MS;
  while (!__atomic_load_n(&term.shell_finished, __ATOMIC_ACQUIRE)) {
    int event;
    while ((event = window_get_event(term.window)) >= 0) {
      if (event == GUI_EVENT_CLOSE_WINDOW)
        return 0;
      window_get_event(term.window); // Mouse coordinates.
      if (event == GUI_EVENT_MOUSE_WHEEL) {
        int direction = window_get_event(term.window);
        terminal_handle_mouse_scroll(term.emulator, direction == 1 ? 1 : -1);
        term.dirty = true;
      }
    }
    while (window_get_key_up_data(term.window) >= 0) {
    }
    if (window_get_key_press_status(term.window) > 0)
      tty_notify_input(term.tty);
    unsigned now = (unsigned)clock();
    if (term.dirty && now - last_frame >= FRAME_MS) {
      terminal_flush(term.emulator);
      term.dirty = false;
      last_frame = now;
      if (window_present(term.window, BORDER << 16 | TITLE_HEIGHT,
                         (term.width - BORDER) << 16 |
                             (term.height - BORDER)) != RPC_OK)
        return 1;
    }
    int status = rpc_serve_once(FRAME_MS);
    if (status != RPC_OK && status != RPC_ERR_TIMEOUT)
      return 1;
  }
  return term.shell_status;
}

int main(int argc, char **argv) {
  char *default_program[] = {"psh.bin"};
  char **command = argc > 1 ? argv + 1 : default_program;
  size_t command_length;
  if (runtime_command_line_build(argc > 1 ? argc - 1 : 1, command,
                                 &term.command, &command_length) != 0)
    return 1;
  term.program = command[0];
  term.state = (tty_rpc_state_t){.color = 7, .cursor_visible = 1};
  int status = 1, shell = -1;
  void *stack = NULL;
  framebuffer_info_t display;
  if (framebuffer_info(&display) < 0 || display.width < 160 ||
      display.height < 100)
    goto done;
  term.width = display.width < DEFAULT_WIDTH + 2 * BORDER
                   ? display.width
                   : DEFAULT_WIDTH + 2 * BORDER;
  term.height = display.height < DEFAULT_HEIGHT + TITLE_HEIGHT + BORDER
                    ? display.height
                    : DEFAULT_HEIGHT + TITLE_HEIGHT + BORDER;
  int x = (display.width - term.width) / 2;
  int y = (display.height - term.height) / 2;
  term.window = create_window("term", x, y, term.width, term.height);
  if (!term.window)
    goto done;
  TerminalDisplay surface = {
      .width = term.width - 2 * BORDER,
      .height = term.height - TITLE_HEIGHT - BORDER,
      .buffer = (uint32_t *)window_get_fb(term.window) +
                TITLE_HEIGHT * term.width + BORDER,
      .pitch = term.width * sizeof(uint32_t),
      .red_mask_size = 8,
      .red_mask_shift = 16,
      .green_mask_size = 8,
      .green_mask_shift = 8,
      .blue_mask_size = 8,
      .blue_mask_shift = 0,
  };
  term.emulator = terminal_new(&surface, 10.0f, terminal_allocate, free);
  if (!term.emulator)
    goto done;
  terminal_set_auto_flush(term.emulator, false);
  terminal_set_crnl_mapping(term.emulator, true);
  terminal_set_pty_writer(term.emulator, cursor_report);
  size_t columns = terminal_columns(term.emulator);
  size_t rows = terminal_rows(term.emulator);
  if (columns == 0 || rows == 0 || columns > surface.width ||
      rows > surface.height)
    goto done;
  term.columns = columns;
  term.rows = rows;
  terminal_write("\033[2J\033[H", 7);
  if (rpc_register_handler(TTY_RPC_DISPATCH, terminal_dispatch) != RPC_OK)
    goto done;
  term.tty = tty_alloc(NowTaskID(), TTY_RPC_DISPATCH, term.columns, term.rows);
  stack = malloc(32 * 1024);
  if (!term.tty || !stack)
    goto done;
  window_start_recv_keyboard(term.window);
  shell = AddThread("shell", (uintptr_t)shell_task,
                    (uintptr_t)stack + 32 * 1024, 0);
  if (shell < 0)
    goto done;
  logkf("TERM ready x=%d y=%d width=%d height=%d columns=%d rows=%d\n", x, y,
        term.width, term.height, term.columns, term.rows);
  status = terminal_run();

done:
  if (term.tty)
    tty_free(term.tty);
  if (shell >= 0)
    SubThread(shell);
  free(stack);
  if (term.emulator)
    terminal_destroy(term.emulator);
  close_window(term.window);
  free(term.command);
  if (status != 0)
    logkf("TERM exited with status %d\n", status);
  return status;
}
