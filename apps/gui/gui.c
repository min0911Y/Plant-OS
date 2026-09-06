#include "gui.h"
#include "../libutf/include/utf.h"
#include <math.h>
#include <stb_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <time.h>
#include <rpc.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
desktop_t *desktop0;
static void *load_file(const char *path, uint32_t *size) {
  struct stat status;
  if (stat(path, &status) != 0) {
    return NULL;
  }
  void *buffer = malloc(status.st_size);
  FILE *stream = fopen(path, "rb");
  if (buffer == NULL || stream == NULL ||
      fread(buffer, 1, status.st_size, stream) != status.st_size) {
    if (stream != NULL) {
      fclose(stream);
    }
    free(buffer);
    return NULL;
  }
  fclose(stream);
  if (size != NULL) {
    *size = status.st_size;
  }
  return buffer;
}
typedef struct gui_launch {
  struct gui_launch *next;
  int tid;
  bool finished;
  unsigned char stack[32 * 1024];
} gui_launch_t;

static gui_launch_t *launches;

static void terminal_task(gui_launch_t *launch) {
  int status = exec("term.bin", "term.bin");
  if (status != 0)
    logkf("GUI: term exited with status %d\n", status);
  __atomic_store_n(&launch->finished, true, __ATOMIC_RELEASE);
  _exit(status);
}

static void launch_terminal(button_t *button) {
  (void)button;
  gui_launch_t *launch = calloc(1, sizeof(*launch));
  if (launch == NULL)
    return;
  TaskLock();
  launch->tid = AddThread("launch", (uintptr_t)terminal_task,
                          (uintptr_t)(launch->stack + sizeof(launch->stack)),
                          (uintptr_t)launch);
  if (launch->tid < 0) {
    free(launch);
    logkf("GUI: could not start term\n");
  } else {
    launch->next = launches;
    launches = launch;
  }
  TaskUnlock();
}

