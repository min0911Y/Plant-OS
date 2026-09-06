#include "gui.h"
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

_Static_assert(sizeof(((console_t *)0)->tty_handle) == sizeof(tty_t),
               "console TTY handle must retain the native ABI width");

void draw_window(window_t *window, int x, int y, int x1, int y1, color_t color);
void puts_window(window_t *window, char *s, int x, int y, color_t color);

static void console_task(tty_t tty) {
  if (tty_set(NowTaskID(), tty) != 0)
    _exit((unsigned)-1);
  int status = exec("psh.bin", "psh.bin");
  if (status != 0)
    logkf("GUI console shell exited with status %d\n", status);
  _exit(status);
}

static color_t text_color(unsigned color, bool foreground) {
  static const color_t palette[16] = {
      COL_000000, COL_000084, COL_008400, argb(0, 36, 36, 36),
      COL_840000, COL_FFFF00, COL_848400, COL_C6C6C6,
      COL_848484, COL_0000FF, COL_00FF00, COL_00FFFF,
      COL_FF0000, COL_FF00FF, COL_FFFF00, COL_FFFFFF};
  return palette[(foreground ? color : color >> 4) & 15];
}

static void console_refresh(console_t *console, int x, int y, int x1, int y1) {
  sheet_refresh(console->sht_copy, x, y, x1, y1);
  sheet_refresh(console->window->sht, x, y, x1, y1);
}

static void console_move(console_t *console, int x, int y) {
  console->state.x = x;
  console->state.y = y;
  bool visible = console->state.cursor_visible && x < console->xsize / 8;
  sheet_updown(console->sht_cur, visible ? 1 : -1);
  int old_x = console->sht_cur->vx0;
  int old_y = console->sht_cur->vy0;
  sheet_slide(console->sht_cur, console->x + x * 8, console->y + y * 16);
  console_refresh(console, old_x, old_y, old_x + 8, old_y + 16);
  int new_x = console->sht_cur->vx0;
  int new_y = console->sht_cur->vy0;
  sheet_refresh(console->window->sht, new_x, new_y, new_x + 8, new_y + 16);
}

static void console_scroll(console_t *console) {
  unsigned stride = console->window->xsize;
  for (int row = 0; row < console->ysize - 16; row++) {
    vram_t *line =
        console->vram_copy + (console->y + row) * stride + console->x;
    memcpy(line, line + 16 * stride, console->xsize * sizeof(*line));
  }
  console->window->draw(
      console->window, console->x, console->y + console->ysize - 16,
      console->x + console->xsize, console->y + console->ysize,
      text_color(console->state.color, false));
  console_refresh(console, console->x, console->y, console->x + console->xsize,
                  console->y + console->ysize);
  console_move(console, 0, console->ysize / 16 - 1);
}

static void console_putchar(console_t *console, char c) {
  tty_rpc_state_t *state = &console->state;
  int columns = console->xsize / 8, rows = console->ysize / 16;
  if (c == '\r') {
    console_move(console, 0, state->y);
    return;
  }
  if (c == '\t') {
    for (int i = 0; i < 4; i++)
      console_putchar(console, ' ');
    return;
  }
  if (c == '\b') {
    if (state->x > 0) {
      state->x--;
    } else if (state->y > 0) {
      state->x = columns - 1;
      state->y--;
    } else {
      return;
    }
  } else if (state->x == columns || c == '\n') {
    if (state->y == rows - 1)
      console_scroll(console);
    else
      console_move(console, 0, state->y + 1);
    if (c == '\n')
      return;
  }
  int x = console->x + state->x * 8, y = console->y + state->y * 16;
  console->window->draw(console->window, x, y, x + 8, y + 16,
                        text_color(state->color, false));
  if (c != '\b') {
    char text[2] = {c, 0};
    console->window->puts(console->window, text, x, y,
                          text_color(state->color, true));
    state->x++;
  }
  console_move(console, state->x, state->y);
}

