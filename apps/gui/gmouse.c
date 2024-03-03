#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define MOUSE_ROLL_NONE 0
#define MOUSE_ROLL_UP 1
#define MOUSE_ROLL_DOWN 2

int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat) {
  if (mdec->phase == 1) {
    if (dat == 0xfa) { // ACK
      return 0;
    }
    mdec->buf[0] = dat;
    mdec->phase = 2;
    return 0;
  } else if (mdec->phase == 2) {
    mdec->buf[1] = dat;
    mdec->phase = 3;
    return 0;
  } else if (mdec->phase == 3) {
    mdec->buf[2] = dat;
    mdec->phase = 4;
    return 0;
  } else if (mdec->phase == 4) {
    // printk("已经收集了四个字节\n");
    mdec->buf[3] = dat;
    mdec->phase = 1;
    mdec->btn = mdec->buf[0] & 0x07;
    mdec->x = mdec->buf[1]; // x
    mdec->y = mdec->buf[2]; // y
    if ((mdec->buf[0] & 0x10) != 0) {
      mdec->x |= 0xffffff00;
    }
    if ((mdec->buf[0] & 0x20) != 0) {
      mdec->y |= 0xffffff00;
    }
    mdec->y = -mdec->y; //
    if (mdec->buf[3] == 0xff) {
      mdec->roll = MOUSE_ROLL_UP;
    } else if (mdec->buf[3] == 0x01) {
      mdec->roll = MOUSE_ROLL_DOWN;
    } else {
      mdec->roll = MOUSE_ROLL_NONE;
    }
    return 1;
  }
  return -1;
}
struct MOUSE_DEC mdec;
void gmouse(gmouse_t *gmouse) {
  mdec.phase = 1;
  mouse_enable();
  for (;;) {
    // logkf("%d\n",mouse_dat_status());
    if (mouse_dat_status() == 0) {
      continue;
    } else if (mouse_dat_status() != 0) {
      int i = mouse_dat_get();
      //  logkf("%02x\n",i);
      if (mouse_decode(&mdec, i) != 0) {
        // logkf("%d %d\n",mdec.x,mdec.y);

        if ((mdec.btn & 0x01) != 0) {
          // 左键
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->click_button_last = NULL;
          gmouse->click_textbox_last = NULL;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            for (int i = 1;
                 list_search_by_count(i, gmouse->desktop->window_list) != NULL;
                 i++) {
              window_t *w = (window_t *)list_search_by_count(
                                i, gmouse->desktop->window_list)
                                ->val;
              if (Collision(w->x, w->y, w->xsize, w->ysize, gmouse->x,
                            gmouse->y) &&
                  w->sht->height == top) {
                    //logk("call\n");
                gmouse->click_left = w;
                if (w->handle_left != NULL) {
                  w->handle_left(w, gmouse);
                }
                top = 1;
                break;
              }
            }
          }
        } else if ((mdec.btn & 0x02) != 0) {
          // 右键
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            for (int i = 1;
                 list_search_by_count(i, gmouse->desktop->window_list) != NULL;
                 i++) {
              window_t *w = (window_t *)list_search_by_count(
                                i, gmouse->desktop->window_list)
                                ->val;
              if (Collision(w->x, w->y, w->xsize, w->ysize, gmouse->x,
                            gmouse->y) &&
                  w->sht->height == top) {
                gmouse->click_right = w;
                if (w->handle_right != NULL) {
                  w->handle_right(w, gmouse);
                }
                top = 1;
                break;
              }
            }
          }
        } else if ((mdec.btn & 0x04) != 0) {
          // 滚动
        } else {
          // 停留
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            for (int i = 1;
                 list_search_by_count(i, gmouse->desktop->window_list) != NULL;
                 i++) {
              window_t *w = (window_t *)list_search_by_count(
                                i, gmouse->desktop->window_list)
                                ->val;
              if (Collision(w->x, w->y, w->xsize, w->ysize, gmouse->x,
                            gmouse->y) &&
                  w->sht->height == top) {
                gmouse->stay = w;
                if (w->handle_stay != NULL) {
                  w->handle_stay(w, gmouse);
                }
                top = 1;
                break;
              }
            }
          }
        }
        gmouse->x += mdec.x;
        gmouse->y += mdec.y;
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
    }
    // else if (fifo8_status(task_get_key_fifo(current_task())) != 0) {
    //   if (gmouse->click_textbox_last != NULL) {
    //     gmouse->click_textbox_last->add_char(gmouse->click_textbox_last,
    //                                          getch());
    //   } else {
    //     fifo8_get(task_get_key_fifo(current_task())); // 排空
    //   }
    // }
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
  gmouse_t *res = malloc(sizeof(gmouse_t));
  res->sht = sheet_alloc(desktop->shtctl);
  res->desktop = desktop;
  res->x = x;
  res->y = y;
  vram_t *mouse_vram = malloc(16 * 19 * sizeof(vram_t));
  draw_mouse_cursor(mouse_vram, COL_TRANSPARENT);
  sheet_setbuf(res->sht, mouse_vram, 16, 19, COL_TRANSPARENT);
  sheet_slide(res->sht, x, y);
  sheet_updown(res->sht, pos);
  sheet_refresh(res->sht, 0, 0, 16, 19);

  void *mouse_stack = malloc(32 * 1024) + 32 * 1024;
  ((unsigned int *)mouse_stack)[-1] = res;

  res->tid = AddThread("", gmouse, (unsigned int)mouse_stack - 8);

  return res;
}
