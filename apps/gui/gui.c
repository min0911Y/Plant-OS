#include "gui.h"
#include "../libutf/include/utf.h"
#include <math.h>
#include <stb_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

static struct VBEINFO *vinfo;
desktop_t *desktop0;
static void click1(button_t *button) {
  window_t *a = create_window(desktop0, "console1", 800, 600, NowTaskID());
  a->display(a, 0, 0, 3);
  create_console(a, 40 * 8, 20 * 16, 4, 24);
}

unsigned char *ascfont, *hzkfont;
void handle(uint32_t *a) { logkf("%c", a[2]); }
char *ttf_buffer;
stbtt_fontinfo font;
uint32_t LCD_AlphaBlend(uint32_t foreground_color, uint32_t background_color,
                        uint8_t alpha) {
  uint8_t *fg = (uint8_t *)&foreground_color;
  uint8_t *bg = (uint8_t *)&background_color;

  uint32_t rb = (((uint32_t)(*fg & 0xFF) * alpha) +
                 ((uint32_t)(*bg & 0xFF) * (256 - alpha))) >>
                8;
  uint32_t g = (((uint32_t)(*(fg + 1) & 0xFF) * alpha) +
                ((uint32_t)(*(bg + 1) & 0xFF) * (256 - alpha))) >>
               8;
  uint32_t a = (((uint32_t)(*(fg + 2) & 0xFF) * alpha) +
                ((uint32_t)(*(bg + 2) & 0xFF) * (256 - alpha))) >>
               8;

  return (rb & 0xFF) | ((g & 0xFF) << 8) | ((a & 0xFF) << 16);
}
void set_size(int s1) {}
#define EPS (2.22044604925031308085e-16)
static const float_t toint = 1 / EPS;

float roundf(float x) {
  union {
    float f;
    uint32_t i;
  } u = {x};
  int e = u.i >> 23 & 0xff;
  float_t y;

  if (e >= 0x7f + 23)
    return x;
  if (u.i >> 31)
    x = -x;
  if (e < 0x7f - 1) {
    FORCE_EVAL(x + toint);
    return 0 * u.f;
  }
  y = x + toint - toint - x;
  if (y > 0.5f)
    y = y + x - 1;
  else if (y <= -0.5f)
    y = y + x + 1;
  else
    y = y + x;
  if (u.i >> 31)
    y = -y;
  return y;
}
char *TTF_Print(vram_t *vram, unsigned xsize, int xpos, int y_shift, unsigned c,
                int *buf, unsigned bc, unsigned *width, unsigned *heigh) {
  /* 创建位图 */
  int bitmap_w = 512; /* 位图的宽 */
  int bitmap_h = 128; /* 位图的高 */
  unsigned char *bitmap = calloc(bitmap_w * bitmap_h, sizeof(unsigned char));

  /* "STB"的 unicode 编码 */
  int *word = buf;

  /* 计算字体缩放 */
  float pixels = 30.0; /* 字体大小（字号） */
  float scale = stbtt_ScaleForPixelHeight(
      &font, pixels); /* scale = pixels / (ascent - descent) */

  /**
   * 获取垂直方向上的度量
   * ascent：字体从基线到顶部的高度；
   * descent：基线到底部的高度，通常为负值；
   * lineGap：两个字体之间的间距；
   * 行间距为：ascent - descent + lineGap。
   */
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

  /* 根据缩放调整字高 */
  ascent = roundf(ascent * scale);
  descent = roundf(descent * scale);

  int x = 0; /*位图的x*/
  int max = 0;
  /* 循环加载word中每个字符 */
  unsigned h = (int)((float)(ascent - descent + lineGap * scale));
  for (int i = 0; word[i] != 0; ++i) {
    /**
     * 获取水平方向上的度量
     * advanceWidth：字宽；
     * leftSideBearing：左侧位置；
     */
    int advanceWidth = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&font, word[i], &advanceWidth, &leftSideBearing);

    /* 获取字符的边框（边界） */
    int c_x1, c_y1, c_x2, c_y2;
    stbtt_GetCodepointBitmapBox(&font, word[i], scale, scale, &c_x1, &c_y1,
                                &c_x2, &c_y2);

    /* 计算位图的y (不同字符的高度不同） */
    int y = ascent + c_y1;
    // if (y > max)
    //   max = y;
    /* 渲染字符 */
    int byteOffset = x + roundf(leftSideBearing * scale) + (y * bitmap_w);
    stbtt_MakeCodepointBitmap(&font, bitmap + byteOffset, c_x2 - c_x1,
                              c_y2 - c_y1, bitmap_w, scale, scale, word[i]);

    /* 调整x */
    x += roundf(advanceWidth * scale);

    /* 调整字距 */
    int kern;
    kern = stbtt_GetCodepointKernAdvance(&font, word[i], word[i + 1]);
    x += roundf(kern * scale);
  }
  *width = x;
  *heigh = max + h;
  return bitmap;
}
void put_bitmap(unsigned char *bitmap, vram_t *vram, unsigned x, unsigned y,
                unsigned width, unsigned heigh, unsigned bitmap_xsize,
                unsigned xsize, unsigned fc, unsigned bc) {
  for (int i = 0; i < width; i++) {
    for (int j = 0; j < heigh; j++) {
      vram[(y + j) * xsize + (x + i)] =
          LCD_AlphaBlend(fc, bc, bitmap[j * bitmap_xsize + i]);
    }
  }
}

