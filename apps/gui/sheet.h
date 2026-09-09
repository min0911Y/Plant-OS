#ifndef GUI_SHEET_H
#define GUI_SHEET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int vram_t;
struct SHEET {
  vram_t *buf;
  int bxsize, bysize, vx0, vy0, col_inv, height;
  struct SHTCTL *ctl;
  struct window *wnd;
  struct SHEET *next;
};
struct SHTCTL {
  vram_t *vram;
  struct SHEET **map;
  int xsize, ysize, top;
  size_t stride;
  uint8_t red_shift, green_shift, blue_shift;
  struct SHEET **sheets;
  struct SHEET *allocated;
  size_t count, capacity;
};

struct SHTCTL *shtctl_init(vram_t *vram, int xsize, int ysize);
void ctl_free(struct SHTCTL *ctl);
void sheet_refreshsub(struct SHTCTL *ctl, int x, int y, int x1, int y1, int h0,
                      int h1);
struct SHEET *sheet_alloc(struct SHTCTL *ctl);
void sheet_setbuf(struct SHEET *sht, vram_t *buf, int xsize, int ysize,
                  int col_inv);
void sheet_updown(struct SHEET *sht, int height);
void sheet_refresh(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_slide(struct SHEET *sht, int vx0, int vy0);
void sheet_free(struct SHEET *sht);

#endif