static void console_draw_box(console_t *console,
                             const tty_rpc_request_t *request) {
  int x = console->x + request->args.box.x * 8;
  int y = console->y + request->args.box.y * 16;
  int x1 = console->x + (request->args.box.x1 + 1) * 8;
  int y1 = console->y + (request->args.box.y1 + 1) * 16;
  color_t foreground = text_color(console->state.color, true);
  color_t new_foreground = text_color(request->args.box.color, true);
  color_t new_background = text_color(request->args.box.color, false);
  for (int row = y; row < y1; row++) {
    vram_t *line = console->vram_copy + row * console->window->xsize;
    for (int column = x; column < x1; column++)
      line[column] =
          line[column] == foreground ? new_foreground : new_background;
  }
  console_refresh(console, x, y, x1, y1);
}

static void draw_console_window(window_t *window, int x, int y, int x1, int y1,
                                color_t color) {
  SDraw_Box(window->console->vram_copy, x, y, x1, y1, color, window->xsize);
  console_refresh(window->console, x, y, x1, y1);
}

static void puts_console_window(window_t *window, char *text, int x, int y,
                                color_t color) {
  Sputs(window->console->vram_copy, text, x, y, color, window->xsize);
  console_refresh(window->console, x, y, x + strlen(text) * 8, y + 16);
}

int console_rpc_dispatch(rpc_call_t *call) {
  if (!(call->call_id & RPC_KERNEL_CALL) ||
      call->arg_len < sizeof(tty_rpc_request_t) ||
      call->ret_cap < sizeof(tty_rpc_reply_t))
    return RPC_ERR_INVAL;
  const tty_rpc_request_t *request = call->arg;
  if (request->operation >= TTY_RPC_COUNT ||
      (request->operation != TTY_RPC_WRITE &&
       call->arg_len != sizeof(*request)) ||
      request->state.color > 255 || request->state.cursor_visible > 1)
    return RPC_ERR_INVAL;

  TaskLock();
  desktop_t *desktop = get_now_desktop();
  console_t *console = NULL;
  if (desktop != NULL) {
    for (size_t i = 1;; i++) {
      struct List *entry = list_search_by_count(i, desktop->window_list);
      if (entry == NULL)
        break;
      window_t *window = (window_t *)entry->val;
      if (window->console != NULL &&
          window->console->tty_handle == request->handle) {
        console = window->console;
        break;
      }
    }
  }
  if (console == NULL) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }
  int columns = console->xsize / 8, rows = console->ysize / 16;
  bool valid = request->state.x >= 0 && request->state.x <= columns &&
               request->state.y >= 0 && request->state.y < rows;
  if (request->operation == TTY_RPC_MOVE)
    valid = valid && request->args.cursor.x >= 0 &&
            request->args.cursor.x <= columns && request->args.cursor.y >= 0 &&
            request->args.cursor.y < rows;
  if (request->operation == TTY_RPC_DRAW_BOX)
    valid = valid && request->args.box.x >= 0 && request->args.box.y >= 0 &&
            request->args.box.x1 >= request->args.box.x &&
            request->args.box.x1 < columns &&
            request->args.box.y1 >= request->args.box.y &&
            request->args.box.y1 < rows && request->args.box.color <= 255;
  if (!valid) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }
  console->state = request->state;
  tty_rpc_reply_t reply = {0};
  switch (request->operation) {
  case TTY_RPC_WRITE: {
    const char *text = (const char *)(request + 1);
    for (unsigned i = 0; i < call->arg_len - sizeof(*request); i++)
      console_putchar(console, text[i]);
    break;
  }
  case TTY_RPC_MOVE:
    console_move(console, request->args.cursor.x, request->args.cursor.y);
    break;
  case TTY_RPC_CLEAR:
    console->window->draw(
        console->window, console->x, console->y, console->x + console->xsize,
        console->y + console->ysize, text_color(console->state.color, false));
    console_move(console, 0, 0);
    break;
  case TTY_RPC_SCROLL:
    console_scroll(console);
    break;
  case TTY_RPC_DRAW_BOX:
    console_draw_box(console, request);
    break;
  case TTY_RPC_INPUT_STATUS:
    reply.value = fifo8_status(console->window->fifo_keypress);
    break;
  case TTY_RPC_INPUT_GET:
    reply.value = fifo8_get(console->window->fifo_keypress);
    break;
  }
  reply.state = console->state;
  memcpy(call->ret, &reply, sizeof(reply));
  call->ret_len = sizeof(reply);
  TaskUnlock();
  return RPC_OK;
}

