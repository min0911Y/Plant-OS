#include <dos.h>
struct List *tty_list;
struct tty *tty_default;
void t_putchar(struct tty *res,char ch);
static bool tty_registered(const struct tty *tty) {
  if (tty == NULL || tty_list == NULL) {
    return false;
  }
  for (size_t index = 1;; index++) {
    struct List *entry = FindForCount(index, tty_list);
    if (entry == NULL) {
      return false;
    }
    if ((struct tty *)(uintptr_t)entry->val == tty) {
      return true;
    }
  }
}
static void tty_print(struct tty *res, const char *string) {
  for (int i = 0; i < strlen(string); i++) {
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
  tty_default =
      tty_alloc((void *)0xb8000, 80, 25, putchar_TextMode, MoveCursor_TextMode,
                clear_TextMode, screen_ne_TextMode, Draw_Box_TextMode,
                default_tty_fifo_status, default_tty_fifo_get);
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
  res->gotoxy = tty_gotoxy;
  res->print = tty_print;
  res->fifo_status = fifo_status;
  res->fifo_get = fifo_get;
  res->color = 0x07;
  res->cur_moving = 1;
  res->vt100 = 0;
  res->buf_p = 0;
  memset(res->buffer,0,sizeof(res->buffer));
  res->done = 0;
  res->mode = 0;
  res->color_saved = -1;
  if (tty_list == NULL || !AddVal((uintptr_t)res, tty_list)) {
    page_free((void *)res, sizeof(struct tty));
    return NULL;
  }
  return res;
}
void tty_free(struct tty *res) {
  if (!tty_registered(res) || res == tty_default) {
    return;
  }
  res->using1 = 0;
  task_close_tty(res, tty_default);
  for (size_t i = 1; FindForCount(i, tty_list) != NULL; i++) {
    if (FindForCount(i, tty_list)->val == (uintptr_t)res) {
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
  return tty_registered(tty) && task_wake_tty(tty) != 0;
}
struct tty *tty_set_default(struct tty *res) {
  if (res->using1 == 1) {
    struct tty *old = tty_default;
    tty_default = res;
    return old;
  }
  return NULL;
}
void tty_set_reserved(struct tty *res, unsigned int reserved1,
                      unsigned int reserved2, unsigned int reserved3,
                      unsigned int reserved4) {
  res->reserved[0] = reserved1;
  res->reserved[1] = reserved2;
  res->reserved[2] = reserved3;
  res->reserved[3] = reserved4;
}
void tty_stop_cursor_moving(struct tty *t) { t->cur_moving = 0; }
void tty_start_curor_moving(struct tty *t) {
  t->cur_moving = 1;
  t->MoveCursor(t, t->x, t->y);
}
