#include "gui.h"
#include <syscall.h>

static struct List *desktop_list = NULL;

void display_desktop(desktop_t *desktop, vram_t *vram) {
  memcpy((void *)vram, (void *)desktop->shtctl->vram,
         desktop->xsize * desktop->ysize * sizeof(vram_t));
  free((void *)desktop->shtctl->vram);
  desktop->shtctl->vram = vram;
  desktop->is_using     = true;
  return;
}

void hide_desktop(desktop_t *desktop) {
  vram_t *vram1 = desktop->shtctl->vram;
  vram_t *vram  = (vram_t *)malloc(desktop->xsize * desktop->ysize * sizeof(vram_t));
  memcpy((void *)vram, (void *)vram1, desktop->xsize * desktop->ysize * sizeof(vram_t));
  desktop->shtctl->vram = vram;
  memset((void *)vram1, 0, desktop->xsize * desktop->ysize * sizeof(vram_t));
  desktop->is_using = false;
  return;
}

void draw_desktop(desktop_t *desktop, int x, int y, int x1, int y1, color_t color) {
  if (color != 0x12ffffff) SDraw_Box(desktop->vram, x, y, x1, y1, color, desktop->xsize);
  sheet_refresh(desktop->sht, x, y, x1, y1);
  return;
}

void puts_desktop(desktop_t *desktop, char *s, int x, int y, color_t color) {
  Sputs(desktop->vram, s, x, y, color, desktop->xsize);
  sheet_refresh(desktop->sht, x, y, x + strlen(s) * 8, y + 16);
  return;
}

desktop_t *get_now_desktop() {
  for (int i = 1; list_search_by_count(i, desktop_list) != NULL; i++) {
    desktop_t *d = (desktop_t *)list_search_by_count(i, desktop_list)->val;
    if (d->is_using) { return d; }
  }
  return NULL;
}

desktop_t *create_desktop(int xsize, int ysize, unsigned tid) {
  if (desktop_list == NULL) { desktop_list = list_new(); }
  desktop_t *res   = (desktop_t *)malloc(sizeof(desktop_t));
  res->shtctl      = shtctl_init((vram_t *)malloc(xsize * ysize * sizeof(vram_t)), xsize, ysize);
  res->sht         = sheet_alloc(res->shtctl);
  res->vram        = (vram_t *)malloc(xsize * ysize * sizeof(vram_t));
  res->xsize       = xsize;
  res->ysize       = ysize;
  res->window_list = list_new();
  res->display     = display_desktop;
  res->hide        = hide_desktop;
  res->draw        = draw_desktop;
  res->tid         = tid;
  res->puts        = puts_desktop;
  res->sht->wnd    = NULL;
  sheet_setbuf(res->sht, res->vram, xsize, ysize, -1);
  sheet_slide(res->sht, 0, 0);
  sheet_updown(res->sht, 0);
  sheet_refresh(res->sht, 0, 0, res->xsize, res->ysize);
  list_add_val((uintptr_t)res, desktop_list);
  return res;
}
