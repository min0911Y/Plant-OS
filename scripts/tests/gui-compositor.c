/* Host-side algorithm benchmark: links the production compositor unchanged.
 * RAM timings exclude the guest RPC, scheduler and physical framebuffer. */
#include "sheet.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t random_state = 12345;
static unsigned random_value(void) {
  random_state = random_state * 1664525u + 1013904223u;
  return random_state;
}

static struct SHEET *layer(struct SHTCTL *ctl, int w, int h, int x, int y,
                           bool transparent) {
  struct SHEET *sheet = sheet_alloc(ctl);
  vram_t *pixels = malloc((size_t)w * h * sizeof(*pixels));
  assert(sheet && pixels);
  for (int i = 0; i < w * h; i++)
    pixels[i] = transparent && i % 3 == 0 ? 0x50ffffff : random_value() & 0xffffff;
  sheet_setbuf(sheet, pixels, w, h, transparent ? 0x50ffffff : -1);
  sheet_slide(sheet, x, y);
  sheet_updown(sheet, ctl->top + 1);
  return sheet;
}

static void release(struct SHTCTL *ctl) {
  for (struct SHEET *sheet = ctl->allocated; sheet; sheet = sheet->next)
    free(sheet->buf);
  free(ctl->vram);
  ctl_free(ctl);
}

/* Independent, deliberately simple back-to-front reference renderer. */
static void verify(struct SHTCTL *ctl) {
  for (int y = 0; y < ctl->ysize; y++) {
    for (int x = 0; x < ctl->xsize; x++) {
      struct SHEET *owner = NULL;
      uint32_t color = 0;
      for (int h = 0; h <= ctl->top; h++) {
        struct SHEET *sheet = ctl->sheets[h];
        int bx = x - sheet->vx0, by = y - sheet->vy0;
        if (bx < 0 || by < 0 || bx >= sheet->bxsize || by >= sheet->bysize)
          continue;
        uint32_t pixel = sheet->buf[(size_t)by * sheet->bxsize + bx];
        if (sheet->col_inv != -1 && pixel == (vram_t)sheet->col_inv)
          continue;
        color = ((pixel >> 16) & 255) << ctl->red_shift |
                ((pixel >> 8) & 255) << ctl->green_shift |
                (pixel & 255) << ctl->blue_shift;
        owner = sheet;
      }
      assert(ctl->map[(size_t)y * ctl->xsize + x] == owner);
      assert(ctl->vram[(size_t)y * ctl->stride + x] == color);
    }
    for (size_t x = ctl->xsize; x < ctl->stride; x++)
      assert(ctl->vram[(size_t)y * ctl->stride + x] == 0xabcdef01);
  }
}

static void correctness(void) {
  enum { W = 96, H = 64, STRIDE = 103, N = 12 };
  for (int format = 0; format < 2; format++) {
    vram_t *vram = malloc(STRIDE * H * sizeof(*vram));
    assert(vram);
    for (int i = 0; i < STRIDE * H; i++)
      vram[i] = 0xabcdef01;
    struct SHTCTL *ctl = shtctl_init(vram, W, H);
    assert(ctl);
    ctl->stride = STRIDE;
    ctl->red_shift = format ? 0 : 16;
    ctl->blue_shift = format ? 16 : 0;
    layer(ctl, W, H, 0, 0, false);
    struct SHEET *sheets[N];
    for (int i = 0; i < N; i++)
      sheets[i] = layer(ctl, 17 + i, 13 + i, i * 3, i * 2, i % 2);
    verify(ctl);
    for (int i = 0; i < 800; i++) {
      struct SHEET *sheet = sheets[random_value() % N];
      switch (i % 5) {
      case 0:
        sheet_updown(sheet, -1);
        break;
      case 1:
        sheet_updown(sheet, 1 + random_value() % (ctl->top + 1));
        break;
      case 2:
        sheet_slide(sheet, (int)(random_value() % (W + 60)) - 30,
                     (int)(random_value() % (H + 60)) - 30);
        break;
      case 4: {
        int width = 5 + random_value() % 60;
        int height = 5 + random_value() % 50;
        vram_t *old = sheet->buf;
        vram_t *pixels = malloc((size_t)width * height * sizeof(*pixels));
        assert(pixels);
        for (int pixel = 0; pixel < width * height; pixel++)
          pixels[pixel] = sheet->col_inv != -1 && pixel % 3 == 0
                              ? sheet->col_inv
                              : random_value() & 0xffffff;
        sheet_setbuf(sheet, pixels, width, height, sheet->col_inv);
        free(old);
        break;
      }
      case 3:
        sheet_slide(sheet, sheet->vx0 + (i % 7) - 3, sheet->vy0 + (i % 5) - 2);
        sheet_refresh(sheet, 0, 0, sheet->bxsize, sheet->bysize);
        break;
      }
      verify(ctl);
    }
    for (int i = 0; i < N; i++) {
      free(sheets[i]->buf);
      sheet_free(sheets[i]);
      verify(ctl);
    }
    release(ctl);
  }
}

static uint64_t now(void) {
  struct timespec value;
  assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
  return (uint64_t)value.tv_sec * 1000000000 + value.tv_nsec;
}

int main(int argc, char **argv) {
  int covered = argc == 2 ? atoi(argv[1]) : 0;
  assert(covered >= 0 && covered <= 256);
  correctness();
  enum { W = 1024, H = 768, ITERATIONS = 400 };
  vram_t *vram = calloc(W * H, sizeof(*vram));
  assert(vram);
  struct SHTCTL *ctl = shtctl_init(vram, W, H);
  assert(ctl);
  layer(ctl, W, H, 0, 0, false);
  for (int i = 0; i < covered; i++)
    layer(ctl, 640, 400, 100, 100, false);
  struct SHEET *window = layer(ctl, 640, 400, 100, 100, false);
  struct SHEET *cursor = layer(ctl, 16, 19, 200, 200, true);
  const char *names[] = {"stationary", "horizontal", "vertical", "diagonal", "cursor", "refresh"};
  for (unsigned scenario = 0; scenario < sizeof(names) / sizeof(*names); scenario++) {
    uint64_t start = now();
    for (int i = 0; i < ITERATIONS; i++) {
      int step = i & 1;
      if (scenario == 4)
        sheet_slide(cursor, 200 + step, 200 + step);
      else if (scenario == 5)
        sheet_refresh(window, 0, 0, 640, 400);
      else
        sheet_slide(window, 100 + ((scenario == 1 || scenario == 3) ? step : 0),
                     100 + ((scenario == 2 || scenario == 3) ? step : 0));
    }
    uint64_t elapsed = now() - start;
    verify(ctl);
    printf("%s %.3f\n", names[scenario], (double)elapsed / ITERATIONS / 1000);
  }
  release(ctl);
  return 0;
}
