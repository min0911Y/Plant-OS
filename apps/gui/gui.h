#ifndef __GUI_H__
#define __GUI_H__
typedef unsigned int vram_t;
typedef vram_t color_t;
#include "list.h"
#include <ctypes.h>
struct tty {
  int using1;                              // 使用标志
  void *vram;                              // 显存（也可以当做图层）
  int x, y;                                // 目前的 x y 坐标
  int xsize, ysize;                        // x 坐标大小 y 坐标大小
  int Raw_y;                               // 换行次数
  int cur_moving;                          // 光标需要移动吗
  unsigned char color;                     // 颜色
  void (*putchar)(struct tty *res, int c); // putchar函数
  void (*MoveCursor)(struct tty *res, int x, int y);  // MoveCursor函数
  void (*clear)(struct tty *res);                     // clear函数
  void (*screen_ne)(struct tty *res);                 // screen_ne函数
  void (*gotoxy)(struct tty *res, int x, int y);      // gotoxy函数
  void (*print)(struct tty *res, const char *string); // print函数
  void (*Draw_Box)(struct tty *res, int x, int y, int x1, int y1,
                   unsigned char color); // Draw_Box函数
  unsigned int reserved[4];              // 保留项
};
typedef struct desktop desktop_t;
typedef struct window window_t;
typedef struct super_window super_window_t;
typedef struct gmouse gmouse_t;
typedef struct console console_t;
typedef struct button button_t;
typedef struct textbox textbox_t;
#define COL_000000 0x00000000
#define COL_FF0000 0x00ff0000
#define COL_00FF00 0x0000ff00
#define COL_FFFF00 0x00ffff00
#define COL_0000FF 0x000000ff
#define COL_FF00FF 0x00ff00ff
#define COL_00FFFF 0x0000ffff
#define COL_C6C6C6 0x00c6c6c6
#define COL_848484 0x00848484
#define COL_840000 0x00840000
#define COL_008400 0x00008400
#define COL_848400 0x00848400
#define COL_000084 0x00000084
#define COL_840084 0x00840084
#define COL_008484 0x00008484
#define COL_FFFFFF 0x00ffffff
#define COL_TRANSPARENT 0x50ffffff
#define argb(a, r, g, b) ((a) << 24 | (r) << 16 | (g) << 8 | (b))
void gui();

void gui_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx,
             int eax);
typedef struct {
  List *phead; // 队头
  List *ctl;
} queue_t;
queue_t *queue_init();
void queue_push(queue_t *q, unsigned value);
unsigned queue_pop(queue_t *q);
void queue_free(queue_t *q);
struct FIFO8 {
  unsigned char *buf;
  int p, q, size, free, flags;
};
struct FIFO32 {
  int *buf;
  int size, free, flags;
  int next_r, next_w;
};
void fifo8_init(struct FIFO8 *fifo, int size, unsigned char *buf);
int fifo8_put(struct FIFO8 *fifo, unsigned char data);
int fifo8_get(struct FIFO8 *fifo);
int fifo8_status(struct FIFO8 *fifo);

void fifo32_init(struct FIFO32 *fifo, int size, int *buf);
int fifo32_put(struct FIFO32 *fifo, int data);
int fifo32_get(struct FIFO32 *fifo);
int fifo32_status(struct FIFO32 *fifo);
#define MAX_SHEETS 256


struct desktop {
  bool using1;
  struct SHTCTL *shtctl;
  struct SHEET *sht;
  vram_t *vram;
  unsigned tid;
  int xsize, ysize;
  struct List *window_list;
  void (*display)(desktop_t *desktop, vram_t *vram);
  void (*hide)(desktop_t *desktop);
  void (*draw)(desktop_t *desktop, int x, int y, int x1, int y1, color_t color);
  void (*puts)(desktop_t *desktop, char *s, int x, int y, color_t color);
};
struct MOUSE_DEC {
  unsigned char buf[4], phase;
  int x, y, btn;
  int sleep;
  char roll;
};
extern struct MOUSE_DEC mdec;
desktop_t *create_desktop(int xsize, int ysize, unsigned tid);
desktop_t *get_now_desktop();

