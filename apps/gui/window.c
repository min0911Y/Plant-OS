#include "gui.h"

void display_window(window_t *window, int x, int y, int pos) {
  window->x = x;
  window->y = y;
  window->using1 = true;
  sheet_slide(window->sht, x, y);
  if (window->sht->height != pos) {
    sheet_updown(window->sht, pos);
  }
  // sheet_refresh(window->sht, 0, 0, window->xsize, window->ysize);
}

void hide_window(window_t *window) {
  window->using1 = false;
  sheet_updown(window->sht, -1);
}

void close_window(window_t *window) {
  if (window->console != NULL) {
    window->console->close(window->console);
  } else if (window->super_window != NULL) {
    window->super_window->close(window->super_window);
  }
  // if (window->task != get_task(3)) {
  //   task_delete(window->task);
  // }
  int count = 1;
  for (; list_search_by_count(count, window->desktop->window_list)->val !=
         (uintptr_t)window;
       count++)
    ;
  list_delete_by_count(count, window->desktop->window_list);
  sheet_free(window->sht);
  free((void *)window->vram);
  free((void *)window->title);
  free((void *)window);
}

void draw_window(window_t *window, int x, int y, int x1, int y1,
                 color_t color) {
                  logkf("vram is %p\n",window->vram);
  SDraw_Box(window->vram, x, y, x1, y1, color, window->xsize);
  sheet_refresh(window->sht, x, y, x1, y1);
}

void puts_window(window_t *window, char *s, int x, int y, color_t color) {
  Sputs(window->vram, s, x, y, color, window->xsize);
  sheet_refresh(window->sht, x, y, x + strlen(s) * 8, y + 16);
}
extern void (*drop)();
window_t *backup_w;
gmouse_t *backup_gmouse;
void w_drop() {
  if (Collision(backup_w->x + 3, backup_w->y + 3, backup_w->xsize - 37, 20,
                backup_gmouse->x, backup_gmouse->y)) {
    // 移动
    int oldx;
    oldx = backup_w->x;
    backup_w->x += mdec.x;
    backup_w->y += mdec.y;
    backup_w->display(backup_w, (backup_w->x + 2) & ~3, backup_w->y,
                      backup_w->sht->ctl->top - 1);
    int x = (backup_w->x + 2) & ~3;
    x -= oldx;
    mdec.x = x;
  } else {
    drop = NULL;
  }
}
void handle_left_window(window_t *window, gmouse_t *gmouse) {
  if (!window->using1)
    return;
  if (Collision(window->x + 3, window->y + 3, window->xsize - 37, 20, gmouse->x,
                gmouse->y)) {
    // 移动
    window->x += mdec.x;
    window->y += mdec.y;
    backup_w = window;
    backup_gmouse = gmouse;
    drop = w_drop;
  } else if (Collision(window->x + window->xsize - 21, window->y + 5, 16, 19,
                       gmouse->x, gmouse->y)) {
    // 关闭
    window->close(window);
    // printk("You close a window.\n");
    return;
  } else if (Collision(window->x + window->xsize - 37, window->y + 5, 16, 19,
                       gmouse->x, gmouse->y)) {
    window->hide(window);
    // printk("You hide a window.\n");
    return;
  }
  window->display(window, window->x, window->y, window->sht->ctl->top - 1);
  if (window->console != NULL) {
    if (window->console->handle_left != NULL) {
      window->console->handle_left(window->console, gmouse);
    }
  } else if (window->super_window != NULL) {
    if (window->super_window->handle_left != NULL) {
      window->super_window->handle_left(window->super_window, gmouse);
    }
  }
}

