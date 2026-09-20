#include "gui.h"
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <vm.h>

void putfonts_asc(vram_t *vram, int xsize, int x, int y, color_t c,
                  unsigned char *s);

static void desktop_focus_top_window(desktop_t *desktop) {
  desktop->focused_window = NULL;
  for (int height = desktop->shtctl->top; height >= 0; height--) {
    window_t *window = desktop->shtctl->sheets[height]->wnd;
    if (window != NULL && window->using1) {
      desktop->focused_window = window;
      return;
    }
  }
}

void window_show(window_t *window, bool focused) {
  window->using1 = true;
  int top_window = window->desktop->sht->height;
  for (int height = top_window + 1; height <= window->sht->ctl->top; height++) {
    if (window->sht->ctl->sheets[height]->wnd != NULL) {
      top_window = height;
    }
  }
  sheet_updown(window->sht,
               window->sht->height < 0 ? top_window + 1 : top_window);
  if (focused)
    window->desktop->focused_window = window;
  gui_update_window_states(window->desktop);
}

void window_focus(window_t *window) {
  if (window == NULL || !window->using1 || window->sht == NULL)
    return;
  window_show(window, true);
}

void display_window(window_t *window, int x, int y) {
  window->x = x;
  window->y = y;
  sheet_slide(window->sht, x, y);
  window_show(window, true);
}

void hide_window(window_t *window) {
  if (window == NULL || !window->using1) {
    return;
  }
  bool focused = window->desktop->focused_window == window;
  gui_mouse_release(window);
  window->using1 = false;
  sheet_updown(window->sht, -1);
  if (focused) {
    desktop_focus_top_window(window->desktop);
  }
  gui_update_window_states(window->desktop);
}

void close_window(window_t *window) {
  if (window == NULL) {
    return;
  }
  if (window->super_window != NULL) {
    window->super_window->close(window->super_window);
  }
  destroy_window(window);
}

void draw_window(window_t *window, int x, int y, int x1, int y1,
                 color_t color) {
  SDraw_Box(window->vram, x, y, x1, y1, color, window->xsize);
  sheet_refresh(window->sht, x, y, x1, y1);
}

void puts_window(window_t *window, char *s, int x, int y, color_t color) {
  Sputs(window->vram, s, x, y, color, window->xsize);
  sheet_refresh(window->sht, x, y, x + strlen(s) * 8, y + 16);
}
void handle_left_window(window_t *window, gmouse_t *mouse) {
  if (!window->using1)
    return;
  window_focus(window);
  int x = mouse->x - window->x, y = mouse->y - window->y;
  mouse->target = window;
  mouse->gesture = GUI_GESTURE_CLIENT;
  mouse->anchor_x = mouse->x;
  mouse->anchor_y = mouse->y;
  mouse->initial_width = window->xsize;
  mouse->initial_height = window->ysize;
  mouse->resize_axes =
      (x >= window->xsize - 4 ? 1u : 0u) | (y >= window->ysize - 4 ? 2u : 0u);
  if (window->resizable && mouse->resize_axes) {
    mouse->gesture = GUI_GESTURE_RESIZE;
    return;
  }
  if (Collision(window->x + 3, window->y + 3, window->xsize - 40, 20, mouse->x,
                mouse->y)) {
    mouse->gesture = GUI_GESTURE_MOVE;
    return;
  }
  if (Collision(window->x + window->xsize - 21, window->y + 5, 16, 14, mouse->x,
                mouse->y)) {
    mouse->target = NULL;
    mouse->gesture = GUI_GESTURE_NONE;
    window->close(window);
    return;
  }
  if (Collision(window->x + window->xsize - 37, window->y + 5, 16, 14, mouse->x,
                mouse->y)) {
    window->hide(window);
    return;
  }
  if (window->super_window && window->super_window->handle_left)
    window->super_window->handle_left(window, mouse);
}

static void window_draw_title(window_t *window) {
  int xsize = window->xsize;
  if (xsize < 40 || window->ysize < 20)
    return;
  static const char *const closebtn[14] = {
      "OOOOOOOOOOOOOOO@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQ@@QQQQ@@QQ$@", "OQQQQ@@QQ@@QQQ$@", "OQQQQQ@@@@QQQQ$@",
      "OQQQQQQ@@QQQQQ$@", "OQQQQQ@@@@QQQQ$@", "OQQQQ@@QQ@@QQQ$@",
      "OQQQ@@QQQQ@@QQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "O$$$$$$$$$$$$$$@", "@@@@@@@@@@@@@@@@"};
  static const char *const smallbtn[14] = {
      "OOOOOOOOOOOOOOO@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@", "OQQQQQQQQQQQQQ$@",
      "OQQ@@@@@@@@@QQ$@", "OQQ@@@@@@@@@QQ$@", "OQQQQQQQQQQQQQ$@",
      "O$$$$$$$$$$$$$$@", "@@@@@@@@@@@@@@@@",
  };
  uint32_t times = (xsize - 8) / (255 - 106) + 1;
  for (int i = 3; i < 20; i++) {
    color_t color = argb(0, 10, 36, 106);
    for (int j = 3, count = 0; j < xsize - 4; j++, count++) {
      window->vram[j + i * xsize] = color;
      if (count == times && (color & 0xff) != 255) {
        color += 0x00010101;
        count = 0;
      }
    }
  }

  size_t visible = xsize > 62 ? (xsize - 62) / 8 : 0;
  size_t length = strlen(window->title);
  if (visible > length)
    visible = length;
  char saved = window->title[visible];
  window->title[visible] = 0;
  putfonts_asc(window->vram, xsize, 24, 4, COL_FFFFFF,
               (unsigned char *)window->title);
  window->title[visible] = saved;
  color_t c;
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
      window->vram[(5 + y) * xsize + (xsize - 21 + x)] = c;
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
      window->vram[(5 + y) * xsize + (xsize - 37 + x)] = c;
    }
  }
}