struct window {
  bool using1;
  desktop_t *desktop;
  console_t *console;
  super_window_t *super_window;
  struct SHEET *sht;
  vram_t *vram;
  unsigned tid;
  int xsize, ysize, x, y;
  char *title;
  struct FIFO8 *fifo_keypress;
  struct FIFO8 *fifo_keyup;
  struct FIFO32 *events;
  unsigned int event[32];
  void (*display)(window_t *window, int x, int y, int pos);
  void (*hide)(window_t *window);
  void (*draw)(window_t *window, int x, int y, int x1, int y1, color_t color);
  void (*puts)(window_t *window, char *s, int x, int y, color_t color);
  void (*handle_left)(window_t *window, gmouse_t *gmouse);
  void (*handle_right)(window_t *window, gmouse_t *gmouse);
  void (*handle_stay)(window_t *window, gmouse_t *gmouse);
  void (*handle_left_for_api)(window_t *window, gmouse_t *gmouse);
  void (*close)(window_t *window);
};

struct super_window {
  window_t *window;
  struct SHTCTL *shtctl;
  struct SHEET *sht_copy;
  vram_t *vram_copy;
  struct List *sht_list;
  struct List *button_list;
  struct List *textbox_list;
  struct SHEET *(*create_sheet)(super_window_t *super_window, int xsize,
                                int ysize, int x, int y, int pos);
  void (*draw_sheet)(super_window_t *super_window, struct SHEET *sht, int x,
                     int y, int x1, int y1, color_t color);
  void (*puts_sheet)(super_window_t *super_window, struct SHEET *sht, char *s,
                     int x, int y, color_t color);
  void (*slide_sheet)(super_window_t *super_window, struct SHEET *sht, int x,
                      int y);
  void (*updown_sheet)(super_window_t *super_window, struct SHEET *sht,
                       int pos);
  void (*handle_left)(window_t *window, gmouse_t *gmouse);
  void (*handle_right)(window_t *window, gmouse_t *gmouse);
  void (*handle_stay)(window_t *window, gmouse_t *gmouse);
  void (*close)(super_window_t *super_window);
};

window_t *create_window(desktop_t *desktop, char *title, int xsize, int ysize,
                        unsigned tid);
super_window_t *create_super_window(window_t *window);

struct gmouse {
  struct SHEET *sht;
  unsigned tid;
  desktop_t *desktop;
  int x, y;
  window_t *click_left;
  window_t *click_right;
  window_t *stay;
  button_t *click_button_last;
  textbox_t *click_textbox_last;
};

bool Collision(int x, int y, int w, int h, int x1, int y1);
gmouse_t *create_gmouse(desktop_t *desktop, int x, int y, int pos);

struct console {
  window_t *window;
  struct tty *tty;
  unsigned tid;
  int xsize, ysize, x, y;
  struct SHTCTL *shtctl;
  vram_t *vram_copy, *vram_cur;
  struct SHEET *sht_copy, *sht_cur;
  void (*handle_left)(console_t *console, gmouse_t *gmouse);
  void (*handle_right)(console_t *console, gmouse_t *gmouse);
  void (*handle_stay)(console_t *console, gmouse_t *gmouse);
  void (*close)(console_t *console);
};

bool now_tty_GraphicMode(struct tty *res);
color_t text_color_to_real_color(unsigned char text_color, bool back_or_font);
console_t *create_console(window_t *window, int xsize, int ysize, int x, int y);

struct button {
  super_window_t *super_window;
  struct SHEET *sht;
  vram_t *vram;
  char *text;
  void (*handle_left)(button_t *button);
  void (*click)(button_t *button);
  void (*close)(button_t *button);
};

button_t *create_button(super_window_t *super_window, char *text, int xsize,
                        int ysize, int x, int y,
                        void (*click)(button_t *button));

struct textbox {
  super_window_t *super_window;
  struct SHEET *sht;
  vram_t *vram;
  char text[255];
  void (*add_char)(textbox_t *textbox, char c);
  void (*close)(textbox_t *textbox);
};
void SDraw_Box(vram_t *vram, int x, int y, int x1, int y1, color_t color,
               int xsize);
void Sputs(vram_t *vram, char *str, int x, int y, color_t col, int xsize);
void SDraw_Char(vram_t *vram1, int x, int y, char c, color_t color, int xsize);
void boxfill(vram_t *vram, int xsize, color_t c, int x0, int y0, int x1,
             int y1);
bool Collision(int x, int y, int w, int h, int x1, int y1);
struct SHEET {
  vram_t *buf;
  int bxsize, bysize, vx0, vy0, col_inv, height, flags;
  struct SHTCTL *ctl;
  window_t *wnd;
};
struct SHTCTL {
  vram_t *vram;
  unsigned char *map;
  int xsize, ysize, top;
  struct SHEET *sheets[MAX_SHEETS];
  struct SHEET sheets0[MAX_SHEETS];
};

#endif