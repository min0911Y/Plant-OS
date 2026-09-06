#include "gui.h"
#include <limits.h>
#include <rpc.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>
#include <vm.h>

typedef struct gui_remote_window {
  struct gui_remote_window *next;
  window_t *window;
  uint32_t mapping_size;
  uint32_t id;
  uint32_t owner_tid;
  uint32_t owner_generation;
  bool event_notifications;
} gui_remote_window_t;

extern desktop_t *desktop0;

static gui_remote_window_t *gui_remote_windows;
static uint32_t gui_next_window_id = 1;

void gui_wake_window(window_t *window) {
  for (gui_remote_window_t *remote = gui_remote_windows; remote;
       remote = remote->next) {
    if (remote->window != window)
      continue;
    if (remote->event_notifications) {
      rpc_endpoint_t owner = {remote->owner_tid, remote->owner_generation};
      rpc_notify(&owner, GUI_RPC_EVENT_READY, NULL, 0);
    }
    return;
  }
}

static uint32_t gui_pack_xy(int x, int y) {
  return (uint32_t)(uint16_t)x << 16 | (uint16_t)y;
}

static void gui_event_stay(window_t *window, gmouse_t *gmouse) {
  if (window->shared != NULL) {
    gui_event_queue_push2(&window->shared->events, GUI_EVENT_MOUSE_STAY,
                          gui_pack_xy(gmouse->x - window->x,
                                      gmouse->y - window->y));
    gui_wake_window(window);
  }
}

static void gui_event_left(window_t *window, gmouse_t *gmouse) {
  if (window->shared != NULL) {
    gui_event_queue_push2(&window->shared->events, GUI_EVENT_MOUSE_CLICK_LEFT,
                          gui_pack_xy(gmouse->x - window->x,
                                      gmouse->y - window->y));
    gui_wake_window(window);
  }
}

static void gui_event_right(window_t *window, gmouse_t *gmouse) {
  if (window->shared != NULL) {
    gui_event_queue_push2(&window->shared->events,
                          GUI_EVENT_MOUSE_CLICK_RIGHT,
                          gui_pack_xy(gmouse->x - window->x,
                                      gmouse->y - window->y));
    gui_wake_window(window);
  }
}

static void gui_event_close(window_t *window) {
  if (window->shared != NULL) {
    gui_event_queue_push(&window->shared->events, GUI_EVENT_CLOSE_WINDOW);
    gui_wake_window(window);
  }
}

static void gui_event_wheel(window_t *window, gmouse_t *gmouse,
                            unsigned value) {
  if (window->shared != NULL) {
    gui_event_queue_push3(&window->shared->events, GUI_EVENT_MOUSE_WHEEL,
                          gui_pack_xy(gmouse->x - window->x,
                                      gmouse->y - window->y),
                          value);
    gui_wake_window(window);
  }
}

static gui_remote_window_t *gui_remote_find(const rpc_call_t *call,
                                             uint32_t id) {
  for (gui_remote_window_t *remote = gui_remote_windows; remote != NULL;
       remote = remote->next) {
    if (remote->id == id && remote->owner_tid == call->caller_tid &&
        remote->owner_generation == call->caller_generation) {
      return remote;
    }
  }
  return NULL;
}

static void gui_remote_destroy(gui_remote_window_t *remote) {
  vm_unmap(remote->window->shared, remote->mapping_size);
  destroy_window(remote->window);
  free(remote);
}

void gui_rpc_reap_windows(const task_info_t *tasks, size_t count) {
  for (gui_remote_window_t **link = &gui_remote_windows; *link;) {
    gui_remote_window_t *remote = *link;
    const task_info_t *owner =
        task_snapshot_find(tasks, count, remote->owner_tid);
    if (owner && owner->generation == remote->owner_generation &&
        owner->state != TASK_INFO_ZOMBIE) {
      link = &remote->next;
    } else {
      *link = remote->next;
      gui_remote_destroy(remote);
    }
  }
}

