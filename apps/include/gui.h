#ifndef PLOS_GUI_H
#define PLOS_GUI_H

#include <ctypes.h>
#include <gui_rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gui_window *window_t;

window_t create_window(const char *title, int x, int y, int width, int height);
int window_get_event(window_t window);
void close_window(window_t window);
int window_set_title(window_t window, const char *title);
void draw_px(window_t window, int x, int y, int color);
void window_refresh(window_t window, int first, int last);
void *window_get_fb(window_t window);
void window_start_recv_keyboard(window_t window);
void window_stop_recv_keyboard(window_t window);

int window_get_key_press_data(window_t window);
int window_get_key_press_status(window_t window);
int window_get_key_up_data(window_t window);
int window_get_key_up_status(window_t window);

#ifdef __cplusplus
}
#endif

#endif
