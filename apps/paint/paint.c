#include <stdio.h>
#include <gui.h>
#include <stdlib.h>
#include <math.h>
#define EXTRACT_X(xy) (xy >> 16)
#define EXTRACT_Y(xy) (xy & 0xffff)
#define PACK_XY(x,y) (( x << 16) | y)
enum {
  MOUSE_STAY = 1,
  MOUSE_CLICK_LEFT,
  MOUSE_CLICK_RIGHT,
  CLOSE_WINDOW,
  MOUSE_WHEEL
};
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
  window_t wnd = create_window("My first Plant OS Window",(1024-640)/2,(768-480)/2,640,480);
  unsigned *framebuffer = (unsigned *)window_get_fb(wnd);
  printf("%p\n",framebuffer);
  int mx = -1,my = -1;
  for(;;) {
    int i = window_get_event(wnd);
    if (i != -1) {
      if (i == CLOSE_WINDOW)
      {
        close_window(wnd);
        break;
      }
      if (i == MOUSE_CLICK_LEFT) {
        unsigned j = window_get_event(wnd);

        if(mx == -1) {
          framebuffer[EXTRACT_Y(j) * 640 + EXTRACT_X(j)] = 0;
          window_refresh(wnd,j,j+0x00010001);
          mx = EXTRACT_X(j);
          my = EXTRACT_Y(j);
        } else {
          if(mx != EXTRACT_X(j) || my != EXTRACT_Y(j)) {
            draw_line(framebuffer,mx,my,EXTRACT_X(j),EXTRACT_Y(j),640,0);
            window_refresh(wnd,PACK(mx,my,EXTRACT_X(j),EXTRACT_Y(j)));
            mx = EXTRACT_X(j);
            my = EXTRACT_Y(j);
          }
        }
      } else if (i == MOUSE_STAY) {
        unsigned j = window_get_event(wnd);
        mx = -1;
        my = -1;
      }
      else if (i == MOUSE_CLICK_RIGHT) {

        unsigned j = window_get_event(wnd);
      }
    }
  }
  return 0;
}