unsigned char *ascfont, *hzkfont;
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
static void draw_text(struct SHEET *sheet, const char *text,
                      uint32_t foreground, uint32_t background_color, int x,
                      int y, const vram_t *background) {
  float scale = stbtt_ScaleForPixelHeight(&font, 30.0f);
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
  int baseline = y + (int)roundf(ascent * scale);
  int left = x, right = x, top = y;
  int bottom = y + (int)ceilf((ascent - descent + gap) * scale);
  int pen = x;
  for (const char *cursor = text; *cursor;) {
    Rune codepoint, next = 0;
    cursor += chartorune(&codepoint, cursor);
    if (*cursor)
      chartorune(&next, cursor);
    int advance, bearing, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &bearing);
    stbtt_GetCodepointBitmapBox(&font, codepoint, scale, scale, &x0, &y0, &x1,
                                &y1);
    if (pen + x0 < left)
      left = pen + x0;
    if (pen + x1 > right)
      right = pen + x1;
    if (baseline + y0 < top)
      top = baseline + y0;
    if (baseline + y1 > bottom)
      bottom = baseline + y1;
    pen += (int)roundf(
        (advance + stbtt_GetCodepointKernAdvance(&font, codepoint, next)) *
        scale);
  }
  if (pen > right)
    right = pen;
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right > sheet->bxsize)
    right = sheet->bxsize;
  if (bottom > sheet->bysize)
    bottom = sheet->bysize;
  if (left >= right || top >= bottom)
    return;
  for (int row = top; row < bottom; row++) {
    for (int column = left; column < right; column++) {
      size_t offset = (size_t)row * sheet->bxsize + column;
      sheet->buf[offset] = background ? background[offset] : background_color;
    }
  }
  pen = x;
  for (const char *cursor = text; *cursor;) {
    Rune codepoint, next = 0;
    cursor += chartorune(&codepoint, cursor);
    if (*cursor)
      chartorune(&next, cursor);
    struct {
      unsigned char *pixels;
      int width, height, x, y;
    } glyph;
    glyph.pixels =
        stbtt_GetCodepointBitmap(&font, scale, scale, codepoint, &glyph.width,
                                 &glyph.height, &glyph.x, &glyph.y);
    if (glyph.pixels) {
      for (int row = 0; row < glyph.height; row++) {
        int target_y = baseline + glyph.y + row;
        if (target_y < top || target_y >= bottom)
          continue;
        for (int column = 0; column < glyph.width; column++) {
          int target_x = pen + glyph.x + column;
          if (target_x < left || target_x >= right)
            continue;
          size_t offset = (size_t)target_y * sheet->bxsize + target_x;
          unsigned char alpha = glyph.pixels[row * glyph.width + column];
          sheet->buf[offset] =
              LCD_AlphaBlend(foreground, sheet->buf[offset], alpha);
        }
      }
      stbtt_FreeBitmap(glyph.pixels, font.userdata);
    }
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &bearing);
    pen += (int)roundf(
        (advance + stbtt_GetCodepointKernAdvance(&font, codepoint, next)) *
        scale);
  }
  sheet_refresh(sheet, left, top, right, bottom);
}
void convert_ABGR_to_ARGB(uint32_t *bitmap, size_t num_pixels) {
  for (size_t i = 0; i < num_pixels; ++i) {
    uint32_t pixel = bitmap[i];
    uint8_t alpha = (pixel >> 24) & 0xFF;
    uint8_t red = (pixel >> 16) & 0xFF;
    uint8_t green = (pixel >> 8) & 0xFF;
    uint8_t blue = pixel & 0xFF;
    bitmap[i] = (alpha << 24) | (blue << 16) | (green << 8) | red;
  }
}
void main() {
  int rpc_status = gui_rpc_service_start();
  if (rpc_status != RPC_OK) {
    logkf("GUI RPC service failed: %d\n", rpc_status);
    return;
  }
  // char *s34 = malloc(64*1024*1024);
  // free(s34);
  ttf_buffer = load_file("font.ttf", NULL);
  printf("Reading font...");
  if (ttf_buffer == NULL) {
    return;
  }
  printf("Done.\n");
  stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));

  printf("\n\n");
  int xsize_input, ysize_input;
  xsize_input = 1024;
  ysize_input = 768;
  uintptr_t vram;
  vram = set_mode(xsize_input, ysize_input);

  if (vram == (uintptr_t)-1) {
    logkf("GUI failed to acquire %dx%dx32 framebuffer\n", xsize_input,
          ysize_input);
    return;
  }
  framebuffer_info_t framebuffer;
  if (framebuffer_info(&framebuffer) < 0 || framebuffer.bpp != 32 ||
      framebuffer.pitch % sizeof(vram_t) ||
      framebuffer.pitch / sizeof(vram_t) < framebuffer.width) {
    logkf("GUI framebuffer layout unavailable\n");
    return;
  }
  logkf("GUI framebuffer %ux%u pitch=%u\n", framebuffer.width,
        framebuffer.height, framebuffer.pitch);
  xsize_input = framebuffer.width;
  ysize_input = framebuffer.height;
  ascfont = load_file("font.bin", NULL);
  hzkfont = load_file("HZK16", NULL);
  if (ascfont == NULL || hzkfont == NULL) {
    print("font load error\n");
    return;
  }
  desktop0 = create_desktop(xsize_input, ysize_input, NowTaskID());
  if (desktop0 == NULL) {
    logkf("GUI failed to create desktop\n");
    return;
  }
  desktop0->display(desktop0, &framebuffer);
  desktop0->draw(desktop0, 0, 0, xsize_input, ysize_input,
                 argb(0, 58, 110, 165));
  vram_t *background = NULL;
  struct stat background_status;
  if (stat("123.png", &background_status) == 0) {
    int w, h, bpp;
    stbi_uc *b = stbi_load("123.png", &w, &h, &bpp, 4);
    if (!b) {
      goto A;
    }
    if (w > xsize_input && h > ysize_input) {
      uint8_t *b1 = malloc(xsize_input * ysize_input * 4);
      stbir_resize_uint8(b, w, h, 0, b1, xsize_input, ysize_input, 0, 4);
      convert_ABGR_to_ARGB((uint32_t *)b1, xsize_input * ysize_input);
      uint32_t *buff = (uint32_t *)b1;
      background = buff;
      for (int i = 0; i < xsize_input; i++) {
        for (int j = 0; j < ysize_input; j++) {
          desktop0->vram[j * xsize_input + i] = buff[j * xsize_input + i];
        }
      }
    } else if (w > xsize_input) {
      uint8_t *b1 = malloc(xsize_input * h * 4);
      stbir_resize_uint8(b, w, h, 0, b1, xsize_input, h, 0, 4);
      convert_ABGR_to_ARGB((uint32_t *)b1, xsize_input * h);
      uint32_t *buff = (uint32_t *)b1;
      background = buff;
      for (int i = 0; i < xsize_input; i++) {
        for (int j = 0; j < h; j++) {
          desktop0->vram[j * xsize_input + i] = buff[j * xsize_input + i];
        }
      }
    } else if (h > ysize_input) {
      uint8_t *b1 = malloc(w * ysize_input * 4);
      stbir_resize_uint8(b, w, h, 0, b1, w, ysize_input, 0, 4);
      convert_ABGR_to_ARGB((uint32_t *)b1, w * ysize_input);
      uint32_t *buff = (uint32_t *)b1;
      background = buff;
      for (int i = 0; i < w; i++) {
        for (int j = 0; j < ysize_input; j++) {
          desktop0->vram[j * xsize_input + i] = buff[j * w + i];
        }
      }
    } else {
      convert_ABGR_to_ARGB((uint32_t *)b, w * h);
      uint32_t *buff = (uint32_t *)b;
      background = buff;
      for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
          desktop0->vram[j * xsize_input + i] = buff[j * w + i];
        }
      }
    }
    sheet_refresh(desktop0->sht, 0, 0, xsize_input, ysize_input);
  } else {
  A:

    desktop0->draw(desktop0, 0, 0, xsize_input, ysize_input,
                   argb(0, 58, 110, 165));
  }

  // desktop0->draw(desktop0, 10, 30, 18 + 48 * 8 + 8, 30 + 16, COL_C6C6C6);

  // desktop0->puts(desktop0, "Power Desktop powered by Powerint DOS 386
  // kernel",
  //                18, 30, COL_000000);

  gmouse_t *gmouse0 =
      create_gmouse(desktop0, desktop0->xsize / 2, desktop0->ysize / 2, 5);
  if (gmouse0 == NULL) {
    logkf("GUI failed to create input\n");
    return;
  }

  window_t *window1 =
      create_window(desktop0, "ToolBox", 200, 200, NowTaskID());
  if (window1 == NULL) {
    logkf("GUI failed to create toolbox window\n");
    return;
  }
  super_window_t *super_window0 = create_super_window(window1);
  if (super_window0 == NULL) {
    close_window(window1);
    logkf("GUI failed to create toolbox\n");
    return;
  }
  window1->display(window1, 200, 200);
  button_t *button0 = create_button(super_window0, "Terminal", 100, 20, 50, 50,
                                    launch_terminal);
  if (button0 == NULL) {
    close_window(window1);
    logkf("GUI failed to create toolbox button\n");
    return;
  }
  launch_terminal(NULL);
  unsigned clock1 = (unsigned)clock() - 1000;
  for (;;) {
    unsigned elapsed = (unsigned)clock() - clock1;
    if (elapsed >= 1000) {
      clock1 = clock();
      TaskLock();
      for (gui_launch_t **link = &launches; *link;) {
        gui_launch_t *launch = *link;
        if (!__atomic_load_n(&launch->finished, __ATOMIC_ACQUIRE)) {
          link = &launch->next;
          continue;
        }
        SubThread(launch->tid);
        *link = launch->next;
        free(launch);
      }
      TaskUnlock();
      time_t now = time(NULL);
      if (now == (time_t)-1)
        continue;
      struct tm calendar;
      char buffer[80];
      localtime_r(&now, &calendar);
      strftime(buffer, sizeof(buffer), "当前时间：%Y-%m-%d %H:%M:%S", &calendar);
      TaskLock();
      draw_text(desktop0->sht, buffer, COL_FFFFFF, argb(0, 58, 110, 165), 312,
                0, background);
      TaskUnlock();
      continue;
    }
    rpc_status = rpc_serve_once(1000 - elapsed);
    if (rpc_status != RPC_OK && rpc_status != RPC_ERR_TIMEOUT) {
      logkf("GUI RPC service stopped: %d\n", rpc_status);
      return;
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
