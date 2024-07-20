#pragma once
#include <define.h>
#include <type.h>
typedef enum {
  MODE_A = 'A',
  MODE_B = 'B',
  MODE_C = 'C',
  MODE_D = 'D',
  MODE_E = 'E',
  MODE_F = 'F',
  MODE_G = 'G',
  MODE_H = 'H',
  MODE_f = 'f',
  MODE_J = 'J',
  MODE_K = 'K',
  MODE_S = 'S',
  MODE_T = 'T',
  MODE_m = 'm'
} vt100_mode_t;
struct tty {
  int   is_using;                                     // 使用标志
  void *vram;                                         // 显存（也可以当做图层）
  int   x, y;                                         // 目前的 x y 坐标
  int   xsize, ysize;                                 // x 坐标大小 y 坐标大小
  int   Raw_y;                                        // 换行次数
  int   cur_moving;                                   // 光标需要移动吗
  u8    color;                                        // 颜色
  void (*putchar)(struct tty *res, int c);            // putchar函数
  void (*MoveCursor)(struct tty *res, int x, int y);  // MoveCursor函数
  void (*clear)(struct tty *res);                     // clear函数
  void (*screen_ne)(struct tty *res);                 // screen_ne函数
  void (*gotoxy)(struct tty *res, int x, int y);      // gotoxy函数
  void (*print)(struct tty *res, const char *string); // print函数
  void (*Draw_Box)(struct tty *res, int x, int y, int x1, int y1,
                   u8 color); // Draw_Box函数
  int (*fifo_status)(struct tty *res);
  int (*fifo_get)(struct tty *res);
  u32 reserved[4]; // 保留项

  //////////////实现VT100需要的//////////////////

  int          vt100;       // 是否检测到标志
  char         buffer[81];  // 缓冲区
  int          buf_p;       // 缓冲区指针
  int          done;        // 这个东西读取完毕没有？
  vt100_mode_t mode;        // 控制模式
  int          color_saved; // 保存的颜色
};