void close_console(console_t *console) {
  if (console == NULL) {
    return;
  }
  window_t *window = console->window;
  if (console->tty_handle != 0) {
    tty_free(console->tty_handle);
  }
  if (console->task_stack != NULL) {
    free(console->task_stack);
  }
  if (window->fifo_keypress != NULL) {
    free(window->fifo_keypress->buf);
    free(window->fifo_keypress);
    window->fifo_keypress = NULL;
  }
  if (console->sht_cur != NULL) {
    sheet_free(console->sht_cur);
  }
  if (console->sht_copy != NULL) {
    sheet_free(console->sht_copy);
  }
  free(console->vram_cur);
  free(console->vram_copy);
  if (console->shtctl != NULL) {
    ctl_free(console->shtctl);
  }
  window->console = NULL;
  window->draw = draw_window;
  window->puts = puts_window;
  free(console);
}
console_t *create_console(window_t *window, int xsize, int ysize, int x,
                          int y) {
  if (window == NULL || window->super_window != NULL ||
      window->console != NULL || xsize < 8 || ysize < 16 || xsize % 8 != 0 ||
      ysize % 16 != 0 || x < 0 || y < 0 || xsize > window->xsize ||
      ysize > window->ysize || x > window->xsize - xsize ||
      y > window->ysize - ysize) {
    return NULL;
  }
  console_t *res = malloc(sizeof(console_t));
  if (res == NULL) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  res->window = window;
  res->xsize = xsize;
  res->ysize = ysize;
  res->x = x;
  res->y = y;
  res->handle_left = NULL;
  res->handle_right = NULL;
  res->handle_stay = NULL;
  res->close = close_console;
  res->state = (tty_rpc_state_t){.color = 7, .cursor_visible = 1};
  res->shtctl = shtctl_init(window->vram, window->xsize, window->ysize);
  if (res->shtctl == NULL) {
    goto fail;
  }
  res->sht_copy = sheet_alloc(res->shtctl);
  res->sht_cur = sheet_alloc(res->shtctl);
  res->vram_cur = malloc(8 * 16 * sizeof(vram_t));
  res->vram_copy = malloc(window->xsize * window->ysize * sizeof(vram_t));
  if (res->sht_copy == NULL || res->sht_cur == NULL ||
      res->vram_cur == NULL || res->vram_copy == NULL) {
    goto fail;
  }
  window->draw(window, x, y, x + xsize, y + ysize, COL_000000);
  SDraw_Box(res->vram_cur, 0, 0, 8, 16, COL_FFFFFF, 8);
  memcpy((void *)res->vram_copy, (void *)window->vram,
         window->xsize * window->ysize * sizeof(vram_t));
  sheet_setbuf(res->sht_cur, res->vram_cur, 8, 16, COL_TRANSPARENT);
  sheet_setbuf(res->sht_copy, res->vram_copy, window->xsize, window->ysize, -1);
  sheet_slide(res->sht_cur, res->x, res->y);
  sheet_slide(res->sht_copy, 0, 0);
  sheet_updown(res->sht_copy, 0);
  sheet_updown(res->sht_cur, 1);
  sheet_refresh(res->sht_copy, 0, 0, window->xsize, window->ysize);
  sheet_refresh(res->sht_cur, 0, 0, 8, 16);
  struct FIFO8 *fifo_keypress = malloc(sizeof(struct FIFO8));
  uint8_t *buf = malloc(128);
  res->task_stack = malloc(32 * 1024);
  if (fifo_keypress == NULL || buf == NULL || res->task_stack == NULL) {
    free(fifo_keypress);
    free(buf);
    goto fail;
  }
  window->fifo_keypress = fifo_keypress;
  fifo8_init(fifo_keypress, 128, buf);
  window->console = res;
  window->draw = draw_console_window;
  window->puts = puts_console_window;
  res->tty_handle =
      tty_alloc(window->desktop->tid, TTY_RPC_DISPATCH, xsize / 8, ysize / 16);
  if (res->tty_handle == 0)
    goto fail;
  uintptr_t stack_top = (uintptr_t)res->task_stack + 32 * 1024;
  int thread_tid =
      AddThread("console", (uintptr_t)console_task, stack_top, res->tty_handle);
  if (thread_tid < 0) {
    goto fail;
  }
  tty_set(thread_tid, res->tty_handle);
  return res;

fail:
  close_console(res);
  return NULL;
}