window_t *create_window(desktop_t *desktop, char *title, int xsize, int ysize,
                        unsigned tid) {
  window_t *res = (window_t *)malloc(sizeof(window_t));
  res->desktop = desktop;
  res->vram = (vram_t *)malloc(xsize * ysize * sizeof(vram_t));
  res->xsize = xsize;
  res->ysize = ysize;
  res->title = malloc(strlen(title) + 1);
  strcpy(res->title, title);
  res->sht = sheet_alloc(desktop->shtctl);
  res->tid = tid;
  res->display = display_window;
  res->hide = hide_window;
  res->draw = draw_window;
  res->puts = puts_window;
  res->handle_left = handle_left_window;
  res->handle_right = NULL;
  res->handle_stay = NULL;
  res->close = close_window;
  res->console = NULL;
  res->super_window = NULL;
  list_add_val((uintptr_t)res, desktop->window_list);

  sheet_setbuf(res->sht, res->vram, xsize, ysize, -1);

  static char *closebtn[14] = {
      "OOOOOOOOOOOOOOO@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQ@@QQQQ@@QQ$@", "OQQQQ@@QQ@@QQQ$@", "OQQQQQ@@@@QQQQ$@",
      "OQQQQQQ@@QQQQQ$@", "OQQQQQ@@@@QQQQ$@", "OQQQQ@@QQ@@QQQ$@",
      "OQQQ@@QQQQ@@QQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "O$$$$$$$$$$$$$$@", "@@@@@@@@@@@@@@@@"};
  static char *smallbtn[14] = {
      "OOOOOOOOOOOOOOO@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQ@@@@@@@@@QQ$@", "OQQ@@@@@@@@@QQ$@", "OQQQQQQQQQQQQQ$@",
      "O$$$$$$$$$$$$$$@", "@@@@@@@@@@@@@@@@",
  };
  color_t c;
  boxfill(res->vram, xsize, COL_C6C6C6, 0, 0, xsize - 1, 0);
  boxfill(res->vram, xsize, COL_FFFFFF, 1, 1, xsize - 2, 1);
  boxfill(res->vram, xsize, COL_C6C6C6, 0, 0, 0, ysize - 1);
  boxfill(res->vram, xsize, COL_FFFFFF, 1, 1, 1, ysize - 2);
  boxfill(res->vram, xsize, COL_848484, xsize - 2, 1, xsize - 2, ysize - 2);
  boxfill(res->vram, xsize, COL_000000, xsize - 1, 0, xsize - 1, ysize - 1);
  boxfill(res->vram, xsize, COL_C6C6C6, 2, 2, xsize - 3, ysize - 3);

  uint32_t times = (xsize - 8) / (255 - 106) + 1;
  for (int i = 3; i < 20; i++) {
    color_t color = argb(0, 10, 36, 106);
    for (int j = 3, count = 0; j < xsize - 4; j++, count++) {
      res->vram[j + i * xsize] = color;
      if (count == times && (color & 0xff) != 255) {
        color += 0x00010101;
        count = 0;
      }
    }
  }

  boxfill(res->vram, xsize, COL_848484, 1, ysize - 2, xsize - 2, ysize - 2);
  boxfill(res->vram, xsize, COL_000000, 0, ysize - 1, xsize - 1, ysize - 1);
  putfonts_asc(res->vram, xsize, 24, 4, COL_FFFFFF, (unsigned char *)title);

  for (int y = 0; y < 14; y++) {
    for (int x = 0; x < 16; x++) {
      if (closebtn[y][x] == '@') {
        c = COL_000000;
      } else if (closebtn[y][x] == '$') {
        c = COL_848484;
      } else if (closebtn[y][x] == 'Q') {
        c = COL_C6C6C6;
      } else {
        c = COL_FFFFFF;
      }
      res->vram[(5 + y) * xsize + (xsize - 21 + x)] = c;
    }
  }
  for (int y = 0; y < 14; y++) {
    for (int x = 0; x < 16; x++) {
      if (smallbtn[y][x] == '@') {
        c = COL_000000;
      } else if (smallbtn[y][x] == '$') {
        c = COL_848484;
      } else if (smallbtn[y][x] == 'Q') {
        c = COL_C6C6C6;
      } else {
        c = COL_FFFFFF;
      }
      res->vram[(5 + y) * xsize + (xsize - 37 + x)] = c;
    }
  }
  return res;
}
