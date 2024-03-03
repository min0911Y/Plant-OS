#include "gui.h"
#include <syscall.h>

void button_draw(button_t *button) {
  int w = button->sht->bxsize;
  int h = button->sht->bysize;
  int dy = 0; // 按钮的文字垂直偏移量
  if (h > 20) {
    dy = (h - 20) / 2;
  }
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 0 + w, 0 + h, COL_C6C6C6);
  button->super_window->puts_sheet(button->super_window, button->sht, button->text, 0 + (w - strlen(button->text) * 8) / 2,
                                   0 + dy, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0 + w - 2, 0, 0 + w - 1, 0 + h, COL_848484);
  button->super_window->draw_sheet(button->super_window, button->sht, 0 + w - 1, 0, 0 + w, 0 + h, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 0 + w, 1, COL_FFFFFF);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 1, 0 + h, COL_FFFFFF);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0 + h - 1, 0 + w, 0 + h, COL_000000);
}
void button_draw_clicking(button_t *button) {
  int w = button->sht->bxsize;
  int h = button->sht->bysize;
  int dy = 0; // 按钮的文字垂直偏移量
  if (h > 20) {
    dy = (h - 20) / 2;
  }

  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 0 + w, 0 + h, COL_C6C6C6);
  button->super_window->puts_sheet(button->super_window, button->sht, button->text, 0 + (w - strlen(button->text) * 8) / 2,
                                   0 + dy, COL_000000);
  // 按钮被按下是凹陷状
  button->super_window->draw_sheet(button->super_window, button->sht, 0 + w - 2, 0, 0 + w - 1, 0 + h, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0 + w - 1, 0, 0 + w, 0 + h, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 0 + w, 1, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0, 1, 0 + h, COL_000000);
  button->super_window->draw_sheet(button->super_window, button->sht, 0, 0 + h - 1, 0 + w, 0 + h, COL_FFFFFF);
  button->super_window->draw_sheet(button->super_window, button->sht, 0 + w - 1, 0 + h, 0 + w, 0, COL_FFFFFF);
}
void handle_left_button(button_t *button) {
  button_draw_clicking(button);
  if (button->click != NULL) {
	button->click(button);
  }
  sleep(100);
  button_draw(button);
}
button_t *create_button(super_window_t *super_window, char *text, int xsize, int ysize, int x, int y, void (*click)(button_t *button)) {
  button_t *res = malloc(sizeof(button_t));
  res->text = malloc(strlen(text) + 1);
  strcpy(res->text, text);
  res->super_window = super_window;
  res->handle_left = handle_left_button;
  res->click = click;
  res->close = NULL;
  res->sht = super_window->create_sheet(super_window, xsize, ysize, x, y, 1);
  res->vram = res->sht->buf;
  list_add_val((uintptr_t)res, super_window->button_list);
  button_draw(res);
  return res;
}
