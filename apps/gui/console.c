#include "gui.h"
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
void draw_window(window_t *window, int x, int y, int x1, int y1, color_t color);
void puts_window(window_t *window, char *s, int x, int y, color_t color);
void console_task(tty_t tty) {
  tty_set(NowTaskID(), tty);
  exec("psh.bin", "");
  for (;;)
    ;
}
bool now_tty_GraphicMode(struct tty *res) {
  console_t *console = (console_t *)res->vram;
  return console->window->desktop->focused_window == console->window;
}
color_t text_color_to_real_color(unsigned char text_color, bool back_or_font) {
  unsigned char c;
  if (back_or_font)
    c = text_color & 0x0f;
  else
    c = text_color >> 4;
  if (c == 0)
    return COL_000000;
  else if (c == 1)
    return COL_000084;
  else if (c == 2)
    return COL_008400;
  else if (c == 3)
    return argb(0, 36, 36, 36);
  else if (c == 4)
    return COL_840000;
  else if (c == 5)
    return COL_FFFF00;
  else if (c == 6)
    return COL_848400;
  else if (c == 7)
    return COL_C6C6C6;
  else if (c == 8)
    return COL_848484;
  else if (c == 9)
    return COL_0000FF;
  else if (c == 10)
    return COL_00FF00;
  else if (c == 11)
    return COL_00FFFF;
  else if (c == 12)
    return COL_FF0000;
  else if (c == 13)
    return COL_FF00FF;
  else if (c == 14)
    return COL_FFFF00;
  else if (c == 15)
    return COL_FFFFFF;
}
void putchar_console(struct tty *res, int c) {
  if (c == '\r') {
    res->MoveCursor(res, 0, res->y);
    return;
  }
  if (c == '\t') {
    putchar_console(res, ' ');
    putchar_console(res, ' ');
    putchar_console(res, ' ');
    putchar_console(res, ' ');
    return;
  }
  console_t *console = (console_t *)res->vram;
  if (res->x == res->xsize) {
    if (res->y == res->ysize - 1) {
      res->screen_ne(res);
    } else {
      res->x = 0;
      res->y++;
    }
  }
  if (c == '\n') {
    if (res->y == res->ysize - 1) {
      res->screen_ne(res);
      return;
    } else {
      res->x = 0;
      res->y++;
      res->MoveCursor(res, res->x, res->y);
      return;
    }
  }
  if (c == '\b') {
    if (res->x > 0) {
      res->x--;
      console->window->draw(
          console->window, res->x * 8 + console->x, res->y * 16 + console->y,
          res->x * 8 + console->x + 8, res->y * 16 + console->y + 16,
          text_color_to_real_color(res->color, false));
      res->MoveCursor(res, res->x, res->y);
      return;
    } else {
      res->x = res->xsize - 1;
      res->y--;
      res->MoveCursor(res, res->x, res->y);
      return;
    }
  }
  char s[2] = {0, 0};
  s[0] = c;
  console->window->draw(console->window, res->x * 8 + console->x,
                        res->y * 16 + console->y, res->x * 8 + console->x + 8,
                        res->y * 16 + console->y + 16,
                        text_color_to_real_color(res->color, false));
  console->window->puts(console->window, s, res->x * 8 + console->x,
                        res->y * 16 + console->y,
                        text_color_to_real_color(res->color, true));
  res->x++;
  res->MoveCursor(res, res->x, res->y);
}
void clear_console(struct tty *res) {
  console_t *console = (console_t *)res->vram;
  res->x = 0;
  res->y = 0;
  console->window->draw(
      console->window, console->x, console->y, console->x + console->xsize,
      console->y + console->ysize, text_color_to_real_color(res->color, false));
}
void MoveCursor_console(struct tty *res, int x, int y) {
  console_t *console = (console_t *)res->vram;
  res->x = x;
  res->y = y;
  int cur_x_old = console->sht_cur->vx0;
  int cur_y_old = console->sht_cur->vy0;
  sheet_slide(console->sht_cur, console->x + x * 8, console->y + y * 16);
  sheet_refresh(console->sht_copy, cur_x_old, cur_y_old, cur_x_old + 8,
                cur_y_old + 16);
  sheet_refresh(console->window->sht, cur_x_old, cur_y_old, cur_x_old + 8,
                cur_y_old + 16);
  int cur_x_new = console->sht_cur->vx0;
  int cur_y_new = console->sht_cur->vy0;
  sheet_refresh(console->window->sht, cur_x_new, cur_y_new, cur_x_new + 8,
                cur_y_new + 16);
}
static void copy_char(vram_t *vram, int off_x, int off_y, int x, int y, int x1,
                      int y1, int xsize) {
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 8; j++) {
      vram[(y + i + off_y) * xsize + (j + x + off_x)] =
          vram[(y1 + i + off_y) * xsize + (j + x1 + off_x)];
    }
  }
}
void screen_ne_console(struct tty *res) {
  console_t *console = (console_t *)res->vram;
  for (int i = 0; i < res->ysize; i++) {
    for (int j = 0; j < res->xsize; j++) {
      copy_char(console->vram_copy, console->x, console->y, j * 8, i * 16,
                j * 8, (i + 1) * 16, console->window->xsize);
    }
  }
  console->window->draw(
      console->window, console->x, console->y + (res->ysize - 1) * 16,
      console->x + res->xsize * 8, console->y + res->ysize * 16,
      text_color_to_real_color(res->color, false));
  res->x = 0;
  res->y = res->ysize - 1;
  sheet_refresh(console->sht_copy, console->x, console->y,
                console->x + console->xsize, console->y + console->ysize);
  sheet_refresh(console->window->sht, console->x, console->y,
                console->x + console->xsize, console->y + console->ysize);
  res->MoveCursor(res, res->x, res->y);
}
void Draw_Box_console(struct tty *res, int x, int y, int x1, int y1,
                      unsigned char color) {
  console_t *console = (console_t *)res->vram;
  for (int i = y * 16 + console->y; i <= y1 * 16 + console->y; i++) {
    for (int j = x * 8 + console->x; j <= x1 * 8 + console->x; j++) {
      if (console->vram_copy[i * console->window->xsize + j] ==
          text_color_to_real_color(res->color, true)) {
        console->vram_copy[i * console->window->xsize + j] =
            text_color_to_real_color(color, true);
      } else {
        console->vram_copy[i * console->window->xsize + j] =
            text_color_to_real_color(color, false);
      }
    }
  }
  sheet_refresh(console->sht_copy, console->x + x * 8, console->y + y * 16,
                console->x + x1 * 8, console->y + y1 * 16);
  sheet_refresh(console->window->sht, console->x + x * 8, console->y + y * 16,
                console->x + x1 * 8, console->y + y1 * 16);
}
void draw_console_window(window_t *window, int x, int y, int x1, int y1,
                         color_t color) {
  SDraw_Box(window->console->vram_copy, x, y, x1, y1, color, window->xsize);
  sheet_refresh(window->console->sht_copy, x, y, x1, y1);
  sheet_refresh(window->sht, x, y, x1, y1);
}
void puts_console_window(window_t *window, char *s, int x, int y,
                         color_t color) {
  Sputs(window->console->vram_copy, s, x, y, color, window->xsize);
  sheet_refresh(window->console->sht_copy, x, y, x + strlen(s) * 8, y + 16);
  sheet_refresh(window->sht, x, y, x + strlen(s) * 8, y + 16);
}
struct tty *mtty_alloc(void *vram, int xsize, int ysize,
                       void (*putchar)(struct tty *res, int c),
                       void (*MoveCursor)(struct tty *res, int x, int y),
                       void (*clear)(struct tty *res),
                       void (*screen_ne)(struct tty *res),
                       void (*Draw_Box)(struct tty *res, int x, int y, int x1,
                                        int y1, unsigned char color)) {
  struct tty *res = (struct tty *)malloc(sizeof(struct tty));
  if (res == NULL) {
    return NULL;
  }
  res->using1 = 1;
  res->x = 0;
  res->y = 0;
  res->vram = vram;
  res->xsize = xsize;
  res->ysize = ysize;
  res->putchar = putchar;
  res->MoveCursor = MoveCursor;
  res->clear = clear;
  res->screen_ne = screen_ne;
  res->Draw_Box = Draw_Box;
  res->color = 0x07;
  return res;
}
void mtty_handle(uint32_t *a) {
  struct tty *tty;
  tty = (struct tty *)a[1]; // tty
  tty->x = a[7];
  tty->y = a[8];
  tty->color = a[9];

  console_t *cons = (console_t *)tty->vram;
  switch (a[0]) {
  case 0:
    putchar_console(tty, a[2]);
    break;
  case 1:
    MoveCursor_console(tty, a[2], a[3]);
    break;
  case 2:
    clear_console(tty);
    break;
  case 3:
    screen_ne_console(tty);
    break;
  case 4:
    Draw_Box_console(tty, a[2], a[3], a[4], a[5], a[6]);
    break;
  case 5:
    a[0] = fifo8_status(cons->window->fifo_keypress);
    break;
  case 6:
    a[0] = fifo8_get(cons->window->fifo_keypress);
    break;
  default:
    break;
  }
  a[7] = tty->x;
  a[8] = tty->y;
  a[9] = tty->color;
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
  free(console->tty);
  window->console = NULL;
  window->draw = draw_window;
  window->puts = puts_window;
  free(console);
}
console_t *create_console(window_t *window, int xsize, int ysize, int x,
                          int y) {
  if (window == NULL || window->super_window != NULL || xsize <= 0 ||
      ysize <= 0) {
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
  res->tty = mtty_alloc((void *)res, xsize / 8, ysize / 16, putchar_console,
                        MoveCursor_console, clear_console, screen_ne_console,
                        Draw_Box_console);
  res->shtctl = shtctl_init(window->vram, window->xsize, window->ysize);
  if (res->tty == NULL || res->shtctl == NULL) {
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
  res->tty_handle =
      tty_alloc(res->tty, (uintptr_t)mtty_handle, xsize / 8, ysize / 16);
  struct FIFO8 *fifo_keypress = malloc(sizeof(struct FIFO8));
  uint8_t *buf = malloc(128);
  res->task_stack = malloc(32 * 1024);
  if (res->tty_handle == 0 || fifo_keypress == NULL || buf == NULL ||
      res->task_stack == NULL) {
    free(fifo_keypress);
    free(buf);
    goto fail;
  }
  window->fifo_keypress = fifo_keypress;
  fifo8_init(fifo_keypress, 128, buf);
  window->console = res;
  window->draw = draw_console_window;
  window->puts = puts_console_window;
  uintptr_t stack_top = (uintptr_t)res->task_stack + 32 * 1024;
  ((uintptr_t *)stack_top)[-1] = res->tty_handle;
  int thread_tid =
      AddThread("", (uintptr_t)console_task, (unsigned int)stack_top - 8);
  if (thread_tid < 0) {
    goto fail;
  }
  tty_set(thread_tid, res->tty_handle);
  return res;

fail:
  window->console = NULL;
  window->draw = draw_window;
  window->puts = puts_window;
  if (res->tty_handle != 0) {
    tty_free(res->tty_handle);
  }
  if (window->fifo_keypress != NULL) {
    free(window->fifo_keypress->buf);
    free(window->fifo_keypress);
    window->fifo_keypress = NULL;
  }
  free(res->task_stack);
  if (res->sht_cur != NULL) {
    sheet_free(res->sht_cur);
  }
  if (res->sht_copy != NULL) {
    sheet_free(res->sht_copy);
  }
  free(res->vram_cur);
  free(res->vram_copy);
  if (res->shtctl != NULL) {
    ctl_free(res->shtctl);
  }
  free(res->tty);
  free(res);
  return NULL;
}
