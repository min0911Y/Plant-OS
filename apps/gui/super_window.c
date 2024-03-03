#include "gui.h"
#include <syscall.h>
void draw_window(window_t *window, int x, int y, int x1, int y1, color_t color);
void puts_window(window_t *window, char *s, int x, int y, color_t color);
void close_super_window(super_window_t *super_window) {
  sheet_free(super_window->sht_copy);
  free(super_window->vram_copy);
  for (int i = 1; (i, super_window->sht_list) != NULL; i++) {
    struct SHEET *sht =
        (struct SHEET *)list_search_by_count(i, super_window->sht_list)->val;
    free(sht->buf);
    sheet_free(sht);
  }
  list_delete(super_window->sht_list);
  list_delete(super_window->button_list);
  list_delete(super_window->textbox_list);
  super_window->window->super_window = NULL;
  super_window->window->draw = draw_window;
  super_window->window->puts = puts_window;
  free((void *)super_window);
}
struct SHEET *create_sheet_super_window(super_window_t *super_window, int xsize,
                                        int ysize, int x, int y, int pos) {
  struct SHEET *res = sheet_alloc(super_window->shtctl);
  list_add_val((uintptr_t)res, super_window->sht_list);
  vram_t *vram = malloc(xsize * ysize * sizeof(vram_t));
  SDraw_Box(vram, 0, 0, xsize, ysize, COL_000000, xsize);
  sheet_setbuf(res, vram, xsize, ysize, COL_TRANSPARENT);
  sheet_slide(res, x, y);
  sheet_updown(res, pos);
  sheet_refresh(res, 0, 0, xsize, ysize);
  sheet_refresh(super_window->window->sht, x, y, x + xsize, y + ysize);
  return res;
}
void draw_sheet_super_window(super_window_t *super_window, struct SHEET *sht,
                             int x, int y, int x1, int y1, color_t color) {
  SDraw_Box(sht->buf, x, y, x1, y1, color, sht->bxsize);
  sheet_refresh(sht, x, y, x1, y1);
  sheet_refresh(super_window->window->sht, sht->vx0 + x, sht->vy0 + y,
                sht->vx0 + x1, sht->vy0 + y1);
}
void puts_sheet_super_window(super_window_t *super_window, struct SHEET *sht,
                             char *s, int x, int y, color_t color) {
  Sputs(sht->buf, s, x, y, color, sht->bxsize);
  sheet_refresh(sht, x, y, x + strlen(s) * 8, y + 16);
  sheet_refresh(super_window->window->sht, sht->vx0 + x, sht->vy0 + y,
                sht->vx0 + x + strlen(s) * 8, sht->vy0 + y + 16);
}
void updown_sheet_super_window(super_window_t *super_window, struct SHEET *sht,
                               int pos) {
  sheet_updown(sht, pos);
  sheet_refresh(super_window->window->sht, sht->vx0, sht->vy0,
                sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize);
}
void slide_sheet_super_window(super_window_t *super_window, struct SHEET *sht,
                              int x, int y) {
  int old_x = sht->vx0, old_y = sht->vy0;
  sheet_slide(sht, x, y);
  sheet_refresh(super_window->window->sht, old_x, old_y, old_x + sht->bxsize,
                old_y + sht->bysize);
  sheet_refresh(super_window->window->sht, x, y, x + sht->bxsize,
                y + sht->bysize);
}
void draw_super_window(window_t *window, int x, int y, int x1, int y1,
                       color_t color) {
  SDraw_Box(window->super_window->vram_copy, x, y, x1, y1, color,
            window->xsize);
  sheet_refresh(window->super_window->sht_copy, x, y, x1, y1);
  sheet_refresh(window->sht, x, y, x1, y1);
}
void puts_super_window(window_t *window, char *s, int x, int y, color_t color) {
  Sputs(window->super_window->vram_copy, s, x, y, color, window->xsize);
  sheet_refresh(window->super_window->sht_copy, x, y, x + strlen(s) * 8,
                y + 16);
  sheet_refresh(window->sht, x, y, x + strlen(s) * 8, y + 16);
}
void handle_left_super_window(super_window_t *super_window, gmouse_t *gmouse) {
  for (int i = 1; list_search_by_count(i, super_window->button_list) != NULL; i++) {
    button_t *b = (button_t *)list_search_by_count(i, super_window->button_list)->val;
    if (Collision(b->sht->vx0 + super_window->window->x,
                  b->sht->vy0 + super_window->window->y, b->sht->bxsize,
                  b->sht->bysize, gmouse->x, gmouse->y)) {
      gmouse->click_button_last = b;
      b->handle_left(b);
      break;
    }
  }
  for (int i = 1; list_search_by_count(i, super_window->textbox_list) != NULL; i++) {
    textbox_t *t =
        (textbox_t *)list_search_by_count(i, super_window->textbox_list)->val;
    if (Collision(t->sht->vx0 + super_window->window->x,
                  t->sht->vy0 + super_window->window->y, t->sht->bxsize,
                  t->sht->bysize, gmouse->x, gmouse->y)) {
      gmouse->click_textbox_last = t;
      break;
    }
  }
}
super_window_t *create_super_window(window_t *window) {
  if (window->console != NULL)
    return NULL;
  super_window_t *res = malloc(sizeof(super_window_t));
  res->window = window;
  window->super_window = res;
  res->handle_left = handle_left_super_window;
  res->handle_right = NULL;
  res->handle_stay = NULL;
  res->close = close_super_window;
  res->create_sheet = create_sheet_super_window;
  res->draw_sheet = draw_sheet_super_window;
  res->puts_sheet = puts_sheet_super_window;
  res->slide_sheet = slide_sheet_super_window;
  res->updown_sheet = updown_sheet_super_window;
  res->sht_list = list_new();
  res->button_list = list_new();
  res->textbox_list = list_new();

  res->shtctl = shtctl_init(window->vram, window->xsize, window->ysize);
  res->sht_copy = sheet_alloc(res->shtctl);
  res->vram_copy = malloc(window->xsize * window->ysize * sizeof(vram_t));
  memcpy((void *)res->vram_copy, (void *)window->vram,
         window->xsize * window->ysize * sizeof(vram_t));
  sheet_setbuf(res->sht_copy, res->vram_copy, window->xsize, window->ysize, -1);
  sheet_slide(res->sht_copy, 0, 0);
  sheet_updown(res->sht_copy, 0);
  sheet_refresh(res->sht_copy, 0, 0, window->xsize, window->ysize);
  sheet_refresh(window->sht, 0, 0, window->xsize, window->ysize);

  window->draw = draw_super_window;
  window->puts = puts_super_window;
  return res;
}