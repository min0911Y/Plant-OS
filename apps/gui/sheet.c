/* Window composition. Pixel ownership uses stable sheet pointers; the visible
 * order grows independently, without a fixed window count or narrow IDs. */
#include "gui.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct SHTCTL *shtctl_init(vram_t *vram, int xsize, int ysize) {
  if (xsize <= 0 || ysize <= 0 ||
      (size_t)xsize > SIZE_MAX / sizeof(struct SHEET *) / (size_t)ysize)
    return NULL;
  struct SHTCTL *ctl = calloc(1, sizeof(*ctl));
  if (!ctl)
    return NULL;
  ctl->map = calloc((size_t)xsize * ysize, sizeof(*ctl->map));
  if (!ctl->map) {
    free(ctl);
    return NULL;
  }
  ctl->vram = vram;
  ctl->xsize = xsize;
  ctl->ysize = ysize;
  ctl->stride = xsize;
  ctl->red_shift = 16;
  ctl->green_shift = 8;
  ctl->top = -1;
  return ctl;
}

void ctl_free(struct SHTCTL *ctl) {
  if (!ctl)
    return;
  while (ctl->allocated) {
    struct SHEET *sheet = ctl->allocated;
    ctl->allocated = sheet->next;
    free(sheet);
  }
  free(ctl->map);
  free(ctl->sheets);
  free(ctl);
}

struct SHEET *sheet_alloc(struct SHTCTL *ctl) {
  if (!ctl || ctl->count == INT_MAX)
    return NULL;
  if (ctl->count == ctl->capacity) {
    size_t capacity = ctl->capacity ? ctl->capacity * 2 : 16;
    if (capacity < ctl->capacity || capacity > SIZE_MAX / sizeof(*ctl->sheets))
      return NULL;
    struct SHEET **sheets = realloc(ctl->sheets, capacity * sizeof(*sheets));
    if (!sheets)
      return NULL;
    ctl->sheets = sheets;
    ctl->capacity = capacity;
  }
  struct SHEET *sheet = calloc(1, sizeof(*sheet));
  if (!sheet)
    return NULL;
  sheet->height = -1;
  sheet->ctl = ctl;
  sheet->next = ctl->allocated;
  ctl->allocated = sheet;
  ctl->count++;
  return sheet;
}

void sheet_setbuf(struct SHEET *sheet, vram_t *buffer, int width, int height,
                  int transparent) {
  sheet->buf = buffer;
  sheet->bxsize = width;
  sheet->bysize = height;
  sheet->col_inv = transparent;
}

void sheet_refreshsub(struct SHTCTL *ctl, int x0, int y0, int x1, int y1,
                      int h0, int h1) {
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > ctl->xsize)
    x1 = ctl->xsize;
  if (y1 > ctl->ysize)
    y1 = ctl->ysize;
  bool native =
      ctl->red_shift == 16 && ctl->green_shift == 8 && !ctl->blue_shift;
  for (int y = y0; y < y1; y++) {
    struct SHEET **owners = ctl->map + (size_t)y * ctl->xsize;
    for (int x = x0; x < x1;) {
      struct SHEET *sheet = owners[x];
      int first = x++;
      while (x < x1 && owners[x] == sheet)
        x++;
      if (!sheet || sheet->height < h0 || sheet->height > h1)
        continue;
      const vram_t *source = sheet->buf +
                             (size_t)(y - sheet->vy0) * sheet->bxsize + first -
                             sheet->vx0;
      vram_t *target = ctl->vram + (size_t)y * ctl->stride + first;
      if (native) {
        memcpy(target, source, (size_t)(x - first) * sizeof(*target));
      } else {
        for (int i = 0; i < x - first; i++) {
          uint32_t color = source[i];
          target[i] = ((color >> 16) & 255) << ctl->red_shift |
                      ((color >> 8) & 255) << ctl->green_shift |
                      (color & 255) << ctl->blue_shift;
        }
      }
    }
  }
}

/* Visit the frontmost sheets first. Once every pixel has an owner, completely
 * covered windows need no work, even with thousands of overlapping windows. */
