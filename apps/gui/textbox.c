#include "gui.h"
#include "syscall.h"

void add_char_textbox(textbox_t *textbox, char c) {
  if (c == '\n' || c == '\t' ) return;
  if (c == '\b') {
	if (strlen(textbox->text) >= 1) {
      textbox->super_window->draw_sheet(textbox->super_window, textbox->sht, 3 + strlen(textbox->text) * 8, 3, 
                                        11 + strlen(textbox->text) * 8, 19, COL_FFFFFF);
	  textbox->text[strlen(textbox->text) - 1] = 0;
	} else {
	  return;
	}
  } else {
    if (strlen(textbox->text) * 8 + 8 > textbox->sht->bxsize - 3 - 2) return;
    textbox->super_window->draw_sheet(textbox->super_window, textbox->sht, 3, 3, 11 + strlen(textbox->text) * 8, 19, COL_FFFFFF);
    textbox->text[strlen(textbox->text)] = c;
    textbox->super_window->puts_sheet(textbox->super_window, textbox->sht, textbox->text, 3, 3, COL_000000);
  }
  if (strlen(textbox->text) * 8 + 8 <= textbox->sht->bxsize - 3 - 2) {
    textbox->super_window->draw_sheet(textbox->super_window, textbox->sht, 3 + strlen(textbox->text) * 8, 3, 
                                      11 + strlen(textbox->text) * 8, 19, COL_000000);
  }
}

textbox_t *create_textbox(super_window_t *super_window, int xsize, int ysize, int x, int y) {
  textbox_t *res = malloc(sizeof(textbox_t));
  res->super_window = super_window;
  res->add_char = add_char_textbox;
  res->close = NULL;
  res->sht = super_window->create_sheet(super_window, xsize + 3 + 2, ysize + 3 + 2, x, y, 1);
  res->vram = res->sht->buf;
  list_add_val((uintptr_t)res, super_window->textbox_list);
  
  int x0 = 3, y0 = 3, x1 = xsize + 3, y1 = ysize + 3;
  super_window->draw_sheet(super_window, res->sht, x0 - 2, y0 - 3, x1 + 1, y0 - 2, COL_C6C6C6);
  super_window->draw_sheet(super_window, res->sht, x0 - 3, y0 - 3, x0 - 2, y1 + 1, COL_848484);
  super_window->draw_sheet(super_window, res->sht, x0 - 3, y1 + 1, x1 + 1, y1 + 2, COL_FFFFFF);
  super_window->draw_sheet(super_window, res->sht, x1 + 1, y0 - 3, x1 + 2, y1 + 2, COL_FFFFFF);
  super_window->draw_sheet(super_window, res->sht, x0 - 1, y0 - 2, x1 + 0, y0 - 1, COL_000000);
  super_window->draw_sheet(super_window, res->sht, x0 - 2, y0 - 2, x0 - 1, y1 + 0, COL_000000);
  super_window->draw_sheet(super_window, res->sht, x0 - 2, y1 + 0, x1 + 0, y1 + 1, COL_C6C6C6);
  super_window->draw_sheet(super_window, res->sht, x1 + 0, y0 - 2, x1 + 1, y1 + 1, COL_C6C6C6);
  super_window->draw_sheet(super_window, res->sht, x0 - 1, y0 - 1, x1 + 0, y1 + 0, COL_FFFFFF);
  super_window->draw_sheet(super_window, res->sht, x0, y0, x0 + 8, y0 + 16, COL_000000);
  
  return res;
}