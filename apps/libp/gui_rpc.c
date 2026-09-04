#include <gui.h>
#include <gui_rpc.h>
#include <rpc.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

struct gui_window {
  struct gui_window *next;
  uint32_t id;
  uintptr_t mapping;
  uint32_t mapping_size;
  uint32_t width;
  uint32_t height;
  gui_window_shared_t *shared;
};

static struct gui_window *gui_windows;
static rpc_endpoint_t gui_endpoint;
static int gui_endpoint_ready;
static volatile unsigned gui_windows_lock;

#define GUI_CONNECT_TIMEOUT_MS 15000u

static void gui_lock(void) {
  while (__atomic_exchange_n(&gui_windows_lock, 1, __ATOMIC_ACQUIRE)) {
  }
}

static void gui_unlock(void) {
  __atomic_store_n(&gui_windows_lock, 0, __ATOMIC_RELEASE);
}

static int gui_connect(void) {
  if (gui_endpoint_ready) {
    return RPC_OK;
  }
  int result = rpc_connect(GUI_SERVICE_NAME, &gui_endpoint,
                           GUI_CONNECT_TIMEOUT_MS);
  if (result == RPC_OK) {
    gui_endpoint_ready = 1;
  }
  return result;
}

static void gui_connection_failed(int result) {
  if (result == RPC_ERR_NO_SERVICE || result == RPC_ERR_TRANSPORT) {
    gui_endpoint_ready = 0;
  }
}

static int gui_call(unsigned opcode, const void *arg, unsigned arg_len,
                    void *ret, unsigned ret_cap, unsigned *ret_len) {
  int result = gui_connect();
  if (result != RPC_OK) {
    return result;
  }
  result = rpc_call(&gui_endpoint, opcode, arg, arg_len, ret, ret_cap, ret_len,
                    0);
  gui_connection_failed(result);
  return result;
}

static uintptr_t gui_mapping_find(unsigned size) {
  uintptr_t address = GUI_SHARED_REGION_START;
  for (struct gui_window *window = gui_windows; window != NULL;
       window = window->next) {
    if (size <= window->mapping - address) {
      return address;
    }
    address = window->mapping + window->mapping_size;
  }
  return size <= GUI_SHARED_REGION_END - address ? address : 0;
}

static void gui_window_insert(struct gui_window *window) {
  struct gui_window **link = &gui_windows;
  while (*link != NULL && (*link)->mapping < window->mapping) {
    link = &(*link)->next;
  }
  window->next = *link;
  *link = window;
}

static void gui_window_remove(struct gui_window *window) {
  for (struct gui_window **link = &gui_windows; *link != NULL;
       link = &(*link)->next) {
    if (*link == window) {
      *link = window->next;
      return;
    }
  }
}

window_t create_window(const char *title, int x, int y, int width, int height) {
  if (title == NULL || width <= 0 || height <= 0) {
    return NULL;
  }

  unsigned title_length = strlen(title);
  unsigned mapping_size;
  if (title_length > GUI_TITLE_MAX ||
      !gui_window_shared_mapping_size((unsigned)width, (unsigned)height,
                                      &mapping_size) ||
      mapping_size > GUI_SHARED_REGION_END - GUI_SHARED_REGION_START) {
    return NULL;
  }

  struct gui_window *window = malloc(sizeof(*window));
  if (window == NULL) {
    return NULL;
  }
  gui_lock();
  uintptr_t mapping = gui_mapping_find(mapping_size);
  if (mapping != 0) {
    window->id = 0;
    window->mapping = mapping;
    window->mapping_size = mapping_size;
    window->width = (unsigned)width;
    window->height = (unsigned)height;
    window->shared = (gui_window_shared_t *)(uintptr_t)mapping;
    gui_window_insert(window);
  }
  gui_unlock();
  if (mapping == 0) {
    free(window);
    return NULL;
  }

  unsigned char request_buffer[sizeof(gui_rpc_create_request_t) + GUI_TITLE_MAX];
  gui_rpc_create_request_t *request =
      (gui_rpc_create_request_t *)request_buffer;
  request->x = x;
  request->y = y;
  request->width = (unsigned)width;
  request->height = (unsigned)height;
  request->client_mapping = mapping;
  request->title_length = title_length;
  memcpy(request_buffer + sizeof(*request), title, title_length);

  gui_rpc_create_reply_t reply;
  unsigned reply_length = 0;
  int result = gui_call(GUI_RPC_CREATE_WINDOW, request_buffer,
                        sizeof(*request) + title_length, &reply, sizeof(reply),
                        &reply_length);
  if (result == RPC_OK && reply_length == sizeof(reply) && reply.window_id != 0) {
    window->id = reply.window_id;
    return window;
  }

  gui_lock();
  gui_window_remove(window);
  gui_unlock();
  free(window);
  return NULL;
}