static void sheet_recompose(struct SHTCTL *ctl, int x0, int y0, int x1,
                            int y1) {
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > ctl->xsize)
    x1 = ctl->xsize;
  if (y1 > ctl->ysize)
    y1 = ctl->ysize;
  if (x0 >= x1 || y0 >= y1)
    return;
  for (int y = y0; y < y1; y++)
    memset(ctl->map + (size_t)y * ctl->xsize + x0, 0,
           (size_t)(x1 - x0) * sizeof(*ctl->map));
  size_t remaining = (size_t)(x1 - x0) * (y1 - y0);
  for (int h = ctl->top; h >= 0 && remaining; h--) {
    struct SHEET *sheet = ctl->sheets[h];
    int left = x0 > sheet->vx0 ? x0 : sheet->vx0;
    int top = y0 > sheet->vy0 ? y0 : sheet->vy0;
    int right =
        x1 < sheet->vx0 + sheet->bxsize ? x1 : sheet->vx0 + sheet->bxsize;
    int bottom =
        y1 < sheet->vy0 + sheet->bysize ? y1 : sheet->vy0 + sheet->bysize;
    for (int y = top; y < bottom; y++) {
      struct SHEET **owners = ctl->map + (size_t)y * ctl->xsize;
      for (int x = left; x < right; x++) {
        if (owners[x])
          continue;
        if (sheet->col_inv != -1 &&
            sheet->buf[(size_t)(y - sheet->vy0) * sheet->bxsize + x -
                       sheet->vx0] == (vram_t)sheet->col_inv)
          continue;
        owners[x] = sheet;
        remaining--;
      }
    }
  }
  sheet_refreshsub(ctl, x0, y0, x1, y1, 0, ctl->top);
}

void sheet_updown(struct SHEET *sheet, int height) {
  struct SHTCTL *ctl = sheet->ctl;
  int old = sheet->height;
  int maximum = old < 0 ? ctl->top + 1 : ctl->top;
  if (height > maximum)
    height = maximum;
  if (height < -1)
    height = -1;
  if (old == height)
    return;
  if (old >= 0) {
    for (int i = old; i < ctl->top; i++) {
      ctl->sheets[i] = ctl->sheets[i + 1];
      ctl->sheets[i]->height = i;
    }
    ctl->top--;
  }
  if (height >= 0) {
    for (int i = ctl->top; i >= height; i--) {
      ctl->sheets[i + 1] = ctl->sheets[i];
      ctl->sheets[i + 1]->height = i + 1;
    }
    ctl->sheets[height] = sheet;
    ctl->top++;
  }
  sheet->height = height;
  sheet_recompose(ctl, sheet->vx0, sheet->vy0, sheet->vx0 + sheet->bxsize,
                  sheet->vy0 + sheet->bysize);
}

void sheet_refresh(struct SHEET *sheet, int x0, int y0, int x1, int y1) {
  if (sheet->height >= 0)
    sheet_refreshsub(sheet->ctl, sheet->vx0 + x0, sheet->vy0 + y0,
                     sheet->vx0 + x1, sheet->vy0 + y1, sheet->height,
                     sheet->height);
}

void sheet_slide(struct SHEET *sheet, int x, int y) {
  int old_x = sheet->vx0, old_y = sheet->vy0;
  sheet->vx0 = x;
  sheet->vy0 = y;
  if (sheet->height >= 0) {
    sheet_recompose(sheet->ctl, old_x, old_y, old_x + sheet->bxsize,
                    old_y + sheet->bysize);
    sheet_recompose(sheet->ctl, x, y, x + sheet->bxsize, y + sheet->bysize);
  }
}

void sheet_free(struct SHEET *sheet) {
  if (!sheet)
    return;
  sheet_updown(sheet, -1);
  struct SHTCTL *ctl = sheet->ctl;
  for (struct SHEET **link = &ctl->allocated; *link; link = &(*link)->next) {
    if (*link == sheet) {
      *link = sheet->next;
      ctl->count--;
      free(sheet);
      return;
    }
  }
}
