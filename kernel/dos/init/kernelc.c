// Powerint DOS 386
// Copyright (C) 2021-2022 zhouzhihao & min0911
#include <dos.h>
uint32_t running_mode = POWERINTDOS;  // 运行模式
unsigned char *font, *ascfont, *hzkfont;

struct tty *now_tty() {
  extern struct List *tty_list;
  struct tty *n;
  for (int j = 1; list_get(j, tty_list) != 0; j++) {
    n = (struct tty *)list_get(j, tty_list)->val;
    if ((now_tty_TextMode(n) && running_mode == POWERINTDOS) ||
        (now_tty_HighTextMode(n) && running_mode == HIGHTEXTMODE)) {
      return n;
    }
  }
  return NULL;
}
