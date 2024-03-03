#include <gui.h>
#include <dos.h>
void console_task() {
  struct TASK* task = current_task();
  task->line = (char*)page_malloc(1024);
  char buf[255];
  while (1) {
    vfs_getPath(buf);
    printf("%s>", buf);
    clean(task->line, 1024);
    input(task->line, 1024);
    command_run(task->line);
  }
}
bool now_tty_GraphicMode(struct tty *res) {
  console_t *console = (console_t *)res->vram;
  if (console->window->sht->height == console->window->sht->ctl->top - 1) {
	return true;
  }
  return false;
}
color_t text_color_to_real_color(unsigned char text_color, bool back_or_font) {
  unsigned char c;
  if (back_or_font) c = text_color & 0x0f;
  else c = text_color >> 4;
  if (c == 0) return COL_000000;
  else if (c == 1) return COL_000084;
  else if (c == 2) return COL_008400;
  else if (c == 3) return argb(0, 36, 36, 36);
  else if (c == 4) return COL_840000;
  else if (c == 5) return COL_FFFF00;
  else if (c == 6) return COL_848400;
  else if (c == 7) return COL_C6C6C6;
  else if (c == 8) return COL_848484;
  else if (c == 9) return COL_0000FF;
  else if (c == 10) return COL_00FF00;
  else if (c == 11) return COL_00FFFF;
  else if (c == 12) return COL_FF0000;
  else if (c == 13) return COL_FF00FF;
  else if (c == 14) return COL_FFFF00;
  else if (c == 15) return COL_FFFFFF;
}
void putchar_console(struct tty *res, int c) {
  console_t *console = (console_t *)res->vram;
  if (res->x == res->xsize) {
	if (res->y == res->ysize -1) {
	  res->screen_ne(res);
	} else {
	  res->x = 0;
	  res->y++;
	}
  }
  if (c == '\n') {
	if (res->y == res->ysize -1) {
	  res->screen_ne(res);
	  return;
	} else {
	  res->x = 0;
	  res->y++;
	  res->MoveCursor(res, res->x, res->y);
	  return;
	}
  }
  if (c == '\b') {
	if (res->x > 0) {
	  res->x--;
      console->window->draw(console->window, res->x * 8 + console->x, res->y * 16 + console->y,
                            res->x * 8 + console->x + 8, res->y * 16 + console->y + 16, 
						    text_color_to_real_color(res->color, false));
	  res->MoveCursor(res, res->x, res->y);
	  return;
	} else {
	  res->x = res->xsize - 1;
	  res->y--;
	  res->MoveCursor(res, res->x, res->y);
	  return;
	}
  }
  char s[2] = {0, 0};
  s[0] = c;
  console->window->draw(console->window, res->x * 8 + console->x, res->y * 16 + console->y,
                        res->x * 8 + console->x + 8, res->y * 16 + console->y + 16, 
						text_color_to_real_color(res->color, false));
  console->window->puts(console->window, s, res->x * 8 + console->x, res->y * 16 + console->y,
                        text_color_to_real_color(res->color, true));
  res->x++;
  res->MoveCursor(res, res->x, res->y);
}
void clear_console(struct tty *res) {
  console_t *console = (console_t *)res->vram;
  res->x = 0;
  res->y = 0;
  console->window->draw(console->window, console->x, console->y, console->x + console->xsize, console->y + console->ysize,
                        text_color_to_real_color(res->color, false));
}
void MoveCursor_console(struct tty *res, int x, int y) {
  console_t *console = (console_t *)res->vram;
  res->x = x;
  res->y = y;
  int cur_x_old = console->sht_cur->vx0;
  int cur_y_old = console->sht_cur->vy0;
  sheet_slide(console->sht_cur, console->x + x * 8, console->y + y * 16);
  sheet_refresh(console->sht_copy, cur_x_old, cur_y_old, cur_x_old + 8, cur_y_old + 16);
  sheet_refresh(console->window->sht, cur_x_old, cur_y_old, cur_x_old + 8, cur_y_old + 16);
  int cur_x_new = console->sht_cur->vx0;
  int cur_y_new = console->sht_cur->vy0;
  sheet_refresh(console->window->sht, cur_x_new, cur_y_new, cur_x_new + 8, cur_y_new + 16);
}
static void copy_char(vram_t* vram,
                      int off_x,
                      int off_y,
                      int x,
                      int y,
                      int x1,
                      int y1,
                      int xsize) {
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 8; j++) {
      vram[(y + i + off_y) * xsize + (j + x + off_x)] =
          vram[(y1 + i + off_y) * xsize + (j + x1 + off_x)];
    }
  }
}
void screen_ne_console(struct tty *res) {
  io_cli();
  console_t *console = (console_t *)res->vram;
  for (int i = 0; i < res->ysize; i++) {
    for (int j = 0; j < res->xsize; j++) {
      copy_char(console->vram_copy, console->x, console->y, j * 8, i * 16, j * 8, (i + 1) * 16,
                console->window->xsize);
    }
  }
  console->window->draw(console->window, console->x, console->y + (res->ysize - 1) * 16, console->x + res->xsize * 8,
                        console->y + res->ysize * 16, text_color_to_real_color(res->color, false));
  res->x = 0;
  res->y = res->ysize - 1;
  sheet_refresh(console->sht_copy, console->x, console->y, console->x + console->xsize, console->y + console->ysize);
  sheet_refresh(console->window->sht, console->x, console->y, console->x + console->xsize, console->y + console->ysize);
  res->MoveCursor(res, res->x, res->y);
  io_sti();
}
void Draw_Box_console(struct tty *res, int x, int y, int x1, int y1, unsigned char color) {
  io_cli();
  console_t *console = (console_t *)res->vram;
  for (int i = y * 16 + console->y; i <= y1 * 16 + console->y; i++) {
    for (int j = x * 8 + console->x; j <= x1 * 8 + console->x; j++) {
      if (console->vram_copy[i * console->window->xsize + j] == text_color_to_real_color(res->color, true)) {
        console->vram_copy[i * console->window->xsize + j] = text_color_to_real_color(color, true);
      } else {
        console->vram_copy[i * console->window->xsize + j] = text_color_to_real_color(color, false);
      }
    }
  }
  sheet_refresh(console->sht_copy, console->x + x * 8, console->y + y * 16, console->x + x1 * 8, console->y + y1 * 16);
  sheet_refresh(console->window->sht, console->x + x * 8, console->y + y * 16, console->x + x1 * 8, console->y + y1 * 16);
  io_sti();
}
void draw_console_window(window_t *window, int x, int y, int x1, int y1, color_t color) {
  SDraw_Box(window->console->vram_copy, x, y, x1, y1, color, window->xsize);
  sheet_refresh(window->console->sht_copy, x, y, x1, y1);
  sheet_refresh(window->sht, x, y, x1, y1);
}
void puts_console_window(window_t *window, char *s, int x, int y, color_t color) {
  Sputs(window->console->vram_copy, s, x, y, color, window->xsize);
  sheet_refresh(window->console->sht_copy, x, y, x + strlen(s) * 8, y + 16);
  sheet_refresh(window->sht, x, y, x + strlen(s) * 8, y + 16);
}
void draw_window(window_t *window, int x, int y, int x1, int y1, color_t color);
void puts_window(window_t *window, char *s, int x, int y, color_t color);
void close_console(console_t *console) {
  sheet_free(console->sht_cur);
  sheet_free(console->sht_copy);
  free(console->vram_cur);
  free(console->vram_copy);
  ctl_free(console->shtctl);
  tty_free(console->tty);
  io_sti();
  for (int i = 3; get_task(i) != NULL; i++) {
	struct TASK *task = get_task(i);
	if (task->thread.father == console->task) {
	  task_delete(task);
	}
  }
  task_delete(console->task);
  console->window->console = NULL;
  console->window->draw = draw_window;
  console->window->puts = puts_window;
  free((void *)console);
}
console_t *create_console(window_t *window, int xsize, int ysize, int x, int y) {
  if (window->super_window != NULL) {
	return (console_t *)NULL;
  }
  console_t *res = malloc(sizeof(console_t));
  res->window = window;
  window->console = res;
  res->xsize = xsize;
  res->ysize = ysize;
  res->x = x;
  res->y = y;
  res->handle_left = NULL;
  res->handle_right = NULL;
  res->handle_stay = NULL;
  res->close = close_console;
  window->draw(window, x, y, x + xsize, y + ysize, COL_000000);

  struct FIFO8 *keyboard_fifo = malloc(sizeof(struct FIFO8));
  struct FIFO8 *mouse_fifo = malloc(sizeof(struct FIFO8));
  uint8_t *keyboard_buf = malloc(128);
  uint8_t *mouse_buf = malloc(128);
  fifo8_init(mouse_fifo, 128, mouse_buf);
  fifo8_init(keyboard_fifo, 128, keyboard_buf);
  io_cli();
  res->task = register_task("cmdline", 1, 2 * 8, console_task, 1 * 8, 1 * 8, (unsigned int)page_malloc(64 * 1024) + 64 * 1024 - 4);
  task_set_fifo(res->task, keyboard_fifo, mouse_fifo);
  void *alloc_addr = page_malloc(512 * 1024);
  res->task->alloc_addr = alloc_addr;
  res->task->alloc_size = 512 * 1024;
  res->task->mm = memory_init((uint32_t)alloc_addr, 512 * 1024);
  
  res->shtctl = shtctl_init(window->vram, window->xsize, window->ysize);
  res->sht_cur = sheet_alloc(res->shtctl);
  res->sht_copy = sheet_alloc(res->shtctl);
  res->vram_cur = malloc(8 * 16 * sizeof(vram_t));
  res->vram_copy = malloc(window->xsize * window->ysize * sizeof(vram_t));
  SDraw_Box(res->vram_cur, 0, 0, 8, 16, COL_FFFFFF, 8);
  memcpy((void *)res->vram_copy, (void *)window->vram, window->xsize * window->ysize * sizeof(vram_t));
  sheet_setbuf(res->sht_cur, res->vram_cur, 8, 16, COL_TRANSPARENT);
  sheet_setbuf(res->sht_copy, res->vram_copy, window->xsize, window->ysize, -1);
  sheet_slide(res->sht_cur, res->x, res->y);
  sheet_slide(res->sht_copy, 0, 0);
  sheet_updown(res->sht_copy, 0);
  sheet_updown(res->sht_cur, 1);
  sheet_refresh(res->sht_copy, 0, 0, window->xsize, window->ysize);
  sheet_refresh(res->sht_cur, 0, 0, 8, 16);
  window->draw = draw_console_window;
  window->puts = puts_console_window;
  
  res->tty = tty_alloc((void *)res, xsize / 8, ysize / 16, putchar_console, MoveCursor_console,
                       clear_console, screen_ne_console, Draw_Box_console);
  tty_set_default(res->tty);
  tty_set(res->task, res->tty);
  io_sti();
  
  return res;
}
