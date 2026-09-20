#include "gui.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

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

void gui_mouse_release(window_t *window) {
  gmouse_t *mouse = window->desktop->mouse;
  if (!mouse)
    return;
  if (mouse->target == window) {
    mouse->target = NULL;
    mouse->gesture = GUI_GESTURE_NONE;
  }
  if (mouse->captured == window) {
    mouse->captured = NULL;
    mouse->mode = GUI_MOUSE_NORMAL;
    sheet_updown(mouse->sht, mouse->sht->ctl->top + 1);
  }
  if (window->pointer_buttons && window->shared) {
    gui_event_t event = {.type = GUI_EVENT_POINTER,
                         .x = mouse->x - window->x,
                         .y = mouse->y - window->y};
    gui_pointer_push(&window->shared->events, &event);
    gui_wake_window(window);
  }
  window->pointer_buttons = 0;
}

void gui_mouse_sync(gmouse_t *mouse) {
  if (!mouse)
    return;
  window_t *focus = mouse->desktop->focused_window;
  if (mouse->target && (!mouse->target->using1 || mouse->target != focus))
    gui_mouse_release(mouse->target);
  if (mouse->captured && (!mouse->captured->using1 || mouse->captured != focus))
    gui_mouse_release(mouse->captured);
  bool hidden = mouse->captured &&
                (mouse->mode & (GUI_MOUSE_RELATIVE | GUI_MOUSE_HIDDEN));
  if (hidden != (mouse->sht->height < 0))
    sheet_updown(mouse->sht, hidden ? -1 : mouse->sht->ctl->top + 1);
}

static void gui_mouse_dispatch(gmouse_t *mouse, const mouse_event_t *input) {
  gui_mouse_sync(mouse);
  bool relative = mouse->captured && (mouse->mode & GUI_MOUSE_RELATIVE);
  int old_x = mouse->x, old_y = mouse->y;
  if (!relative) {
    int min_x = -16, min_y = -19;
    int max_x = mouse->desktop->xsize, max_y = mouse->desktop->ysize;
    if (mouse->captured && (mouse->mode & GUI_MOUSE_CONFINED)) {
      window_t *window = mouse->captured;
      min_x = window->x + 4;
      min_y = window->y + 24;
      max_x = window->x + window->xsize - 5;
      max_y = window->y + window->ysize - 5;
    }
    int64_t x = (int64_t)mouse->x + input->x;
    int64_t y = (int64_t)mouse->y + input->y;
    mouse->x = x < min_x ? min_x : x > max_x ? max_x : (int)x;
    mouse->y = y < min_y ? min_y : y > max_y ? max_y : (int)y;
  }
  unsigned previous_buttons = mouse->buttons;
  mouse->buttons = input->buttons;
  bool explicit_capture =
      mouse->captured &&
      (mouse->mode &
       (GUI_MOUSE_CAPTURE | GUI_MOUSE_RELATIVE | GUI_MOUSE_CONFINED));
  window_t *window = explicit_capture ? mouse->captured
                     : mouse->target  ? mouse->target
                                      : window_at(mouse);
  if (window && !previous_buttons && input->buttons && !explicit_capture) {
    mouse->click_button_last = NULL;
    mouse->click_textbox_last = NULL;
    if (input->buttons & 1) {
      window->handle_left(window, mouse);
      window = mouse->target;
    } else {
      window_focus(window);
      mouse->target = window;
      mouse->gesture = GUI_GESTURE_CLIENT;
    }
  }
  if (mouse->target && mouse->gesture == GUI_GESTURE_MOVE) {
    window = mouse->target;
    int64_t x = (int64_t)window->x + (previous_buttons ? mouse->x - old_x : 0);
    int64_t y = (int64_t)window->y + (previous_buttons ? mouse->y - old_y : 0);
    window->x = x < INT16_MIN ? INT16_MIN : x > INT16_MAX ? INT16_MAX : x;
    window->y = y < INT16_MIN ? INT16_MIN : y > INT16_MAX ? INT16_MAX : y;
    sheet_slide(window->sht, window->x, window->y);
  } else if (mouse->target && mouse->gesture == GUI_GESTURE_RESIZE) {
    window = mouse->target;
    int width = mouse->initial_width;
    int height = mouse->initial_height;
    if (mouse->resize_axes & 1)
      width += mouse->x - mouse->anchor_x;
    if (mouse->resize_axes & 2)
      height += mouse->y - mouse->anchor_y;
    window->requested_width = width < 40          ? 40
                              : width > INT16_MAX ? INT16_MAX
                                                  : width;
    window->requested_height = height < 29          ? 29
                               : height > INT16_MAX ? INT16_MAX
                                                    : height;
  } else if (window && window->shared &&
             (explicit_capture || mouse->target || !input->buttons)) {
    gui_event_t event = {.type = GUI_EVENT_POINTER,
                         .x = mouse->x - window->x,
                         .y = mouse->y - window->y,
                         .dx = input->x,
                         .dy = input->y,
                         .wheel = input->wheel,
                         .buttons = input->buttons,
                         .relative = relative};
    window->pointer_buttons = input->buttons;
    gui_pointer_push(&window->shared->events, &event);
    gui_wake_window(window);
  }
  if (!input->buttons) {
    mouse->target = NULL;
    mouse->gesture = GUI_GESTURE_NONE;
  }
  sheet_slide(mouse->sht, mouse->x, mouse->y);
  gui_update_window_states(mouse->desktop);
}

void gmouse(gmouse_t *gmouse) {
  if (start_keyboard_message() != 0 || mouse_enable() != 0 ||
      use_keyboard() != 0) {
    logkf("GUI input devices are already owned\n");
    _exit((unsigned)-1);
  }
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
      mouse_event_t event;
      if (mouse_read(&event) > 0)
        gui_mouse_dispatch(gmouse, &event);
    } else if (key_press_status() != 0) {

      window_t *r = gmouse->desktop->focused_window;
      uint8_t i = get_key_press();
      if (r != NULL && r->shared != NULL && r->keyboard_events) {
        gui_event_queue_push(&r->shared->key_press, i);
        gui_wake_window(r);
      }
    } else if (key_up_status() != 0) {
      window_t *r = gmouse->desktop->focused_window;
      uint8_t i = get_key_up();
      if (r != NULL && r->shared != NULL && r->keyboard_events) {
        gui_event_queue_push(&r->shared->key_up, i);
        gui_wake_window(r);
      }
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

  desktop->mouse = res;
  return res;
}