static int gui_create_window(rpc_call_t *call) {
  if (desktop0 == NULL || call->arg_len < sizeof(gui_rpc_create_request_t)) {
    return RPC_ERR_INVAL;
  }
  if (call->ret_cap < sizeof(gui_rpc_create_reply_t)) {
    return RPC_ERR_TOOBIG;
  }

  const gui_rpc_create_request_t *request = call->arg;
  unsigned title_length = request->title_length;
  if (title_length > GUI_TITLE_MAX ||
      title_length != call->arg_len - sizeof(*request) || request->width == 0 ||
      request->height == 0) {
    return RPC_ERR_INVAL;
  }

  uint32_t mapping_size;
  if (!gui_window_shared_mapping_size(request->width, request->height,
                                      &mapping_size) ||
      request->width > INT_MAX || request->height > INT_MAX ||
      request->x > INT_MAX - (int)request->width ||
      request->y > INT_MAX - (int)request->height ||
      mapping_size > GUI_SHARED_REGION_END - GUI_SHARED_REGION_START ||
      request->client_mapping < GUI_SHARED_REGION_START ||
      request->client_mapping > GUI_SHARED_REGION_END - mapping_size) {
    return RPC_ERR_INVAL;
  }

  char title[GUI_TITLE_MAX + 1];
  memcpy(title, (const char *)call->arg + sizeof(*request), title_length);
  title[title_length] = '\0';

  gui_remote_window_t *remote = malloc(sizeof(*remote));
  gui_window_shared_t *shared = vm_map(NULL, mapping_size);
  if (remote == NULL || shared == NULL) {
    free(remote);
    if (shared)
      vm_unmap(shared, mapping_size);
    return RPC_ERR_NOMEM;
  }

  TaskLock();
  window_t *window =
      create_window(desktop0, title, (int)request->width, (int)request->height,
                    call->caller_tid);
  if (window == NULL) {
    TaskUnlock();
    vm_unmap(shared, mapping_size);
    free(remote);
    return RPC_ERR_NOMEM;
  }

  /* The shared pixels are the client's render buffer. Only committed damage
     reaches the private sheet used for exposure, mouse and window redraws. */
  memcpy((unsigned char *)shared + sizeof(*shared), window->vram,
         (size_t)window->xsize * window->ysize * sizeof(vram_t));
  gui_event_queue_init(&shared->events);
  gui_event_queue_init(&shared->key_press);
  gui_event_queue_init(&shared->key_up);
  gui_damage_init(&shared->damage);
  window->shared = shared;
  window->keyboard_events = false;
  window->handle_stay = gui_event_stay;
  window->handle_client_left = gui_event_left;
  window->handle_right = gui_event_right;
  window->handle_mouse_wheel = gui_event_wheel;
  window->close = gui_event_close;
  window->display(window, request->x, request->y);

  if (shared_memory_map_to(call->caller_tid, call->caller_generation, shared,
                           (void *)(uintptr_t)request->client_mapping,
                           mapping_size) != 0) {
    destroy_window(window);
    TaskUnlock();
    vm_unmap(shared, mapping_size);
    free(remote);
    return RPC_ERR_TRANSPORT;
  }

  uint32_t id = gui_next_window_id++;
  if (gui_next_window_id == 0) {
    gui_next_window_id = 1;
  }
  remote->window = window;
  remote->mapping_size = mapping_size;
  remote->id = id;
  remote->owner_tid = call->caller_tid;
  remote->owner_generation = call->caller_generation;
  remote->event_notifications = false;
  remote->next = gui_remote_windows;
  gui_remote_windows = remote;
  TaskUnlock();

  gui_rpc_create_reply_t reply = {.window_id = id};
  memcpy(call->ret, &reply, sizeof(reply));
  call->ret_len = sizeof(reply);
  return RPC_OK;
}

static int gui_close_window(rpc_call_t *call) {
  if (call->arg_len != sizeof(gui_rpc_window_request_t)) {
    return RPC_ERR_INVAL;
  }

  const gui_rpc_window_request_t *request = call->arg;
  TaskLock();
  gui_remote_window_t **link = &gui_remote_windows;
  while (*link != NULL &&
         ((*link)->id != request->window_id ||
          (*link)->owner_tid != call->caller_tid ||
          (*link)->owner_generation != call->caller_generation)) {
    link = &(*link)->next;
  }
  if (*link == NULL) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }
  gui_remote_window_t *remote = *link;
  *link = remote->next;
  gui_remote_destroy(remote);
  TaskUnlock();
  return RPC_OK;
}

