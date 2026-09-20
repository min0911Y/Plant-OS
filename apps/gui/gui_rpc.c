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
  uint32_t front;
  uint32_t owner_tid;
  uint32_t owner_generation;
  rpc_endpoint_t event_target;
} gui_remote_window_t;

extern desktop_t *desktop0;

static gui_remote_window_t *gui_remote_windows;
static uint32_t gui_next_window_id = 1;

/* Called under TaskLock. Publish coherent snapshots without per-frame RPC.
 * Find the topmost window once; the mouse layer itself is not a window. */
void gui_update_window_states(desktop_t *desktop) {
  gmouse_t *mouse = desktop->mouse;
  gui_mouse_sync(mouse);
  window_t *hovered = NULL;
  if (mouse) {
    for (int height = desktop->shtctl->top; height >= 0; height--) {
      struct SHEET *sheet = desktop->shtctl->sheets[height];
      if (sheet->wnd && Collision(sheet->vx0, sheet->vy0, sheet->bxsize,
                                  sheet->bysize, mouse->x, mouse->y)) {
        hovered = sheet->wnd;
        break;
      }
    }
  }
  for (gui_remote_window_t *remote = gui_remote_windows; remote;
       remote = remote->next) {
    window_t *window = remote->window;
    if (window->desktop != desktop)
      continue;
    gui_window_state_t state = {
        .x = window->x,
        .y = window->y,
        .cursor_x = mouse ? mouse->x - window->x : 0,
        .cursor_y = mouse ? mouse->y - window->y : 0,
        .width = window->xsize,
        .height = window->ysize,
        .requested_width = window->requested_width,
        .requested_height = window->requested_height,
        .buttons = window->pointer_buttons,
        .flags =
            (window->resizable ? GUI_WINDOW_RESIZABLE : 0) |
            (mouse && mouse->captured == window &&
                     (mouse->mode & (GUI_MOUSE_CAPTURE | GUI_MOUSE_RELATIVE |
                                     GUI_MOUSE_CONFINED))
                 ? GUI_WINDOW_CAPTURED
                 : 0) |
            (window->using1 ? GUI_WINDOW_VISIBLE : 0) |
            (desktop->focused_window == window ? GUI_WINDOW_FOCUSED : 0) |
            (hovered == window ? GUI_WINDOW_HOVERED : 0),
    };
    gui_window_shared_t *shared = window->shared;
    if (!memcmp(&shared->state, &state, sizeof(state)))
      continue;
    bool notify = state.x != shared->state.x || state.y != shared->state.y ||
                  state.flags != shared->state.flags ||
                  state.buttons != shared->state.buttons ||
                  state.width != shared->state.width ||
                  state.height != shared->state.height ||
                  state.requested_width != shared->state.requested_width ||
                  state.requested_height != shared->state.requested_height;
    uint32_t sequence =
        __atomic_load_n(&shared->state_sequence, __ATOMIC_RELAXED) & ~1u;
    __atomic_store_n(&shared->state_sequence, sequence + 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&shared->state.x, state.x, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.y, state.y, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.cursor_x, state.cursor_x, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.cursor_y, state.cursor_y, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.flags, state.flags, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.width, state.width, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.height, state.height, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.requested_width, state.requested_width,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.requested_height, state.requested_height,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state.buttons, state.buttons, __ATOMIC_RELAXED);
    __atomic_store_n(&shared->state_sequence, sequence + 2, __ATOMIC_RELEASE);
    if (notify && remote->event_target.tid)
      rpc_notify(&remote->event_target, GUI_RPC_EVENT_READY, NULL, 0);
  }
}

void gui_wake_window(window_t *window) {
  for (gui_remote_window_t *remote = gui_remote_windows; remote;
       remote = remote->next) {
    if (remote->window != window)
      continue;
    if (remote->event_target.tid)
      rpc_notify(&remote->event_target, GUI_RPC_EVENT_READY, NULL, 0);
    return;
  }
}