int window_set_title(window_t *window, const char *title) {
  char *copy = strdup(title);
  if (!copy)
    return -1;
  free(window->title);
  window->title = copy;
  window_draw_title(window);
  sheet_refresh(window->sht, 3, 3, window->xsize - 4, 20);
  return 0;
}

window_t *create_window(desktop_t *desktop, const char *title, int xsize,
                        int ysize, unsigned tid, gui_window_shared_t *shared) {
  if (desktop == NULL || title == NULL || xsize < 40 || ysize < 28 ||
      (size_t)xsize >
          (SIZE_MAX - (VM_PAGE_SIZE - 1)) / sizeof(vram_t) / (size_t)ysize) {
    return NULL;
  }
  window_t *res = (window_t *)malloc(sizeof(window_t));
  if (res == NULL) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  res->desktop = desktop;
  size_t bytes = ((size_t)xsize * ysize * sizeof(vram_t) + VM_PAGE_SIZE - 1) &
                 ~(size_t)(VM_PAGE_SIZE - 1);
  res->shared = shared;
  res->vram = shared ? gui_window_pixels(shared, xsize, ysize, 1)
                     : vm_map(NULL, bytes);
  if (res->vram == NULL) {
    free(res);
    return NULL;
  }
  res->xsize = xsize;
  res->ysize = ysize;
  res->title = malloc(strlen(title) + 1);
  if (res->title == NULL) {
    if (!shared)
      vm_unmap(res->vram, bytes);
    free(res);
    return NULL;
  }
  strcpy(res->title, title);
  res->sht = sheet_alloc(desktop->shtctl);
  if (res->sht == NULL) {
    free(res->title);
    if (!shared)
      vm_unmap(res->vram, bytes);
    free(res);
    return NULL;
  }
  res->tid = tid;
  res->display = display_window;
  res->hide = hide_window;
  res->draw = draw_window;
  res->puts = puts_window;
  res->handle_left = handle_left_window;
  res->close = close_window;
  res->super_window = NULL;
  res->keyboard_events = false;
  res->sht->wnd = res;
  if (list_add_val((uintptr_t)res, desktop->window_list) == NULL) {
    res->sht->wnd = NULL;
    sheet_free(res->sht);
    free(res->title);
    if (!shared)
      vm_unmap(res->vram, bytes);
    free(res);
    return NULL;
  }

  sheet_setbuf(res->sht, res->vram, xsize, ysize, -1);

  window_draw_frame(res);
  res->requested_width = xsize;
  res->requested_height = ysize;
  return res;
}

void window_draw_frame(window_t *window) {
  int xsize = window->xsize, ysize = window->ysize;
  boxfill(window->vram, xsize, COL_C6C6C6, 0, 0, xsize - 1, 0);
  boxfill(window->vram, xsize, COL_FFFFFF, 1, 1, xsize - 2, 1);
  boxfill(window->vram, xsize, COL_C6C6C6, 0, 0, 0, ysize - 1);
  boxfill(window->vram, xsize, COL_FFFFFF, 1, 1, 1, ysize - 2);
  boxfill(window->vram, xsize, COL_848484, xsize - 2, 1, xsize - 2, ysize - 2);
  boxfill(window->vram, xsize, COL_000000, xsize - 1, 0, xsize - 1, ysize - 1);
  boxfill(window->vram, xsize, COL_C6C6C6, 2, 2, xsize - 3, ysize - 3);

  boxfill(window->vram, xsize, COL_848484, 1, ysize - 2, xsize - 2, ysize - 2);
  boxfill(window->vram, xsize, COL_000000, 0, ysize - 1, xsize - 1, ysize - 1);

  window_draw_title(window);
}

void destroy_window(window_t *window) {
  if (window == NULL) {
    return;
  }
  bool focused = window->desktop->focused_window == window;
  gui_mouse_release(window);
  for (List *entry = window->desktop->window_list->next; entry != NULL;
       entry = entry->next) {
    if (entry->val == (uintptr_t)window) {
      list_delete_child(entry, window->desktop->window_list);
      break;
    }
  }
  if (window->sht != NULL) {
    window->sht->wnd = NULL;
    sheet_free(window->sht);
  }
  if (focused) {
    desktop_focus_top_window(window->desktop);
  }
  gui_update_window_states(window->desktop);
  size_t bytes = ((size_t)window->xsize * window->ysize * sizeof(vram_t) +
                  VM_PAGE_SIZE - 1) &
                 ~(size_t)(VM_PAGE_SIZE - 1);
  if (!window->shared)
    vm_unmap(window->vram, bytes);
  free(window->title);
  free(window);
}
