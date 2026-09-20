#include <stdio.h>
#include <gui.h>
#include <stdlib.h>
#include <math.h>
#define EXTRACT_X(xy) (xy >> 16)
#define EXTRACT_Y(xy) (xy & 0xffff)
#define PACK_XY(x, y) (((unsigned)(x) << 16) | (unsigned)(y))
#define MAX(a,b) (a > b ? a : b)
#define MIN(a,b) (a < b ? a : b)


unsigned PACK_XYL(int x,int y,int x1,int y1) {
  return PACK_XY(MIN(x,x1) - 1,MIN(y,y1) - 1);
}
unsigned PACK_XYR(int x,int y,int x1,int y1) {
return PACK_XY(MAX(x,x1) + 1,MAX(y,y1) + 1);
}
void draw_line(unsigned int *fb,int x1,int y1,int x2,int y2,int xsize,unsigned col) {
 int dx = x2-x1;
 int dy = y2-y1;
 int x = x1;
 int y = y1;
 int x_inc = 1;
 int y_inc = 1;
 int err = 0;
 int dx2= dx*2;
 int dy2 = dy*2;
 if (dx < 0) {
   x_inc = -1;
   dx = -dx;
   dx2 = -dx2;
 }
 if(dy < 0) {
   y_inc = -1;
   dy = -dy;
   dy2 = -dy2;
 }
 if(dx > dy) {
   err = dx;
   while (x != x2){
     fb[y * xsize + x] = col;
     err -= dy2;
     if (err < 0) {
       y += y_inc;
       err += dx2;
     }
     x += x_inc;
   }
 } else {
   err = dy;
   while(y != y2) {
     fb[y * xsize + x] = col;
     err -= dx2;
     if(err < 0) {
       x += x_inc;
       err += dy2;
     }
     y += y_inc;
   }
 }
 fb[y * xsize + x] = col;
}
#define PACK(x,y,x1,y1) PACK_XYL(x,y,x1,y1),PACK_XYR(x,y,x1,y1)
int main() {
  window_t wnd = create_window("My first Plant OS Window", (1024 - 640) / 2,
                               (768 - 480) / 2, 640, 480, 0);
  unsigned *framebuffer = (unsigned *)window_get_fb(wnd);
  printf("%p\n",framebuffer);
  int mx = -1,my = -1;
  for (;;) {
    gui_event_t event;
    if (window_get_event(wnd, &event) <= 0)
      continue;
    if (event.type == GUI_EVENT_CLOSE_WINDOW) {
      close_window(wnd);
      break;
    }
    int x = event.x, y = event.y;
    if (!(event.buttons & 1) || x < 4 || y < 24 || x >= 636 || y >= 476) {
      mx = my = -1;
      continue;
    }
    if (mx < 0) {
      framebuffer[y * 640 + x] = 0;
      window_refresh(wnd, PACK_XY(x, y), PACK_XY(x + 1, y + 1));
    } else if (mx != x || my != y) {
      draw_line(framebuffer, mx, my, x, y, 640, 0);
      window_refresh(wnd, PACK(mx, my, x, y));
    }
    mx = x;
    my = y;
  }
  return 0;
}
