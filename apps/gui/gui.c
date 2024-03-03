#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>
static struct VBEINFO *vinfo;
static void click1(button_t *button) {
  printf("You click the test button!\n");
  int x = rand() % button->super_window->window->xsize;
  int y = rand() % button->super_window->window->ysize;
  button->super_window->slide_sheet(button->super_window, button->sht, x, y);
}
unsigned char *ascfont, *hzkfont;
void main() {
  if (filesize("A:\\other\\font.bin") == -1 ||
      filesize("A:\\other\\HZK16") == -1) {
    print("error\n");
  }
  printf("\n\n");
  int xsize_input, ysize_input;
  char *buffer_input;
re:
  buffer_input = malloc(512);
  printf("desktop xsize:");
  scan(buffer_input, 512);
  xsize_input = (int)strtol(buffer_input, NULL, 10);
  printf("desktop ysize:");
  scan(buffer_input, 512);
  ysize_input = (int)strtol(buffer_input, NULL, 10);
  free((void *)buffer_input);
  unsigned vram;
  vram = set_mode(xsize_input, ysize_input);

  logkf("vram = %08x\n", vram);
  ascfont = (unsigned char *)malloc(filesize("A:\\other\\font.bin"));
  hzkfont = (unsigned char *)malloc(filesize("A:\\other\\HZK16"));
  api_ReadFile("A:\\other\\font.bin", ascfont);
  api_ReadFile("A:\\other\\HZK16", hzkfont);
  desktop_t *desktop0 = create_desktop(xsize_input, ysize_input, NowTaskID());
  desktop0->display(desktop0, vram);
  desktop0->draw(desktop0, 0, 0, xsize_input, ysize_input,
                 argb(0, 58, 110, 165));
  desktop0->draw(desktop0, 10, 30, 18 + 48 * 8 + 8, 30 + 16, COL_C6C6C6);
  desktop0->puts(desktop0, "Power Desktop powered by Powerint DOS 386 kernel",
                 18, 30, COL_000000);
  window_t *window0 = create_window(desktop0, "hello0", 200, 200, NowTaskID());
  window_t *window1 = create_window(desktop0, "hello1", 200, 200, NowTaskID());
  window_t *window2 =
      create_window(desktop0, "console", 80 * 8 + 8, 25 * 16 + 28, NowTaskID());
  window_t *window3 =
      create_window(desktop0, "console", 40 * 8 + 8, 20 * 16 + 28, NowTaskID());
  window0->display(window0, 100, 100, 1);
  window1->display(window1, 200, 200, 2);
  window2->display(window2, 250, 250, 3);
  window2->display(window3, 400, 400, 4);
  window0->puts(window0, "Hello world!", 52, 92, COL_000000);
  gmouse_t *gmouse0 =
      create_gmouse(desktop0, desktop0->xsize / 2, desktop0->ysize / 2, 5);

  super_window_t *super_window0 = create_super_window(window1);
  button_t *button0 =
      create_button(super_window0, "test", 50, 20, 50, 50, click1);
  textbox_t *textbox0 = create_textbox(super_window0, 15 * 8, 16, 4, 110);
  for (;;) {
    time_t rawtime;
    struct tm *info;
    char buffer[80];

    rawtime = time(&rawtime);

    info = localtime(&rawtime);

    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", info);
    desktop0->draw(desktop0, 10, 10, 18 + strlen(buffer) * 8 + 8, 10 + 16,
                   COL_C6C6C6);
    desktop0->puts(desktop0, buffer, 18, 10, COL_000000);
    sleep(1000);
  }
}
void SDraw_Box(vram_t *vram, int x, int y, int x1, int y1, color_t color,
               int xsize) {
  int i, j;
  for (i = x; i < x1; i++) {
    for (j = y; j < y1; j++) {
      vram[j * xsize + i] = color;
    }
  }
  return;
}
void SDraw_Char(vram_t *vram1, int x, int y, char c, color_t color, int xsize) {
  // x *= 8;
  // y *= 16;
  unsigned char *font;
  font = ascfont;
  font += c * 16;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 8; j++) {
      if (font[i] & (0x80 >> j)) {
        vram1[(y + i) * xsize + x + j] = color;
      }
    }
  }
  return;
}
void Sputs(vram_t *vram, char *str, int x, int y, color_t col, int xsize) {
  for (int i = 0; i != strlen(str); i++) {
    SDraw_Char(vram, x + i * 8, y, str[i], col, xsize);
  }
}
void boxfill(vram_t *vram, int xsize, color_t c, int x0, int y0, int x1,
             int y1) {
  int x, y;
  for (y = y0; y <= y1; y++) {
    for (x = x0; x <= x1; x++)
      vram[y * xsize + x] = c;
  }
  return;
}

void PUTCHINESE0(vram_t *vram, int x, int y, color_t color, unsigned short CH,
                 int xsize) {
  int i, j, k, offset;
  int flag;
  unsigned char buffer[32];
  unsigned char word[2] = {CH & 0xff,
                           (CH & 0xff00) >> 8}; // 将字符转换为两个字节
  unsigned char key[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
  unsigned char *p = hzkfont;
  offset =
      (94 * (unsigned int)(word[0] - 0xa0 - 1) + (word[1] - 0xa0 - 1)) * 32;
  p = p + offset;
  // 读取，并写入到vram中
  for (i = 0; i < 32; i++) {
    buffer[i] = p[i];
  }
  for (k = 0; k < 16; k++) {
    for (j = 0; j < 2; j++) {
      for (i = 0; i < 8; i++) {
        flag = buffer[k * 2 + j] & key[i];
        if (flag) {
          // Draw_Px(x + i + j * 8, y + k, color);
          vram[(y + k) * xsize + (x + i + j * 8)] = color;
        }
      }
    }
  }
}

void putfont(vram_t *vram, int xsize, int x, int y, color_t c, char *font) {
  int i;
  vram_t *p, d /* data */;

  for (i = 0; i < 16; i++) {
    p = vram + (y + i) * xsize + x;
    d = font[i];
    if ((d & 0x80) != 0) {
      p[0] = c;
    }
    if ((d & 0x40) != 0) {
      p[1] = c;
    }
    if ((d & 0x20) != 0) {
      p[2] = c;
    }
    if ((d & 0x10) != 0) {
      p[3] = c;
    }
    if ((d & 0x08) != 0) {
      p[4] = c;
    }
    if ((d & 0x04) != 0) {
      p[5] = c;
    }
    if ((d & 0x02) != 0) {
      p[6] = c;
    }
    if ((d & 0x01) != 0) {
      p[7] = c;
    }
  }
  return;
}

void putfonts_asc(vram_t *vram, int xsize, int x, int y, color_t c,
                  unsigned char *s) {
  int flag = 0;
  char *hankaku = ascfont;
  /* C语言中，字符串都是以0x00结尾 */
  for (; *s != 0x00; s++) {
    if (*s > 0x80 && (*(s + 1) != 0x00 || flag)) {
      if (flag) {
        s--;
        PUTCHINESE0(vram, x, y, c, *(unsigned short *)(s), xsize);
        x += 16;
        flag = 0;
        s++;
      } else {
        flag = 1;
      }
      continue;
    }
    putfont(vram, xsize, x, y, c, hankaku + *s * 16);
    x += 8;
  }
  return;
}
bool Collision(int x, int y, int w, int h, int x1, int y1) {
  // 矩形碰撞检测
  // x,y是矩形坐标
  // w,h是矩形宽高
  // x1,y1是需要检测的点
  if (x1 <= x + w && x1 >= x && y1 <= y + h && y1 >= y) {
    return true;
  }
  return false;
}