int window_get_event(window_t window) {
  return window == NULL ? -1 : gui_event_queue_pop(&window->shared->events);
}

void close_window(window_t window) {
  if (window == NULL) {
    return;
  }

  gui_rpc_window_request_t request = {.window_id = window->id};
  /* Drop the client mapping before the server releases its backing store. */
  shared_memory_unmap((void *)(uintptr_t)window->mapping, window->mapping_size);
  gui_call(GUI_RPC_CLOSE_WINDOW, &request, sizeof(request), NULL, 0, NULL);

  gui_lock();
  gui_window_remove(window);
  gui_unlock();
  free(window);
}

void draw_px(window_t window, int x, int y, int color) {
  if (window == NULL || (unsigned)x >= window->width ||
      (unsigned)y >= window->height) {
    return;
  }
  uint32_t *framebuffer =
      (uint32_t *)((unsigned char *)window->shared + sizeof(*window->shared));
  framebuffer[(unsigned)y * window->width + (unsigned)x] = (uint32_t)color;
}

void window_refresh(window_t window, int first, int last) {
  if (window == NULL || gui_connect() != RPC_OK) {
    return;
  }
  gui_rect_t rect = {
      .x0 = (int16_t)((uint32_t)first >> 16),
      .y0 = (int16_t)first,
      .x1 = (int16_t)((uint32_t)last >> 16),
      .y1 = (int16_t)last,
  };
  if (rect.x0 < 0) {
    rect.x0 = 0;
  }
  if (rect.y0 < 0) {
    rect.y0 = 0;
  }
  if (rect.x1 > (int32_t)window->width) {
    rect.x1 = window->width;
  }
  if (rect.y1 > (int32_t)window->height) {
    rect.y1 = window->height;
  }
  if (rect.x0 >= rect.x1 || rect.y0 >= rect.y1 ||
      !gui_damage_add(&window->shared->damage, &rect)) {
    return;
  }

  gui_rpc_window_request_t request = {.window_id = window->id};
  int result = rpc_notify(&gui_endpoint, GUI_RPC_REFRESH_WINDOW, &request,
                          sizeof(request));
  if (result != RPC_OK) {
    gui_damage_unsignal(&window->shared->damage);
    gui_connection_failed(result);
  }
}

void *window_get_fb(window_t window) {
  return window == NULL ? NULL
                        : (unsigned char *)window->shared +
                              sizeof(*window->shared);
}

static void window_keyboard_set(window_t window, unsigned opcode) {
  if (window == NULL) {
    return;
  }
  gui_rpc_window_request_t request = {.window_id = window->id};
  gui_call(opcode, &request, sizeof(request), NULL, 0, NULL);
}

void window_start_recv_keyboard(window_t window) {
  window_keyboard_set(window, GUI_RPC_START_KEYBOARD);
}

void window_stop_recv_keyboard(window_t window) {
  window_keyboard_set(window, GUI_RPC_STOP_KEYBOARD);
}

int window_get_key_press_data(window_t window) {
  return window == NULL ? -1
                        : gui_event_queue_pop(&window->shared->key_press);
}

int window_get_key_press_status(window_t window) {
  return window == NULL ? 0
                        : (int)gui_event_queue_count(&window->shared->key_press);
}

int window_get_key_up_data(window_t window) {
  return window == NULL ? -1 : gui_event_queue_pop(&window->shared->key_up);
}

int window_get_key_up_status(window_t window) {
  return window == NULL ? 0
                        : (int)gui_event_queue_count(&window->shared->key_up);
}