static int gui_refresh_window(rpc_call_t *call) {
  if (call->arg_len != sizeof(gui_rpc_window_request_t)) {
    return RPC_ERR_INVAL;
  }

  const gui_rpc_window_request_t *request = call->arg;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (remote == NULL) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }

  gui_rect_t rect;
  if (gui_damage_take(&remote->window->shared->damage, &rect)) {
    if (rect.x0 < 0) {
      rect.x0 = 0;
    }
    if (rect.y0 < 0) {
      rect.y0 = 0;
    }
    if (rect.x1 > remote->window->xsize) {
      rect.x1 = remote->window->xsize;
    }
    if (rect.y1 > remote->window->ysize) {
      rect.y1 = remote->window->ysize;
    }
    if (rect.x0 < rect.x1 && rect.y0 < rect.y1) {
      window_t *window = remote->window;
      const vram_t *source = (const vram_t *)(window->shared + 1);
      size_t row_bytes = (size_t)(rect.x1 - rect.x0) * sizeof(*source);
      for (int y = rect.y0; y < rect.y1; y++) {
        size_t offset = (size_t)y * window->xsize + rect.x0;
        memcpy(window->vram + offset, source + offset, row_bytes);
      }
      sheet_refresh(window->sht, rect.x0, rect.y0, rect.x1, rect.y1);
    }
  }
  TaskUnlock();
  return RPC_OK;
}

static int gui_keyboard_set(rpc_call_t *call, int enabled) {
  if (call->arg_len != sizeof(gui_rpc_window_request_t)) {
    return RPC_ERR_INVAL;
  }

  const gui_rpc_window_request_t *request = call->arg;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (remote == NULL) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }
  remote->window->keyboard_events = enabled != 0;
  gui_event_queue_init(&remote->window->shared->key_press);
  gui_event_queue_init(&remote->window->shared->key_up);
  TaskUnlock();
  return RPC_OK;
}

static int gui_start_keyboard(rpc_call_t *call) {
  return gui_keyboard_set(call, 1);
}

static int gui_stop_keyboard(rpc_call_t *call) {
  return gui_keyboard_set(call, 0);
}

static int gui_set_title(rpc_call_t *call) {
  if (call->arg_len <= sizeof(gui_rpc_window_request_t) ||
      call->arg_len > sizeof(gui_rpc_window_request_t) + GUI_TITLE_MAX + 1)
    return RPC_ERR_INVAL;
  const gui_rpc_window_request_t *request = call->arg;
  const char *title = (const char *)call->arg + sizeof(*request);
  size_t length = call->arg_len - sizeof(*request);
  if (title[length - 1] != 0 || strlen(title) != length - 1)
    return RPC_ERR_INVAL;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  int result = remote ? window_set_title(remote->window, title) : -1;
  if (result == 0) {
    window_t *window = remote->window;
    size_t rows = window->ysize < 20 ? window->ysize : 20;
    memcpy(window->shared + 1, window->vram,
           rows * window->xsize * sizeof(vram_t));
  }
  TaskUnlock();
  return result == 0 ? RPC_OK : RPC_ERR_INVAL;
}

static int gui_event_notifications(rpc_call_t *call) {
  if (call->arg_len != sizeof(gui_rpc_event_notifications_t))
    return RPC_ERR_INVAL;
  const gui_rpc_event_notifications_t *request = call->arg;
  if (request->enabled > 1)
    return RPC_ERR_INVAL;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (remote)
    remote->event_notifications = request->enabled;
  TaskUnlock();
  return remote ? RPC_OK : RPC_ERR_INVAL;
}

static const rpc_handler_t gui_handlers[GUI_RPC_COUNT] = {
    [GUI_RPC_CREATE_WINDOW] = gui_create_window,
    [GUI_RPC_CLOSE_WINDOW] = gui_close_window,
    [GUI_RPC_REFRESH_WINDOW] = gui_refresh_window,
    [GUI_RPC_START_KEYBOARD] = gui_start_keyboard,
    [GUI_RPC_STOP_KEYBOARD] = gui_stop_keyboard,
    [GUI_RPC_SET_TITLE] = gui_set_title,
    [GUI_RPC_EVENT_NOTIFICATIONS] = gui_event_notifications,
};

int gui_rpc_service_start(void) {
  int result = rpc_service_create(GUI_SERVICE_NAME);
  if (result != RPC_OK) {
    return result;
  }
  for (unsigned opcode = 1; opcode < GUI_RPC_COUNT; opcode++) {
    if (gui_handlers[opcode] == NULL) {
      continue;
    }
    result = rpc_register_handler(opcode, gui_handlers[opcode]);
    if (result != RPC_OK) {
      rpc_service_destroy(GUI_SERVICE_NAME);
      return result;
    }
  }
  return RPC_OK;
}