void print_box_ttf(struct SHEET *sht, vram_t *vram, char *buf, unsigned fc,
                   unsigned bc, unsigned x, unsigned y, unsigned xsize) {
  Rune *r = (Rune *)malloc((utflen(buf) + 1) * sizeof(Rune));
  r[utflen(buf)] = 0;
  int i = 0;
  while (*buf != '\0') {
    buf += chartorune(&(r[i++]), buf);
  }
  unsigned width, heigh;
  char *bitmap = TTF_Print(vram, xsize, x, y, fc, r, bc, &width, &heigh);
  SDraw_Box(vram, x, y, x + width, y + heigh, bc, xsize);
  sheet_refresh(sht, x, y, x + width, y + heigh);
  put_bitmap(bitmap, vram, x, y, width, heigh, 512, xsize, fc, bc);
  sheet_refresh(sht, x, y, x + width, y + heigh);
  free(bitmap);
  free(r);
}
enum {
  EDI,
  ESI,
  EBP,
  ESP,
  EBX,
  EDX,
  ECX,
  EAX,
  M_PDE,
  C_PDE
}; // M_PDE是GUI程序的PDE，C_PDE是调用者的PDE
enum { MOUSE_STAY = 1, MOUSE_CLICK_LEFT, MOUSE_CLICK_RIGHT, CLOSE_WINDOW };
#define PACK_XY(x, y) ((x << 16) | y)
void handle_stay_api(window_t *window, gmouse_t *gmouse) {
  // queue_push(window->events, MOUSE_STAY);
  // queue_push(window->events, PACK_XY(gmouse->x, gmouse->y));
  fifo32_put(window->events, MOUSE_STAY);
  fifo32_put(window->events,
             PACK_XY(gmouse->x - window->x, gmouse->y - window->y));
}
void handle_click_left_api(window_t *window, gmouse_t *gmouse) {
  // queue_push(window->events, MOUSE_CLICK_LEFT);
  // queue_push(window->events, PACK_XY(gmouse->x, gmouse->y));
  fifo32_put(window->events, MOUSE_CLICK_LEFT);
  fifo32_put(window->events,
             PACK_XY(gmouse->x - window->x, gmouse->y - window->y));
}
void handle_click_right_api(window_t *window, gmouse_t *gmouse) {
  // queue_push(window->events, MOUSE_CLICK_RIGHT);
  // queue_push(window->events, PACK_XY(gmouse->x, gmouse->y));
  fifo32_put(window->events, MOUSE_CLICK_RIGHT);
  fifo32_put(window->events,
             PACK_XY(gmouse->x - window->x, gmouse->y - window->y));
}
void handle_close_window(window_t *window) {
  // queue_push(window->events, CLOSE_WINDOW);
  fifo32_put(window->events, CLOSE_WINDOW);
}
void sheet_refresh(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
void gui_api1(uint32_t *a) {
  if (a[EAX] == 0x01) {
    window_t *wnd = create_window(desktop0, a[EBX], a[EDI], a[ESI], 0);
    wnd->events = (struct FIFO32 *)malloc(sizeof(struct FIFO32));
    fifo32_init(wnd->events, 32, wnd->event);
    wnd->handle_stay = handle_stay_api;
    wnd->handle_left_for_api = handle_click_left_api;
    wnd->handle_right = handle_click_right_api;
    wnd->close = handle_close_window;
    wnd->display(wnd, a[ECX], a[EDX], 1);
    a[EAX] = wnd;
  } else if (a[EAX] == 0x02) {
    window_t *wnd = (window_t *)a[ECX];
    a[EAX] = fifo32_get(wnd->events);
  } else if (a[EAX] == 0x03) {
    window_t *wnd = (window_t *)a[ECX];
    free(wnd->events);
    if (wnd->console != NULL) {
      wnd->console->close(wnd->console);
    } else if (wnd->super_window != NULL) {
      wnd->super_window->close(wnd->super_window);
    }
    int count = 1;
    for (; list_search_by_count(count, wnd->desktop->window_list)->val !=
           (uintptr_t)wnd;
         count++)
      ;
    list_delete_by_count(count, wnd->desktop->window_list);
    sheet_free(wnd->sht);
    free((void *)wnd->vram);
    free((void *)wnd);
  } else if (a[EAX] == 0x04) {
    window_t *wnd = (window_t *)a[ECX];
    wnd->vram[a[EDX] * wnd->xsize + a[ESI]] = a[EDI];
  } else if (a[EAX] == 0x05) {
    window_t *wnd = (window_t *)a[ECX];
    sheet_refresh(wnd->sht, a[EDX] >> 16, a[EDX] & 0xffff, a[ESI] >> 16,
                  a[ESI] & 0xffff);
  } else if (a[EAX] == 0x06) // 设置framebuffer
  {
    window_t *wnd = (window_t *)a[ECX];
    unsigned buf = wnd->vram;
    mem_map(0xfd000000, wnd->xsize * wnd->ysize * 4 + (buf & 0xfff), a[C_PDE],
            buf, a[M_PDE]);
    a[EAX] = 0xfd000000 + (buf & 0xfff);
  } else if (a[EAX] == 0x07) {
    window_t *wnd = (window_t *)a[ECX];
    wnd->fifo_keypress = malloc(sizeof(struct FIFO8));
    uint8_t *buf = (uint8_t *)malloc(128);
    fifo8_init(wnd->fifo_keypress, 128, buf);
    wnd->fifo_keyup = malloc(sizeof(struct FIFO8));
    uint8_t *buf1 = (uint8_t *)malloc(128);
    fifo8_init(wnd->fifo_keyup, 128, buf1);

  } else if (a[EAX] == 0x08) {
    window_t *wnd = (window_t *)a[ECX];
    free(wnd->fifo_keypress->buf);
    free(wnd->fifo_keypress);
    wnd->fifo_keypress = NULL;
    free(wnd->fifo_keyup->buf);
    free(wnd->fifo_keyup);
    wnd->fifo_keyup = NULL;
  } else if (a[EAX] == 0x09) {
    window_t *wnd = (window_t *)a[ECX];
    a[EAX] = fifo8_status(wnd->fifo_keypress);
  } else if (a[EAX] == 0x0a) {
    window_t *wnd = (window_t *)a[ECX];
    a[EAX] = fifo8_get(wnd->fifo_keypress);
  } else if (a[EAX] == 0x0b) {
    window_t *wnd = (window_t *)a[ECX];
    a[EAX] = fifo8_status(wnd->fifo_keyup);
  } else if (a[EAX] == 0x0c) {
    window_t *wnd = (window_t *)a[ECX];
    a[EAX] = fifo8_get(wnd->fifo_keyup);
  }
}
void main() {
  ttf_buffer = malloc(filesize("font.ttf"));
  unsigned char buf[100];
  printf("Reading font...");
  api_ReadFile("font.ttf", ttf_buffer);
  printf("Done.\n");
  stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));

  set_size(15);
  if (filesize("A:\\other\\font.bin") == -1 ||
      filesize("A:\\other\\HZK16") == -1) {
    print("error\n");
  }
  printf("\n\n");
  int xsize_input, ysize_input;
  char *buffer_input;
