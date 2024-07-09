#include <dos.h>
#include <io.h>

extern struct tty *tty_default;
void clear() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->clear(tty_default);
  } else {
    task->TTY->clear(task->TTY);
  }
}
void printchar(char ch) {
  char ch1[2] = {ch, '\0'};
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->print(tty_default, ch1);
  } else {
    task->TTY->print(task->TTY, ch1);
  }
}
static char eos[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'f', 'J', 'K', 'S', 'T', 'm'}; // end of string,
                                        // vt100控制字符中可能的结束符号

static int t_isdigit(int c) { return (c >= '0' && c <= '9'); }
static int t_is_eos(char ch) {
  for (int i = 0; i < sizeof(eos); i++) {
    if (ch == eos[i])
      return 1;
  }
  return 0;
}
static int parse_vt100(struct tty *res, char *string) {
  switch (res->mode) {
  case MODE_A: {
    char dig_string[81] = {0};
    for (int i = 2, j = 0; string[i]; i++) {
      if (t_is_eos(string[i]))
        break;
      if (!t_isdigit(string[i]))
        return 0;
      dig_string[j++] = string[i];
    }
    res->gotoxy(res, res->x, res->y - strtol(dig_string, NULL, 10));
    return 1;
  }
  case MODE_B: {
    char dig_string[81] = {0};
    for (int i = 2, j = 0; string[i]; i++) {
      if (t_is_eos(string[i]))
        break;
      if (!t_isdigit(string[i]))
        return 0;
      dig_string[j++] = string[i];
    }
    res->gotoxy(res, res->x, res->y + strtol(dig_string, NULL, 10));
    return 1;
  }
  default:
    break;
  }
  return 0;
}
void t_putchar(struct tty *res, char ch) {
  if (ch == '\033' && res->vt100 == 0) {
    memset(res->buffer, 0, 81);
    res->buf_p = 0;
    res->buffer[res->buf_p++] = '\033';
    res->vt100 = 1;
    res->done = 0;
    return;
  } else if (res->vt100 && res->buf_p == 1) {
    if (ch == '[') {
      res->buffer[res->buf_p++] = ch;
      return;
    } else {
      res->vt100 = 0;
      for (int i = 0; i < res->buf_p; i++) {
        res->putchar(res, res->buffer[i]);
      }
    }
  } else if (res->vt100 && res->buf_p == 81) {
    for (int i = 0; i < res->buf_p; i++) {
      res->putchar(res, res->buffer[i]);
    }
    res->vt100 = 0;
  } else if (res->vt100) {
    res->buffer[res->buf_p++] = ch;
    if (t_is_eos(ch)) {
      res->mode = (vt100_mode_t)ch;
      if (!parse_vt100(res, res->buffer)) { // 失败了
        for (int i = 0; i < res->buf_p; i++) {
          res->putchar(res, res->buffer[i]);
        }
      }
      res->vt100 = 0;
      return;
    } else if (!t_isdigit(ch) && ch != ';') {
      for (int i = 0; i < res->buf_p; i++) {
        res->putchar(res, res->buffer[i]);
      }
      res->vt100 = 0;
      return;
    }

    return;
  }
  res->putchar(res, ch);
}
void putchar(char ch) {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    t_putchar(tty_default, ch);
  } else {
    t_putchar(task->TTY, ch);
  }
}
void screen_ne() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->screen_ne(tty_default);
  } else {
    task->TTY->screen_ne(task->TTY);
  }
}
int get_x() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    return tty_default->x;
  } else {
    return task->TTY->x;
  }
}
int get_y() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    return tty_default->y;
  } else {
    return task->TTY->y;
  }
}
int get_raw_y() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    return tty_default->Raw_y;
  } else {
    return task->TTY->Raw_y;
  }
}
int get_xsize() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    return tty_default->xsize;
  } else {
    return task->TTY->xsize;
  }
}
int get_ysize() {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    return tty_default->ysize;
  } else {
    return task->TTY->ysize;
  }
}
void print(const char *str) {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->print(tty_default, str);
  } else {
    task->TTY->print(task->TTY, str);
  }
}

void GotoXy_No_Safe(int x1, int y1) {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->MoveCursor(tty_default, x1, y1);
  } else {
    task->TTY->MoveCursor(task->TTY, x1, y1);
  }
}
void gotoxy(int x1, int y1) {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->gotoxy(tty_default, x1, y1);
  } else {
    task->TTY->gotoxy(task->TTY, x1, y1);
  }
}
void Text_Draw_Box(int x, int y, int x1, int y1, unsigned char color) {
  mtask *task = current_task();
  if (task->TTY->using1 != 1) {
    tty_default->Draw_Box(tty_default, x, y, x1, y1, color);
  } else {
    task->TTY->Draw_Box(task->TTY, x, y, x1, y1, color);
  }
}
