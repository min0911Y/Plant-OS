#ifndef PLOS_GUI_H
#define PLOS_GUI_H

#include <ctypes.h>
#include <gui_rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gui_window *window_t;
typedef struct {
  uint32_t *pixels;
  size_t pitch;
  uint32_t width, height;
} window_buffer_t;

window_t create_window(const char *title, int x, int y, int width, int height,
                       unsigned flags);
/* Returns 1 for an event, 0 when empty, negative for invalid arguments. */
int window_get_event(window_t window, gui_event_t *event);
/* Synchronous resize, including decorations. On success reacquire all drawing
 * pointers. The window owner serializes this with drawing and event reads. */
int window_resize(window_t window, unsigned width, unsigned height);
void close_window(window_t window);
int window_set_title(window_t window, const char *title);
/* GUI_ICON_SIZE squared ARGB8888 pixels, or NULL to clear the title icon. */
int window_set_icon(window_t window, const uint32_t *pixels);
void draw_px(window_t window, int x, int y, int color);
/* Queue damage; the render buffer remains shared until it is consumed. */
void window_refresh(window_t window, int first, int last);
/* Publish complete pixels and wait until the compositor has copied them. */
int window_present(window_t window, int first, int last);
/* Transfer the current drawing plane and wait for display. On success, all
 * previous drawing pointers are invalid: reacquire with window_get_buffer().
 * The returned plane contains an older frame inside the submitted rectangle;
 * redraw that rectangle completely. Other pixels are preserved by the server.
 * Calls and drawing are serialized by the window owner, including refreshes. */
int window_present_frame(window_t window, int first, int last);
void *window_get_fb(window_t window);
/* ARGB8888 drawing plane, valid until resize, frame exchange or close_window().
 */
int window_get_buffer(window_t window, window_buffer_t *buffer);
void window_start_recv_keyboard(window_t window);
void window_stop_recv_keyboard(window_t window);
/* Wake rpc_serve_once() when a window event or keyboard input is queued. */
int window_set_event_notifications(window_t window, bool enabled);
/* The target must be a live thread in the window owner's task group.
 * A zero TID disables notifications. No application IPC is consumed here. */
int window_set_event_target(window_t window, unsigned tid, unsigned generation);
int window_get_state(window_t window, gui_window_state_t *state);
int window_control(window_t window, enum gui_window_control operation, int x,
                   int y);

int window_get_key_press_data(window_t window);
int window_get_key_press_status(window_t window);
int window_get_key_up_data(window_t window);
int window_get_key_up_status(window_t window);

#ifdef __cplusplus
}
#endif

#endif