re:
  xsize_input = 1024;
  ysize_input = 768;
  unsigned vram;
  vram = set_mode(xsize_input, ysize_input);

  set_custom_handler(gui_api1);
  logkf("vram = %08x\n", vram);
  ascfont = (unsigned char *)malloc(filesize("A:\\other\\font.bin"));
  hzkfont = (unsigned char *)malloc(filesize("A:\\other\\HZK16"));
  api_ReadFile("A:\\other\\font.bin", ascfont);
  api_ReadFile("A:\\other\\HZK16", hzkfont);
  desktop0 = create_desktop(xsize_input, ysize_input, NowTaskID());
  desktop0->display(desktop0, vram);
  desktop0->draw(desktop0, 0, 0, xsize_input, ysize_input,
                 argb(0, 58, 110, 165));

  // print_box_ttf(desktop0->sht, desktop0->vram,
  //               "原神，启动！Genshin Impact Start!", COL_000000,
  //               COL_C6C6C6, 10, 30, desktop0->xsize);

  // desktop0->draw(desktop0, 10, 30, 18 + 48 * 8 + 8, 30 + 16, COL_C6C6C6);

  // desktop0->puts(desktop0, "Power Desktop powered by Powerint DOS 386
  // kernel",
  //                18, 30, COL_000000);

  window_t *window2 =
      create_window(desktop0, "console", 80 * 8 + 8, 25 * 16 + 28, NowTaskID());
  window_t *window3 =
      create_window(desktop0, "console", 40 * 8 + 8, 20 * 16 + 28, NowTaskID());

  window2->display(window2, 250, 250, 3);
  window2->display(window3, 400, 400, 4);
  gmouse_t *gmouse0 =
      create_gmouse(desktop0, desktop0->xsize / 2, desktop0->ysize / 2, 5);
  console_t *console0 = create_console(window2, 80 * 8, 25 * 16, 4, 24);
  // console_t *console1 = create_console(window3, 40 * 8, 20 * 16, 4, 24);

  window_t *window0 = create_window(desktop0, "hello0", 200, 200, NowTaskID());
  window_t *window1 = create_window(desktop0, "hello1", 200, 200, NowTaskID());
  window0->puts(window0, "Hello world!", 52, 92, COL_000000);
  super_window_t *super_window0 = create_super_window(window1);
  window0->display(window0, 100, 100, 1);
  window1->display(window1, 200, 200, 2);
  button_t *button0 =
      create_button(super_window0, "test", 100, 20, 50, 50, click1);
  textbox_t *textbox0 = create_textbox(super_window0, 15 * 8, 16, 4, 110);
  unsigned clock1 = clock();
  time_t rawtime;
  struct tm *info;
  char buffer[80];
  clock1 = clock();

  rawtime = time(&rawtime);

  info = localtime(&rawtime);

  strftime(buffer, 80, "当前时间：%Y-%m-%d %H:%M:%S", info);
  print_box_ttf(desktop0->sht, desktop0->vram, buffer, COL_000000,
                argb(0, 58, 110, 165), 512 - 200, 0, desktop0->xsize);

  for (;;) {
    if (clock() - clock1 >= 1000) {
      clock1 = clock();

      rawtime = time(&rawtime);

      info = localtime(&rawtime);

      strftime(buffer, 80, "当前时间：%Y-%m-%d %H:%M:%S", info);
      print_box_ttf(desktop0->sht, desktop0->vram, buffer, COL_000000,
                    argb(0, 58, 110, 165), 512 - 200, 0, desktop0->xsize);
    } else {
      api_yield();
    }
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