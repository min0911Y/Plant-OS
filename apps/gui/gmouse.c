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
void (*drop)();
char keytable1[0x54] = { // 未按下Shift
    0,    0x01, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    10,   0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
void gmouse(gmouse_t *gmouse) {
  mdec.phase = 1;
  mouse_enable();
  start_keyboard_message();
  use_keyboard();
  drop = NULL;
  logkf("GMOUSE ID = %d\n", NowTaskID());
  unsigned new = 0;
  unsigned old = 0;
  unsigned last_left = 0;
  unsigned last_right = 0;
  window_t *choice = NULL;
  for (;;) {
    // logkf("%d\n",mouse_dat_status());
    if (mouse_dat_status() == 0 && key_press_status() == 0 &&
        key_up_status() == 0) {
      api_yield();
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
          gmouse->wheel = NULL;
          gmouse->click_button_last = NULL;
          gmouse->click_textbox_last = NULL;
          int flag = 0;
          if (!drop) {
            for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
              // logkf("%d\n",top);

              if (Collision(gmouse->sht->ctl->sheets[top]->vx0,
                            gmouse->sht->ctl->sheets[top]->vy0,
                            gmouse->sht->ctl->sheets[top]->bxsize,
                            gmouse->sht->ctl->sheets[top]->bysize, gmouse->x,
                            gmouse->y)) {
                flag = 1;
                if (gmouse->sht->ctl->sheets[top]->wnd == NULL) {
                  new = 0;
                  continue;
                }
                gmouse->click_left = gmouse->sht->ctl->sheets[top]->wnd;
                if (gmouse->sht->ctl->sheets[top]->wnd->handle_left != NULL) {
                  gmouse->sht->ctl->sheets[top]->wnd->handle_left(
                      gmouse->sht->ctl->sheets[top]->wnd, gmouse);
                }
                if (gmouse->sht->ctl->sheets[top]->wnd->tid) {
                  new = gmouse->sht->ctl->sheets[top]->wnd->tid;
                } else {
                  new = 0;
                }
                break;
              }
            }
            if (flag == 0) {
              new = 0;
            }
          } else
            drop();
        } else if ((mdec.btn & 0x02) != 0) {
          drop = NULL;
          // 右键
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          int flag = 0;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            if (Collision(gmouse->sht->ctl->sheets[top]->vx0,
                          gmouse->sht->ctl->sheets[top]->vy0,
                          gmouse->sht->ctl->sheets[top]->bxsize,
                          gmouse->sht->ctl->sheets[top]->bysize, gmouse->x,
                          gmouse->y)) {
              flag = 1;
              if (gmouse->sht->ctl->sheets[top]->wnd == NULL) {
                new = 0;
                continue;
              }
              gmouse->click_right = gmouse->sht->ctl->sheets[top]->wnd;
              if (gmouse->sht->ctl->sheets[top]->wnd->handle_right != NULL) {
                gmouse->sht->ctl->sheets[top]->wnd->handle_right(
                    gmouse->sht->ctl->sheets[top]->wnd, gmouse);
              }
              if (gmouse->sht->ctl->sheets[top]->wnd->tid) {
                new = gmouse->sht->ctl->sheets[top]->wnd->tid;
              } else {
                new = 0;
              }
              break;
            }
          }
          if (flag == 0) {
            new = 0;
          }
        } else if (mdec.roll != MOUSE_ROLL_NONE) {
          // 滚动
          drop = NULL;

          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          // logkf("%p\n",gmouse->desktop->window_list);
          int flag = 0;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            if (Collision(gmouse->sht->ctl->sheets[top]->vx0,
                          gmouse->sht->ctl->sheets[top]->vy0,
                          gmouse->sht->ctl->sheets[top]->bxsize,
                          gmouse->sht->ctl->sheets[top]->bysize, gmouse->x,
                          gmouse->y)) {
              flag = 1;
              if (gmouse->sht->ctl->sheets[top]->wnd == NULL) {
                new = 0;
                continue;
              }
              gmouse->wheel = gmouse->sht->ctl->sheets[top]->wnd;

              if (gmouse->sht->ctl->sheets[top]->wnd->handle_mouse_wheel !=
                  NULL) {
                gmouse->sht->ctl->sheets[top]->wnd->handle_mouse_wheel(
                    gmouse->sht->ctl->sheets[top]->wnd, gmouse, mdec.roll);
              }
              if (gmouse->sht->ctl->sheets[top]->wnd->tid) {
                new = gmouse->sht->ctl->sheets[top]->wnd->tid;
              } else {
                new = 0;
              }
              break;
            }
          }
          if (flag == 0) {
            new = 0;
          }
        } else {
          drop = NULL;
          // 停留
          gmouse->click_left = NULL;
          gmouse->click_right = NULL;
          gmouse->stay = NULL;
          gmouse->wheel = NULL;
          // logkf("%p\n",gmouse->desktop->window_list);
          int flag = 0;
          for (int top = gmouse->sht->ctl->top - 1; top != 0; top--) {
            if (Collision(gmouse->sht->ctl->sheets[top]->vx0,
                          gmouse->sht->ctl->sheets[top]->vy0,
                          gmouse->sht->ctl->sheets[top]->bxsize,
                          gmouse->sht->ctl->sheets[top]->bysize, gmouse->x,
                          gmouse->y)) {
              flag = 1;
              if (gmouse->sht->ctl->sheets[top]->wnd == NULL) {
                new = 0;
                continue;
              }
              gmouse->stay = gmouse->sht->ctl->sheets[top]->wnd;

              if (gmouse->sht->ctl->sheets[top]->wnd->handle_stay != NULL) {
                gmouse->sht->ctl->sheets[top]->wnd->handle_stay(
                    gmouse->sht->ctl->sheets[top]->wnd, gmouse);
              }
              if (gmouse->sht->ctl->sheets[top]->wnd->tid) {
                new = gmouse->sht->ctl->sheets[top]->wnd->tid;
              } else {
                new = 0;
              }
              break;
            }
          }
          if (flag == 0) {
            new = 0;
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
    } else if (key_press_status() != 0) {

      window_t *r = NULL;
      for (int i = 1;; i++) {
        List *lw =
            (window_t *)list_search_by_count(i, gmouse->desktop->window_list);
        window_t *w;
        if (lw == NULL) {
          break;
        } else {
          w = lw->val;
        }
        if (!w) {
          continue;
        }
        //   logkf("w = %p\n",w);
        if (w->sht->height == gmouse->sht->ctl->top - 1) {
          r = w;
          if (w->tid) {
            new = w->tid;
          }
          break;
        }
      }
      uint8_t i = get_key_press();
      // if (gmouse->click_textbox_last != NULL) {
      //   gmouse->click_textbox_last->add_char(gmouse->click_textbox_last,
      //                                        keytable1[i]);
      // }
      if (r->fifo_keypress) {
        fifo8_put(r->fifo_keypress, i);
      }
    } else if (key_up_status() != 0) {
      window_t *r = NULL;
      for (int i = 1;; i++) {
        List *lw =
            (window_t *)list_search_by_count(i, gmouse->desktop->window_list);
        window_t *w;
        if (lw == NULL) {
          break;
        } else {
          w = lw->val;
        }
        if (!w) {
          continue;
        }
        //   logkf("w = %p\n",w);
        if (w->sht->height == gmouse->sht->ctl->top - 1) {
          r = w;
          if (w->tid) {
            new = w->tid;
          } else {
            new = 0;
          }
          break;
        }
      }
      uint8_t i = get_key_up();
      if (r->fifo_keyup) {
        fifo8_put(r->fifo_keyup, i);
      }
    }
    // api_yield();
    if (old && old != new) {
      task_set_level_normal(old);
      old = 0;
    }
    if (new) {
      //     logk("SET\n");
      old = new;
      task_set_level_higher(old);
    }
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
