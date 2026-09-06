#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define MOUSE_ROLL_NONE 0
#define MOUSE_ROLL_UP 1
#define MOUSE_ROLL_DOWN 2

mouse_event_t mouse_event;
void (*drop)();
static window_t *window_at(gmouse_t *gmouse) {
  for (int height = gmouse->sht->ctl->top; height > 0; height--) {
    struct SHEET *sheet = gmouse->sht->ctl->sheets[height];
    if (sheet->wnd != NULL &&
        Collision(sheet->vx0, sheet->vy0, sheet->bxsize, sheet->bysize,
                  gmouse->x, gmouse->y)) {
      return sheet->wnd;
    }
  }
  return NULL;
}

void gmouse(gmouse_t *gmouse) {
  if (start_keyboard_message() != 0 || mouse_enable() != 0 ||
      use_keyboard() != 0) {
    logkf("GUI input devices are already owned\n");
    _exit((unsigned)-1);
  }
  drop = NULL;
  logkf("GMOUSE ID = %d\n", NowTaskID());
  unsigned new = 0;
  unsigned old = 0;
  for (;;) {
    if (input_wait(INPUT_WAIT_ALL) < 0) {
      logkf("GUI input wait failed\n");
      _exit((unsigned)-1);
    }
    TaskLock();
    if (mouse_dat_status() != 0) {
      if (mouse_read(&mouse_event) > 0) {
        int64_t target_x = (int64_t)gmouse->x + mouse_event.x;
        int64_t target_y = (int64_t)gmouse->y + mouse_event.y;
        if (target_x < -16)
          target_x = -16;
        if (target_x > gmouse->desktop->xsize)
          target_x = gmouse->desktop->xsize;
        if (target_y < -19)
          target_y = -19;
        if (target_y > gmouse->desktop->ysize)
          target_y = gmouse->desktop->ysize;
        mouse_event.x = (int)target_x - gmouse->x;
        mouse_event.y = (int)target_y - gmouse->y;
        unsigned roll = mouse_event.wheel > 0   ? MOUSE_ROLL_UP
                        : mouse_event.wheel < 0 ? MOUSE_ROLL_DOWN
                                                : MOUSE_ROLL_NONE;
        // logkf("%d %d\n",mouse_event.x,mouse_event.y);

        if ((mouse_event.buttons & 0x01) != 0) {
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          gmouse->click_button_last = NULL;
          gmouse->click_textbox_last = NULL;
          if (!drop) {
            window_t *window = window_at(gmouse);
            gmouse->click_left = window;
            if (window != NULL && window->handle_left != NULL) {
              window->handle_left(window, gmouse);
            }
            gmouse->click_left = NULL;
          } else {
            drop();
          }
        } else if ((mouse_event.buttons & 0x02) != 0) {
          drop = NULL;
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          window_t *window = window_at(gmouse);
          gmouse->click_right = window;
          if (window != NULL) {
            window_focus(window);
            if (window->handle_right != NULL) {
              window->handle_right(window, gmouse);
            }
          }
        } else if (roll != MOUSE_ROLL_NONE) {
          drop = NULL;
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          window_t *window = window_at(gmouse);
          gmouse->wheel = window;
          if (window != NULL && window->handle_mouse_wheel != NULL) {
            window->handle_mouse_wheel(window, gmouse, roll);
          }
        } else {
          drop = NULL;
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          window_t *window = window_at(gmouse);
          gmouse->stay = window;
          if (window != NULL && window->handle_stay != NULL) {
            window->handle_stay(window, gmouse);
          }
        }
        gmouse->x += mouse_event.x;
        gmouse->y += mouse_event.y;
        if (gmouse->x > gmouse->desktop->xsize) {
          gmouse->x = gmouse->desktop->xsize;
        } else if (gmouse->x < -16) {
          gmouse->x = -16;
        }
        if (gmouse->y > gmouse->desktop->ysize) {
          gmouse->y = gmouse->desktop->ysize;
        } else if (gmouse->y < -19) {
          gmouse->y = -19;
        }
        sheet_slide(gmouse->sht, gmouse->x, gmouse->y);
      }
    } else if (key_press_status() != 0) {

      window_t *r = gmouse->desktop->focused_window;
      uint8_t i = get_key_press();
      if (r != NULL && r->shared != NULL && r->keyboard_events)
        gui_event_queue_push(&r->shared->key_press, i);
    } else if (key_up_status() != 0) {
      window_t *r = gmouse->desktop->focused_window;
      uint8_t i = get_key_up();
      if (r != NULL && r->shared != NULL && r->keyboard_events)
        gui_event_queue_push(&r->shared->key_up, i);
    }
    window_t *focused = gmouse->desktop->focused_window;
    new = focused != NULL ? focused->tid : 0;
    if (old && old != new) {
      task_set_level_normal(old);
      old = 0;
    }
    if (new) {
      //     logk("SET\n");
      old = new;
      task_set_level_higher(old);
    }
    TaskUnlock();
  }
}
void draw_mouse_cursor(vram_t *mouse, int bc) {
  static char *mouse_cur_graphic[19] = {
      "*...............", "**..............", "*O*.............",
      "*OO*............", "*OOO*...........", "*OOOO*..........",
      "*OOOOO*.........", "*OOOOOO*........", "*OOOOOOO*.......",
      "*OOOOOOOO*......", "*OOOOO*****.....", "*OO*OO*.........",
      "*O*.*OO*........", "**..*OO*........", "*....*OO*.......",
      ".....*OO*.......", "......*OO*......", "......*OO*......",
      ".......**......."};
  for (int y = 0; y < 19; y++) {
    for (int x = 0; x < 16; x++) {
      if (mouse_cur_graphic[y][x] == '*') {
        mouse[y * 16 + x] = COL_000000;
      } else if (mouse_cur_graphic[y][x] == 'O') {
        mouse[y * 16 + x] = COL_FFFFFF;
      } else if (mouse_cur_graphic[y][x] == '.') {
        mouse[y * 16 + x] = bc;
      }
    }
  }
}
gmouse_t *create_gmouse(desktop_t *desktop, int x, int y, int pos) {
  if (desktop == NULL) {
    return NULL;
  }
  gmouse_t *res = malloc(sizeof(gmouse_t));
  if (res == NULL) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  res->sht = sheet_alloc(desktop->shtctl);
  res->desktop = desktop;
  res->x = x;
  res->y = y;
  vram_t *mouse_vram = malloc(16 * 19 * sizeof(vram_t));
  void *mouse_stack = malloc(32 * 1024);
  if (res->sht == NULL || mouse_vram == NULL || mouse_stack == NULL) {
    if (res->sht != NULL) {
      sheet_free(res->sht);
    }
    free(mouse_vram);
    free(mouse_stack);
    free(res);
    return NULL;
  }
  draw_mouse_cursor(mouse_vram, COL_TRANSPARENT);
  sheet_setbuf(res->sht, mouse_vram, 16, 19, COL_TRANSPARENT);
  sheet_slide(res->sht, x, y);
  sheet_updown(res->sht, pos);
  sheet_refresh(res->sht, 0, 0, 16, 19);

  uintptr_t stack_top = (uintptr_t)mouse_stack + 32 * 1024;
  res->tid = AddThread("gmouse", (uintptr_t)gmouse, stack_top, (uintptr_t)res);
  if ((int)res->tid < 0) {
    sheet_free(res->sht);
    free(mouse_vram);
    free(mouse_stack);
    free(res);
    return NULL;
  }

  return res;
}