static void gui_event_close(window_t *window) {
  gui_mouse_release(window);
  gui_event_t event = {.type = GUI_EVENT_CLOSE_WINDOW};
  gui_pointer_push(&window->shared->events, &event);
  gui_wake_window(window);
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
  gui_window_shared_t *shared = remote->window->shared;
  destroy_window(remote->window);
  vm_unmap(shared, remote->mapping_size);
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
      request->height == 0 ||
      (request->flags &
       ~(GUI_CREATE_HIDDEN | GUI_CREATE_UNFOCUSED | GUI_CREATE_RESIZABLE))) {
    return RPC_ERR_INVAL;
  }

  uint32_t mapping_size;
  if (!gui_window_shared_mapping_size(request->width, request->height,
                                      &mapping_size) ||
      request->width > INT16_MAX || request->height > INT16_MAX ||
      request->x < INT16_MIN || request->x > INT16_MAX ||
      request->y < INT16_MIN || request->y > INT16_MAX ||
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
                    call->caller_tid, shared);
  if (window == NULL) {
    TaskUnlock();
    vm_unmap(shared, mapping_size);
    free(remote);
    return RPC_ERR_NOMEM;
  }

  memcpy(gui_window_pixels(shared, window->xsize, window->ysize, 0),
         window->vram, (size_t)window->xsize * window->ysize * sizeof(vram_t));
  shared->events.read = shared->events.write = 0;
  gui_event_queue_init(&shared->key_press);
  gui_event_queue_init(&shared->key_up);
  gui_damage_init(&shared->damage);
  window->keyboard_events = false;
  window->resizable = (request->flags & GUI_CREATE_RESIZABLE) != 0;
  window->close = gui_event_close;
  window->x = request->x;
  window->y = request->y;
  sheet_slide(window->sht, window->x, window->y);

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
  remote->front = 1;
  remote->owner_tid = call->caller_tid;
  remote->owner_generation = call->caller_generation;
  remote->event_target = (rpc_endpoint_t){0};
  remote->next = gui_remote_windows;
  gui_remote_windows = remote;
  if (!(request->flags & GUI_CREATE_HIDDEN))
    window_show(window, !(request->flags & GUI_CREATE_UNFOCUSED));
  else
    gui_update_window_states(desktop0);
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

static void gui_copy_rect(vram_t *target, const vram_t *source, size_t stride,
                           int x0, int y0, int x1, int y1) {
  if (x0 >= x1 || y0 >= y1)
    return;
  size_t bytes = (size_t)(x1 - x0) * sizeof(*source);
  for (int y = y0; y < y1; y++) {
    size_t offset = (size_t)y * stride + x0;
    memcpy(target + offset, source + offset, bytes);
  }
}

static int gui_refresh_window(rpc_call_t *call) {
  bool exchange = call->opcode == GUI_RPC_PRESENT_FRAME;
  if (call->arg_len != (exchange ? sizeof(gui_rpc_frame_request_t)
                                : sizeof(gui_rpc_window_request_t)))
    return RPC_ERR_INVAL;
  if (exchange && call->ret_cap < sizeof(gui_rpc_frame_reply_t))
    return RPC_ERR_TOOBIG;

  const gui_rpc_window_request_t *request = call->arg;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (!remote ||
      (exchange && ((const gui_rpc_frame_request_t *)call->arg)->buffer !=
                       (remote->front ^ 1))) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }

  window_t *window = remote->window;
  gui_rect_t rect = {0};
  gui_damage_take(&window->shared->damage, &rect);
  if (rect.x0 < 0)
    rect.x0 = 0;
  if (rect.y0 < 0)
    rect.y0 = 0;
  if (rect.x1 > window->xsize)
    rect.x1 = window->xsize;
  if (rect.y1 > window->ysize)
    rect.y1 = window->ysize;
  bool dirty = rect.x0 < rect.x1 && rect.y0 < rect.y1;
  vram_t *back = gui_window_pixels(window->shared, window->xsize,
                                   window->ysize, remote->front ^ 1);
  if (exchange) {
    if (dirty) {
      /* Preserve everything outside damage, including current decorations.
       * The client completely overwrites damage before handing off its plane. */
      gui_copy_rect(back, window->vram, window->xsize, 0, 0, window->xsize, rect.y0);
      gui_copy_rect(back, window->vram, window->xsize, 0, rect.y1,
                    window->xsize, window->ysize);
      gui_copy_rect(back, window->vram, window->xsize, 0, rect.y0, rect.x0, rect.y1);
      gui_copy_rect(back, window->vram, window->xsize, rect.x1, rect.y0,
                    window->xsize, rect.y1);
    } else {
      gui_copy_rect(back, window->vram, window->xsize, 0, 0,
                    window->xsize, window->ysize);
    }
    remote->front ^= 1;
    window->vram = back;
    window->sht->buf = back;
    gui_rpc_frame_reply_t reply = {.buffer = remote->front ^ 1};
    memcpy(call->ret, &reply, sizeof(reply));
    call->ret_len = sizeof(reply);
  } else if (dirty) {
    gui_copy_rect(window->vram, back, window->xsize,
                  rect.x0, rect.y0, rect.x1, rect.y1);
  }
  if (dirty)
    sheet_refresh(window->sht, rect.x0, rect.y0, rect.x1, rect.y1);
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

