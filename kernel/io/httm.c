#include <dos.h>
static struct SHEET *sht_cur;
static struct SHTCTL *shtctl0;
static struct TIMER *cur_tmr;
static lock_t l,l1;
static int p1 = 0,p2 = 0;
static int f = 0;
mtask *cursor;
color_t color_table[16] = {COL_000000, COL_000084, COL_008400, COL_008484,
                           COL_840000, COL_840084, COL_848400, COL_C6C6C6,
                           COL_848484, COL_0000FF, COL_00FF00, COL_00FFFF,
                           COL_FF0000, COL_FF00FF, COL_FFFF00, COL_FFFFFF};
static void Draw_Cur(vram_t *vram, int x, int y, int xsize) {
  static char *CUR[16] = {"*.......", "*.......", "*.......", "*.......",
                          "*.......", "*.......", "*.......", "*.......",
                          "*.......", "*.......", "*.......", "*.......",
                          "*.......", "*.......", "*.......", "*......."};
  int i, j;
  for (i = 0; i < 16; i++) {
    for (j = 0; j < 8; j++) {
      if (CUR[i][j] == '.') {
        vram[(y + i) * xsize + x + j] = COL_TRANSPARENT;
      } else if (CUR[i][j] == '*') {
        vram[(y + i) * xsize + x + j] = color_table[7];
      }
    }
  }
}
static void put_sht(struct SHEET *sht, int x, int y, int l) {
  // boxfill(sht->buf, sht->bxsize, 0, x, y, x + l * 8 - 1, y + 15);
  sheet_refresh(sht, x, y, x + l * 8, y + 16);
  return;
}
void clear_HighTextMode(struct tty *res) {
  // 高分辨率模式的清屏
  struct SHEET *sht = (struct SHEET *)res->vram;
  for (int i = 0; i != (res->xsize * 8 * res->ysize * 16); i++) {
    sht->buf[i] = color_table[res->color >> 4];
  }
  sheet_refresh(sht, 0, 0, res->xsize * 8, res->ysize * 16);
  res->x = 0;
  res->y = 0;
  res->MoveCursor(res, res->x, res->y);
}
void screen_ne_HighTextMode(struct tty *res) {
  // 高分辨率模式下的屏幕滚动(向下,每次移动一行(8*16))
  struct SHEET *sht = (struct SHEET *)res->vram;
  memcpy((void *)sht->buf,
         (void *)sht->buf + res->xsize * 8 * 16 * sizeof(color_t),
         res->xsize * 8 * (res->ysize - 1) * 16 * sizeof(color_t));
  for (int i = (res->xsize * 8 * (res->ysize - 1) * 16);
       i != (res->xsize * 8 * res->ysize * 16); i++) {
    sht->buf[i] = color_table[res->color >> 4];
  }
  sheet_refresh(sht, 0, 0, res->xsize * 8, res->ysize * 16);
  res->x = 0;
  res->y = res->ysize - 1;
  res->Raw_y++;
  res->MoveCursor(res, res->x, res->y);
}
void putchar_HighTextMode(struct tty *res, int c) {
  if (cur_tmr != NULL && cursor != NULL) {
    lock(&l);
    f = 1;
    task_run(cursor);
    unlock(&l);
  }
  lock(&l1);
  struct SHEET *sht = (struct SHEET *)res->vram;
  unsigned char *p = (unsigned char *)&c;
  if (p[0] == '\r') {
    res->MoveCursor(res, 0, res->y);
    unlock(&l1);
    return;
  }
  if(c > 0x80 && p1 == 0) {
    p1 = c;
    unlock(&l1);
    return;
  }
  if(c <= 0x80 && p1) {
    p1 = 0;
    unlock(&l1);
    putchar_HighTextMode(res,c);
    return;
  }
  if (res->y == res->ysize - 1 && res->x == res->xsize) {
    res->screen_ne(res);
  }
  if (res->x == res->xsize) {
    res->y++;
    res->x = 0;
    res->MoveCursor(res, res->x, res->y);
  }
  unsigned char str[3];
  str[0] = p[0];
  str[1] = p[1];
  str[2] = 0;
  if (str[0] == '\n') {
    if (res->y >= res->ysize - 1) {
      res->screen_ne(res);
      unlock(&l1);
      return;
    }
    res->y++;
    res->x = 0;
    res->MoveCursor(res, res->x, res->y);
    unlock(&l1);
    return;
  } else if (str[0] == '\b') {
    if (res->x > 0) {
      res->x--;
      int bmx = res->x;
      int bmy = res->y;
      SDraw_Box(sht->buf, res->x * 8, res->y * 16, res->x * 8 + 8,
                res->y * 16 + 16, color_table[res->color >> 4], res->xsize * 8);
      res->x++;
      put_sht(sht, bmx * 8, bmy * 16, 1);
      bmx = res->x;
      bmy = res->y;
      SDraw_Box(sht->buf, res->x * 8, res->y * 16, res->x * 8 + 8,
                res->y * 16 + 16, color_table[res->color >> 4], res->xsize * 8);
      res->x++;
      put_sht(sht, bmx * 8, bmy * 16, 1);
      res->x -= 2;
    } else if (res->x == 0) {
      if (res->y != 0) {
        res->x = res->xsize - 1;
        res->y--;
      }
    }
    res->MoveCursor(res, res->x, res->y);
    unlock(&l1);
    return;
  } else if(str[0] == '\t') {
    unlock(&l1);
    putchar_HighTextMode(res,' ');
    putchar_HighTextMode(res,' ');
    putchar_HighTextMode(res,' ');
    putchar_HighTextMode(res,' ');
    
    return;
  }
  if (c > 0x80 && p1) {
    str[0] = p1;
    str[1] = c;
    str[2] = 0;
    unsigned int cn_ch = *(unsigned int *)(str);
    int bmx = res->x;
    int bmy = res->y;
    if (res->x == res->xsize - 1) {
      res->y++;
      res->x = 0;
      res->MoveCursor(res, res->x, res->y);
    }
    SDraw_Box(sht->buf, res->x * 8, res->y * 16, res->x * 8 + 16,
              res->y * 16 + 16, color_table[res->color >> 4], res->xsize * 8);
    PUTCHINESE0(sht->buf, res->x * 8, res->y * 16,
                color_table[res->color & 0xf], cn_ch, res->xsize * 8);
    res->x += 2;
    put_sht(sht, bmx * 8, bmy * 16, 2);
    res->MoveCursor(res, res->x, res->y);
    p1 = 0;
    unlock(&l1);
    return;
  }
  c = p[0];
  int bmx = res->x;
  int bmy = res->y;
  SDraw_Box(sht->buf, res->x * 8, res->y * 16, res->x * 8 + 8, res->y * 16 + 16,
            color_table[res->color >> 4], res->xsize * 8);
  SDraw_Char(sht->buf, res->x * 8, res->y * 16, c,
             color_table[res->color & 0xf], res->xsize * 8);
  res->x++;
  put_sht(sht, bmx * 8, bmy * 16, 1);
  res->MoveCursor(res, res->x, res->y);
  unlock(&l1);
}
void MoveCursor_HighTextMode(struct tty *res, int x, int y) {
  res->x = x;
  res->y = y;
  if (!res->cur_moving)
    return;
  sheet_slide(sht_cur, res->x * 8, res->y * 16);
}
void Draw_Box_HighTextMode(struct tty *res, int x, int y, int x1, int y1,
                           unsigned char color) {
  struct SHEET *sht = (struct SHEET *)res->vram;
  for (int i = y * 16; i <= y1 * 16; i++) {
    for (int j = x * 8; j <= x1 * 8; j++) {
      if (sht->buf[i * sht->bxsize + j] == color_table[res->color & 0xf]) {
        sht->buf[i * sht->bxsize + j] = color_table[color & 0xf];
      } else {
        sht->buf[i * sht->bxsize + j] = color_table[color >> 4];
      }
    }
  }
  sheet_refresh(sht, x * 8, y * 16, x1 * 8, y1 * 16);
}
/*void Gar_Test_Task() {
  char fifo_buf[128];
  struct FIFO8 fifo;
  fifo8_init(&fifo, 128, fifo_buf);
  struct TIMER *timer;
  timer = timer_alloc();
  timer_init(timer, &fifo, 1);
  timer_settime(timer, 250);
  while (1) {
    sheet_slide(sht_cur, tty_h->x * 8, tty_h->y * 16);
    if (fifo8_status(&fifo) != 0) {
      int i = fifo8_get(&fifo);
      if (i == 1) {
        sheet_updown(sht_cur, -1);
        timer_init(timer, &fifo, 2);
        timer_settime(timer, 250);
      } else if (i == 2) {
        sheet_updown(sht_cur, 1);
        timer_init(timer, &fifo, 1);
        timer_settime(timer, 250);
      }
    }
  }
}*/
int c = 0;
lock_t ll;
void high_text_cursor_task_exited(mtask *task) {
  if (cursor == task) {
    cursor = NULL;
    cur_tmr = NULL;
  }
}
void cur_service() {
  lock(&l);
  cur_tmr = timer_alloc();
  if (cur_tmr == NULL) {
    unlock(&l);
    task_exit((unsigned)-1);
    return;
  }
  cur_tmr->waiter = current_task();
  unsigned char buf[50];
  struct FIFO8 fifo;
  fifo8_init(&fifo, 50, buf);
  timer_init(cur_tmr, &fifo, 1);
  unlock(&l);
  int j = 0;
  while (1) {
    timer_settime(cur_tmr, 50);
    while (fifo8_status(&fifo) == 0) {
      lock(&l);
      if (f) {
        sheet_updown(sht_cur, 1);
        f = 0;
        j = 1;
      }
      unlock(&l);
      task_fall_blocked_reason(WAITING, WAIT_REASON_TIMER);
    }
    if(j) {
      fifo8_get(&fifo);
      j = 0;
      continue;
    }
    int i = fifo8_get(&fifo);
    if (sht_cur->height <= -1) {
      sheet_updown(sht_cur, 1);
    } else {
      sheet_updown(sht_cur, -1);
    }
  }
}
int default_tty_fifo_status(struct tty *res);
int default_tty_fifo_get(struct tty *res);
bool SwitchToHighTextMode(void) {
  if (set_mode(1024, 768, 32) == (unsigned)(-1)) {
    printk("Can't enable 1024x768x32 VBE mode.\n\n");
    return false;
  }
  lock_init(&l);
  lock_init(&l1);
  cur_tmr = NULL;
  struct VBEINFO *vinfo = (struct VBEINFO *)VBEINFO_ADDRESS;
  shtctl0 = shtctl_init((vram_t *)(uintptr_t)vinfo->vram, vinfo->xsize, vinfo->ysize);
  if (shtctl0 == NULL) {
    return false;
  }
  size_t screen_buffer_size =
      (vinfo->xsize + 1) * (vinfo->ysize + 1) * sizeof(color_t);
  size_t cursor_buffer_size = 16 * 32 * sizeof(color_t);
  vram_t *scr_buf =
      page_malloc(screen_buffer_size);
  vram_t *cur_buf = page_malloc(cursor_buffer_size);
  if (scr_buf == NULL || cur_buf == NULL) {
    if (scr_buf != NULL) {
      page_free(scr_buf, screen_buffer_size);
    }
    if (cur_buf != NULL) {
      page_free(cur_buf, cursor_buffer_size);
    }
    ctl_free(shtctl0);
    shtctl0 = NULL;
    return false;
  }
  struct SHEET *sht_scr = sheet_alloc(shtctl0);
  sht_cur = sheet_alloc(shtctl0);
  if (sht_scr == NULL || sht_cur == NULL) {
    if (sht_scr != NULL) {
      sheet_free(sht_scr);
    }
    if (sht_cur != NULL) {
      sheet_free(sht_cur);
    }
    page_free(scr_buf, screen_buffer_size);
    page_free(cur_buf, cursor_buffer_size);
    ctl_free(shtctl0);
    shtctl0 = NULL;
    sht_cur = NULL;
    return false;
  }
  sheet_setbuf(sht_scr, scr_buf, vinfo->xsize, vinfo->ysize, -1);
  sheet_setbuf(sht_cur, cur_buf, 8, 16, COL_TRANSPARENT);
  memset(scr_buf, 0, vinfo->xsize * vinfo->ysize * sizeof(color_t));
  Draw_Cur(cur_buf, 0, 0, 8);
  sheet_slide(sht_scr, 0, 0);
  sheet_slide(sht_cur, 0, 0);
  sheet_updown(sht_scr, 0);
  sheet_updown(sht_cur, 1);
  /*stack = (unsigned int)page_malloc(64 * 1024);
  t1 = AddTask("t1", 1, 2 * 8, (int)Gar_Test_Task, 1 * 8, 1 * 8,
               stack + 64 * 1024);*/

  cursor = create_task((uintptr_t)cur_service, 1);
  if (cursor == NULL) {
    WARNING_K("unable to create high-text cursor task");
    sheet_free(sht_cur);
    sheet_free(sht_scr);
    page_free(scr_buf, screen_buffer_size);
    page_free(cur_buf, cursor_buffer_size);
    ctl_free(shtctl0);
    shtctl0 = NULL;
    sht_cur = NULL;
    return false;
  }
  cursor->sched_flags = TASK_SCHED_PINNED;
  cursor->cpu = 0;
  task_set_name(cursor, "cursor");
  struct tty *tty_h = tty_alloc((void *)sht_scr, vinfo->xsize / 8,
                                vinfo->ysize / 16, putchar_HighTextMode,
                                MoveCursor_HighTextMode, clear_HighTextMode,
                                screen_ne_HighTextMode, Draw_Box_HighTextMode,default_tty_fifo_status,default_tty_fifo_get);
  if (tty_h == NULL) {
    task_abort_creation(cursor);
    cursor = NULL;
    sheet_free(sht_cur);
    sheet_free(sht_scr);
    page_free(scr_buf, screen_buffer_size);
    page_free(cur_buf, cursor_buffer_size);
    ctl_free(shtctl0);
    shtctl0 = NULL;
    sht_cur = NULL;
    return false;
  }
  if (!task_publish(cursor)) {
    task_abort_creation(cursor);
    cursor = NULL;
    tty_free(tty_h);
    sheet_free(sht_cur);
    sheet_free(sht_scr);
    page_free(scr_buf, screen_buffer_size);
    page_free(cur_buf, cursor_buffer_size);
    ctl_free(shtctl0);
    shtctl0 = NULL;
    sht_cur = NULL;
    return false;
  }
  tty_set_default(tty_h);
  tty_set(current_task(), tty_h);
  return true;
}
void SwitchShell_HighTextMode(int i) {
  // extern struct List* tty_list;
  // extern struct tty* tty_default;
  // struct tty* t = (struct tty*)list_get(i + 1, tty_list)->val;
  // struct tty* n = NULL;
  // for (int j = 1; list_get(j, tty_list) != 0; j++) {
  //   n = (struct tty*)list_get(j, tty_list)->val;
  //   struct SHEET* sht = (struct SHEET*)n->vram;
  //   if (sht->height == 0) {
  //     break;
  //   } else {
  //     n = NULL;
  //   }
  // }
  // if (n == NULL) {
  //   n = tty_default;
  // }
  // if (n == t) {
  //   return;
  // }
  // // 交换
  // struct SHEET* sht_t = (struct SHEET*)t->vram;
  // struct SHEET* sht_n = (struct SHEET*)n->vram;
  // sheet_updown(sht_n, -1);
  // sheet_updown(sht_t, 0);
  // for (int j = 1; get_task(j) != 0; j++) {
  //   mtask* task = get_task(j);
  //   if (task->TTY == t && (strcmp("Shell", task->name) == 0 ||
  //                          (task->app == 1 && task->forever == 0))) {
  //     task->sleep = 0;
  //     if (task->fifosleep == 3) {
  //       task->fifosleep = 0;
  //     }
  //   } else if ((task->TTY == n || task->TTY->using1 != 1) &&
  //              (strcmp("Shell", task->name) == 0 ||
  //               (task->app == 1 && task->forever == 0))) {
  //     if (task->fifosleep == 0) {
  //       task->fifosleep = 3;
  //     }
  //   }
  // }
  // t->MoveCursor(t, t->x, t->y);
}
bool now_tty_HighTextMode(struct tty *res) {
  struct SHEET *sht = (struct SHEET *)res->vram;
  if (sht->height == 0) {
    return true;
  } else {
    return false;
  }
}
