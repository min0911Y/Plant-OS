#include <dos.h>
#include <platform.h>
struct List *tty_list;
struct tty *tty_default;
static bool tty_registered(const struct tty *tty) {
  if (tty == NULL || tty_list == NULL) {
    return false;
  }
  for (size_t index = 1;; index++) {
    struct List *entry = list_get(index, tty_list);
    if (entry == NULL) {
      return false;
    }
    if ((struct tty *)(uintptr_t)entry->val == tty) {
      return true;
    }
  }
}
static void tty_print(struct tty *res, const char *string) {
  for (size_t i = 0; string[i]; i++) {
    if (res->y == res->ysize && res->x >= res->xsize) {
      res->screen_ne(res);
    }
    t_putchar(res, ((unsigned char *)string)[i]);
  }
}
static void tty_gotoxy(struct tty *res, int x, int y) {
  if(res->x == x && res->y == y) return;
  if (x >= 0 && y >= 0) {
    int x2 = x;
    int y2 = y;
    if (x <= res->xsize - 1 && y <= res->ysize - 1) {
      res->MoveCursor(res, x, y);
      return;
    }
    if (x <= res->xsize - 1) {
      for (int i = 0; i < y - res->ysize + 1; i++) {
        res->screen_ne(res);
      }
      res->MoveCursor(res, x, res->ysize - 1);
      return;
    }
    if (x > res->xsize - 1) {
      y2 += x / res->xsize - 1;
      x2 = x % res->xsize;
      if (y2 <= res->ysize - 1)
        tty_gotoxy(res, x2, y2 + 1);
      else
        tty_gotoxy(res, x2, y2);
    }
  } else {
    if (x < 0) {
      x += res->xsize;
      y--;
      tty_gotoxy(res, x, y);
    }
    if (y < 0) {
      return;
    }
  }
}

int default_tty_fifo_status(struct tty *res) {
  return fifo8_status(task_get_key_fifo(current_task()));
}
int default_tty_fifo_get(struct tty *res) {
  return fifo8_get(task_get_key_fifo(current_task()));
}
bool init_tty(void) {
  tty_list = NewList();
  if (tty_list == NULL) {
    return false;
  }
  tty_default = tty_console_create();
  if (tty_default == NULL) {
    DeleteList(tty_list);
    tty_list = NULL;
    return false;
  }
  return true;
}
struct tty *tty_alloc(void *vram, int xsize, int ysize,
                      void (*putchar)(struct tty *res, int c),
                      void (*MoveCursor)(struct tty *res, int x, int y),
                      void (*clear)(struct tty *res),
                      void (*screen_ne)(struct tty *res),
                      void (*Draw_Box)(struct tty *res, int x, int y, int x1,
                                       int y1, unsigned char color),
                      int (*fifo_status)(struct tty *res), int (*fifo_get)(struct tty *res)) {
  struct tty *res = (struct tty *)page_malloc(sizeof(struct tty));
  if (res == NULL) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  res->using1 = 1;
  res->vram = vram;
  res->xsize = xsize;
  res->ysize = ysize;
  res->putchar = putchar;
  res->MoveCursor = MoveCursor;
  res->clear = clear;
  res->screen_ne = screen_ne;
  res->Draw_Box = Draw_Box;
  res->gotoxy = tty_gotoxy;
  res->print = tty_print;
  res->fifo_status = fifo_status;
  res->fifo_get = fifo_get;
  res->color = 0x07;
  res->cur_moving = 1;
  res->color_saved = -1;
  if (tty_list == NULL || !AddVal((uintptr_t)res, tty_list)) {
    page_free((void *)res, sizeof(struct tty));
    return NULL;
  }
  return res;
}
void tty_free(struct tty *res) {
  if (!tty_registered(res) || !res->using1 || res == tty_default) {
    return;
  }
  res->using1 = 0;
  task_close_tty(res, tty_default);
  for (size_t i = 1; list_get(i, tty_list) != NULL; i++) {
    if (list_get(i, tty_list)->val == (uintptr_t)res) {
      DeleteVal(i, tty_list);
      break;
    }
  }
  page_free((void *)res, sizeof(struct tty));
  return;
}
struct tty *tty_set(mtask *task, struct tty *res) {
  if (task != NULL && tty_registered(res) && res->using1 == 1) {
    struct tty *old = task->TTY;
    task->TTY = res;
    task->tty_session = res;
    return old;
  }
  return NULL;
}
bool tty_notify_input(struct tty *tty) {
  if (!tty_registered(tty))
    return false;
  tty->input_sequence++;
  task_wake_tty(tty);
  return true;
}
struct tty *tty_set_default(struct tty *res) {
  if (res->using1 == 1) {
    struct tty *old = tty_default;
    tty_default = res;
    return old;
  }
  return NULL;
}
void tty_set_color(struct tty *tty, unsigned char color) {
  tty->color = color;
  if (tty->native_ansi) {
    unsigned foreground = (color & 2) | ((color & 1) << 2) | ((color & 4) >> 2);
    unsigned background = (color >> 4) & 7;
    background =
        (background & 2) | ((background & 1) << 2) | ((background & 4) >> 2);
    char sequence[24];
    snprintf(sequence, sizeof(sequence), "\033[%u;%um",
             foreground + (color & 8 ? 90 : 30),
             background + (color & 128 ? 100 : 40));
    tty->print(tty, sequence);
  }
}
void tty_stop_cursor_moving(struct tty *t) {
  t->cur_moving = 0;
  if (t->native_ansi)
    t->print(t, "\033[?25l");
}
void tty_start_curor_moving(struct tty *t) {
  t->cur_moving = 1;
  if (t->native_ansi)
    t->print(t, "\033[?25h");
  else
    t->MoveCursor(t, t->x, t->y);
}