static int gui_set_decoration(rpc_call_t *call) {
  if (call->arg_len < sizeof(gui_rpc_window_request_t))
    return RPC_ERR_INVAL;
  const gui_rpc_window_request_t *request = call->arg;
  const void *data = (const char *)call->arg + sizeof(*request);
  size_t length = call->arg_len - sizeof(*request);
  bool icon = call->opcode == GUI_RPC_SET_ICON;
  if (icon) {
    if (length && length != GUI_ICON_SIZE * GUI_ICON_SIZE * sizeof(uint32_t))
      return RPC_ERR_INVAL;
  } else {
    const char *title = data;
    if (!length || length > GUI_TITLE_MAX + 1 || title[length - 1] ||
        strlen(title) != length - 1)
      return RPC_ERR_INVAL;
  }
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  int result = -1;
  if (remote)
    result = icon ? window_set_icon(remote->window, length ? data : NULL)
                  : window_set_title(remote->window, data);
  if (result == 0) {
    window_t *window = remote->window;
    size_t rows = window->ysize < 20 ? window->ysize : 20;
    memcpy(gui_window_pixels(window->shared, window->xsize, window->ysize,
                              remote->front ^ 1),
           window->vram, rows * window->xsize * sizeof(vram_t));
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
  task_info_t *tasks = NULL;
  size_t count = 0;
  if (request->enabled && task_list(&tasks, &count) != 0)
    return RPC_ERR_NOMEM;
  const task_info_t *owner = task_snapshot_find(tasks, count, call->caller_tid);
  const task_info_t *target = task_snapshot_find(tasks, count, request->tid);
  bool valid =
      !request->enabled ||
      (owner && target && owner->generation == call->caller_generation &&
       owner->tgid == target->tgid &&
       target->generation == request->generation &&
       target->state != TASK_INFO_ZOMBIE);
  free(tasks);
  if (!valid)
    return RPC_ERR_INVAL;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (remote) {
    remote->event_target = (rpc_endpoint_t){request->enabled ? request->tid : 0,
                                            request->generation};
    gui_wake_window(remote->window);
  }
  TaskUnlock();
  return remote ? RPC_OK : RPC_ERR_INVAL;
}

static int gui_window_control(rpc_call_t *call) {
  if (call->arg_len != sizeof(gui_rpc_window_control_t))
    return RPC_ERR_INVAL;
  const gui_rpc_window_control_t *request = call->arg;
  if (request->operation > GUI_WINDOW_WARP_POINTER || request->x < INT16_MIN ||
      request->x > INT16_MAX || request->y < INT16_MIN ||
      request->y > INT16_MAX)
    return RPC_ERR_INVAL;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (!remote) {
    TaskUnlock();
    return RPC_ERR_INVAL;
  }
  window_t *window = remote->window;
  switch (request->operation) {
  case GUI_WINDOW_WARP_POINTER: {
    int result = gui_mouse_warp(window, request->x, request->y);
    TaskUnlock();
    return result == 0 ? RPC_OK : RPC_ERR_INVAL;
  }
  case GUI_WINDOW_MOVE:
    window->x = request->x;
    window->y = request->y;
    sheet_slide(window->sht, window->x, window->y);
    gui_update_window_states(window->desktop);
    break;
  case GUI_WINDOW_SHOW:
    if (!window->using1)
      window_show(window, request->x != 0);
    break;
  case GUI_WINDOW_HIDE:
    window->hide(window);
    break;
  case GUI_WINDOW_FOCUS:
    window_focus(window);
    break;
  case GUI_WINDOW_SET_RESIZABLE:
    window->resizable = request->x != 0;
    if (!window->resizable) {
      window->requested_width = window->xsize;
      window->requested_height = window->ysize;
      if (window->desktop->mouse && window->desktop->mouse->target == window &&
          window->desktop->mouse->gesture == GUI_GESTURE_RESIZE)
        gui_mouse_release(window);
    }
    gui_update_window_states(window->desktop);
    break;
  case GUI_WINDOW_MOUSE_MODE: {
    gmouse_t *mouse = window->desktop->mouse;
    if (!mouse ||
        (request->x & ~(GUI_MOUSE_CAPTURE | GUI_MOUSE_RELATIVE |
                        GUI_MOUSE_HIDDEN | GUI_MOUSE_CONFINED)) ||
        (request->x &&
         (!window->using1 || window->desktop->focused_window != window))) {
      TaskUnlock();
      return RPC_ERR_INVAL;
    }
    if (mouse->captured == window || request->x) {
      if (mouse->captured && mouse->captured != window)
        gui_mouse_release(mouse->captured);
      if (mouse->target && mouse->target != window)
        gui_mouse_release(mouse->target);
      mouse->captured = request->x ? window : NULL;
      mouse->mode = request->x;
      mouse->target = mouse->buttons ? window : NULL;
      mouse->gesture = mouse->buttons ? GUI_GESTURE_CLIENT : GUI_GESTURE_NONE;
      gui_update_window_states(window->desktop);
    }
    break;
  }
  }
  TaskUnlock();
  return RPC_OK;
}

static int gui_resize_window(rpc_call_t *call) {
  if (call->arg_len != sizeof(gui_rpc_resize_request_t))
    return RPC_ERR_INVAL;
  const gui_rpc_resize_request_t *request = call->arg;
  uint32_t size;
  if (request->width < 40 || request->height < 29 ||
      request->width > INT16_MAX || request->height > INT16_MAX ||
      !gui_window_shared_mapping_size(request->width, request->height, &size) ||
      size > GUI_SHARED_REGION_END - GUI_SHARED_REGION_START ||
      request->client_mapping < GUI_SHARED_REGION_START ||
      request->client_mapping > GUI_SHARED_REGION_END - size ||
      (request->client_mapping & (VM_PAGE_SIZE - 1)))
    return RPC_ERR_INVAL;
  gui_window_shared_t *shared = vm_map(NULL, size);
  if (!shared)
    return RPC_ERR_NOMEM;
  TaskLock();
  gui_remote_window_t *remote = gui_remote_find(call, request->window_id);
  if (!remote) {
    TaskUnlock();
    vm_unmap(shared, size);
    return RPC_ERR_INVAL;
  }
  window_t *window = remote->window;
  /* Prepare the complete replacement before publishing any geometry. The
   * caller is blocked and retains its old mapping until the reply arrives. */
  window_t replacement = *window;
  replacement.xsize = request->width;
  replacement.ysize = request->height;
  replacement.vram =
      gui_window_pixels(shared, request->width, request->height, 1);
  window_draw_frame(&replacement);
  int width =
      window->xsize < replacement.xsize ? window->xsize : replacement.xsize;
  int height =
      window->ysize < replacement.ysize ? window->ysize : replacement.ysize;
  for (int y = 24; y < height - 4; y++)
    memcpy(replacement.vram + (size_t)y * replacement.xsize + 4,
           window->vram + (size_t)y * window->xsize + 4,
           (size_t)(width - 8) * sizeof(vram_t));
  memcpy(gui_window_pixels(shared, request->width, request->height, 0),
         replacement.vram,
         (size_t)request->width * request->height * sizeof(vram_t));
  *shared = *window->shared;
  gui_damage_init(&shared->damage);
  if (shared_memory_map_to(call->caller_tid, call->caller_generation, shared,
                           (void *)request->client_mapping, size) != 0) {
    TaskUnlock();
    vm_unmap(shared, size);
    return RPC_ERR_NOMEM;
  }
  gui_window_shared_t *old_shared = window->shared;
  uint32_t old_size = remote->mapping_size;
  window->shared = shared;
  window->vram = replacement.vram;
  /* A newer drag report may arrive while the client is waiting for this RPC.
   * Keep that request pending instead of losing the final release position. */
  if (window->requested_width == (unsigned)window->xsize &&
      window->requested_height == (unsigned)window->ysize) {
    window->requested_width = request->width;
    window->requested_height = request->height;
  }
  window->xsize = request->width;
  window->ysize = request->height;
  remote->mapping_size = size;
  remote->front = 1;
  sheet_setbuf(window->sht, window->vram, window->xsize, window->ysize, -1);
  gui_update_window_states(window->desktop);
  TaskUnlock();
  vm_unmap(old_shared, old_size);
  return RPC_OK;
}

static const rpc_handler_t gui_handlers[GUI_RPC_COUNT] = {
    [GUI_RPC_CREATE_WINDOW] = gui_create_window,
    [GUI_RPC_CLOSE_WINDOW] = gui_close_window,
    [GUI_RPC_REFRESH_WINDOW] = gui_refresh_window,
    [GUI_RPC_PRESENT_FRAME] = gui_refresh_window,
    [GUI_RPC_START_KEYBOARD] = gui_start_keyboard,
    [GUI_RPC_STOP_KEYBOARD] = gui_stop_keyboard,
    [GUI_RPC_SET_TITLE] = gui_set_decoration,
    [GUI_RPC_SET_ICON] = gui_set_decoration,
    [GUI_RPC_EVENT_NOTIFICATIONS] = gui_event_notifications,
    [GUI_RPC_WINDOW_CONTROL] = gui_window_control,
    [GUI_RPC_RESIZE_WINDOW] = gui_resize_window,
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
