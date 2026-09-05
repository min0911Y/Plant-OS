// Powerint DOS 386
// Copyright (C) 2021-2022 zhouzhihao & min0911
#include <dos.h>
uint32_t running_mode = POWERINTDOS;  // 运行模式
unsigned char *font, *ascfont, *hzkfont;

struct tty *now_tty(void) {
  extern struct tty *tty_default;
  return tty_default;
}
