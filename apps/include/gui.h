#ifndef GUI_H
#define GUI_H
typedef unsigned window_t;
window_t create_window(char *title,int x,int y,int w,int h);
int window_get_event(window_t wnd);
void close_window(window_t wnd);
void draw_px(window_t wnd,int x,int y,int c);
void window_refresh(window_t wnd,int x1y1,int x2y2);
unsigned window_get_fb(window_t wnd);
void window_start_recv_keyboard(window_t wnd);
void window_stop_recv_keyboard(window_t wnd);

int window_get_key_press_data(window_t wnd);
int window_get_key_press_status(window_t wnd);
int window_get_key_up_data(window_t wnd);
int window_get_key_up_status(window_t wnd);
#endif