#ifndef PLOS_GUI_RPC_H
#define PLOS_GUI_RPC_H

#include <ctypes.h>

#define GUI_SERVICE_NAME "gui"
#define GUI_TITLE_MAX 255u

/* This range is reserved for client-side GUI shared mappings. */
#if defined(PLANT_ARCH_X86_64)
#define GUI_SHARED_REGION_START ((uintptr_t)0x400000100000ull)
#else
#define GUI_SHARED_REGION_START ((uintptr_t)0xf0100000u)
#endif
#define GUI_SHARED_REGION_END (GUI_SHARED_REGION_START + 0xf00000u)

enum gui_window_flag {
  GUI_WINDOW_VISIBLE = 1u,
  GUI_WINDOW_FOCUSED = 2u,
  GUI_WINDOW_HOVERED = 4u,
  GUI_WINDOW_CAPTURED = 8u,
  GUI_WINDOW_RESIZABLE = 16u,
};

typedef struct {
  int32_t x, y;
  int32_t cursor_x, cursor_y;
  uint32_t flags;
  uint32_t width, height;
  uint32_t requested_width, requested_height;
  uint32_t buttons;
} gui_window_state_t;

#define GUI_EVENT_QUEUE_CAPACITY 64u

enum gui_event {
  GUI_EVENT_POINTER = 1,
  GUI_EVENT_CLOSE_WINDOW,
};

/* One atomic record includes motion, all buttons and signed wheel steps. */
typedef struct {
  uint32_t type;
  int32_t x, y, dx, dy, wheel;
  uint32_t buttons;
  uint32_t relative;
} gui_event_t;

typedef struct {
  uint32_t read, write;
  gui_event_t data[GUI_EVENT_QUEUE_CAPACITY];
} gui_pointer_queue_t;

static inline bool gui_pointer_push(gui_pointer_queue_t *queue,
                                    const gui_event_t *event) {
  uint32_t write = queue->write;
  uint32_t read = __atomic_load_n(&queue->read, __ATOMIC_ACQUIRE);
  if (write - read >= GUI_EVENT_QUEUE_CAPACITY)
    return false;
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = *event;
  __atomic_store_n(&queue->write, write + 1, __ATOMIC_RELEASE);
  return true;
}

static inline bool gui_pointer_pop(gui_pointer_queue_t *queue,
                                   gui_event_t *event) {
  uint32_t read = queue->read;
  if (read == __atomic_load_n(&queue->write, __ATOMIC_ACQUIRE))
    return false;
  *event = queue->data[read % GUI_EVENT_QUEUE_CAPACITY];
  __atomic_store_n(&queue->read, read + 1, __ATOMIC_RELEASE);
  return true;
}

typedef struct {
  volatile uint32_t read;
  volatile uint32_t write;
  uint32_t data[GUI_EVENT_QUEUE_CAPACITY];
} gui_event_queue_t;

typedef struct {
  int32_t x0;
  int32_t y0;
  int32_t x1;
  int32_t y1;
} gui_rect_t;

typedef struct {
  volatile uint32_t lock;
  volatile uint32_t queued;
  volatile uint32_t dirty;
  gui_rect_t rect;
} gui_damage_t;

typedef struct {
  uint32_t state_sequence;
  gui_window_state_t state;
  gui_pointer_queue_t events;
  gui_event_queue_t key_press;
  gui_event_queue_t key_up;
  gui_damage_t damage;
} gui_window_shared_t;

static inline void gui_damage_lock(gui_damage_t *damage) {
  while (__atomic_exchange_n(&damage->lock, 1, __ATOMIC_ACQUIRE)) {
  }
}

static inline void gui_damage_unlock(gui_damage_t *damage) {
  __atomic_store_n(&damage->lock, 0, __ATOMIC_RELEASE);
}

static inline void gui_damage_init(gui_damage_t *damage) {
  damage->lock = 0;
  damage->queued = 0;
  damage->dirty = 0;
}

static inline bool gui_damage_add(gui_damage_t *damage,
                                  const gui_rect_t *rect) {
  gui_damage_lock(damage);
  if (!damage->dirty) {
    damage->rect = *rect;
    damage->dirty = 1;
  } else {
    if (rect->x0 < damage->rect.x0) {
      damage->rect.x0 = rect->x0;
    }
    if (rect->y0 < damage->rect.y0) {
      damage->rect.y0 = rect->y0;
    }
    if (rect->x1 > damage->rect.x1) {
      damage->rect.x1 = rect->x1;
    }
    if (rect->y1 > damage->rect.y1) {
      damage->rect.y1 = rect->y1;
    }
  }
  bool signal = !damage->queued;
  damage->queued = 1;
  gui_damage_unlock(damage);
  return signal;
}

static inline void gui_damage_unsignal(gui_damage_t *damage) {
  gui_damage_lock(damage);
  damage->queued = 0;
  gui_damage_unlock(damage);
}

static inline bool gui_damage_take(gui_damage_t *damage, gui_rect_t *rect) {
  gui_damage_lock(damage);
  if (!damage->dirty) {
    damage->queued = 0;
    gui_damage_unlock(damage);
    return false;
  }
  *rect = damage->rect;
  damage->dirty = 0;
  damage->queued = 0;
  gui_damage_unlock(damage);
  return true;
}

static inline void gui_event_queue_init(gui_event_queue_t *queue) {
  queue->read = 0;
  queue->write = 0;
}

static inline int gui_event_queue_push(gui_event_queue_t *queue,
                                       uint32_t value) {
  uint32_t write = queue->write;
  uint32_t read = __atomic_load_n(&queue->read, __ATOMIC_ACQUIRE);
  if (write - read >= GUI_EVENT_QUEUE_CAPACITY) {
    return 0;
  }
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = value;
  __atomic_store_n(&queue->write, write + 1, __ATOMIC_RELEASE);
  return 1;
}

static inline int gui_event_queue_push2(gui_event_queue_t *queue,
                                        uint32_t first, uint32_t second) {
  uint32_t write = queue->write;
  uint32_t read = __atomic_load_n(&queue->read, __ATOMIC_ACQUIRE);
  if (write - read > GUI_EVENT_QUEUE_CAPACITY - 2) {
    return 0;
  }
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = first;
  queue->data[(write + 1) % GUI_EVENT_QUEUE_CAPACITY] = second;
  __atomic_store_n(&queue->write, write + 2, __ATOMIC_RELEASE);
  return 1;
}

static inline int gui_event_queue_pop(gui_event_queue_t *queue) {
  uint32_t read = queue->read;
  uint32_t write = __atomic_load_n(&queue->write, __ATOMIC_ACQUIRE);
  if (read == write) {
    return -1;
  }
  uint32_t value = queue->data[read % GUI_EVENT_QUEUE_CAPACITY];
  __atomic_store_n(&queue->read, read + 1, __ATOMIC_RELEASE);
  return (int)value;
}

static inline unsigned gui_event_queue_count(const gui_event_queue_t *queue) {
  uint32_t write = __atomic_load_n(&queue->write, __ATOMIC_ACQUIRE);
  uint32_t read = __atomic_load_n(&queue->read, __ATOMIC_ACQUIRE);
  return write - read;
}

enum gui_rpc_opcode {
  GUI_RPC_CREATE_WINDOW = 1,
  GUI_RPC_CLOSE_WINDOW,
  GUI_RPC_REFRESH_WINDOW,
  GUI_RPC_START_KEYBOARD,
  GUI_RPC_STOP_KEYBOARD,
  GUI_RPC_SET_TITLE,
  GUI_RPC_EVENT_NOTIFICATIONS,
  GUI_RPC_PRESENT_FRAME,
  GUI_RPC_WINDOW_CONTROL,
  GUI_RPC_RESIZE_WINDOW,
  GUI_RPC_COUNT,
};

#define GUI_RPC_EVENT_READY 0x475549u

static inline int gui_window_shared_mapping_size(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t *size_out) {
  if (size_out == NULL || width == 0 || height == 0 ||
      width > 0xffffffffu / height) {
    return 0;
  }
  uint32_t pixels = width * height;
  if (pixels > (0xffffffffu - sizeof(gui_window_shared_t)) / (2 * sizeof(uint32_t))) {
    return 0;
  }
  uint32_t size = sizeof(gui_window_shared_t) + 2 * pixels * sizeof(uint32_t);
  if (size > 0xffffffffu - 0xfffu) {
    return 0;
  }
  *size_out = (size + 0xfffu) & ~0xfffu;
  return 1;
}

/* Two complete planes: the compositor holds front, the client owns front ^ 1.
 * Only a successful frame RPC transfers ownership; resize replaces the mapping.
 */
static inline uint32_t *gui_window_pixels(gui_window_shared_t *shared,
                                           uint32_t width, uint32_t height,
                                           unsigned buffer) {
  return (uint32_t *)(shared + 1) + (size_t)width * height * buffer;
}

typedef struct {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  uintptr_t client_mapping;
  uint32_t title_length;
  uint32_t flags;
} gui_rpc_create_request_t;

typedef struct {
  uint32_t window_id;
} gui_rpc_create_reply_t;

typedef struct {
  uint32_t window_id;
} gui_rpc_window_request_t;

typedef struct {
  uint32_t window_id;
  uint32_t buffer;
} gui_rpc_frame_request_t;

typedef struct {
  uint32_t buffer;
} gui_rpc_frame_reply_t;

#ifdef __cplusplus
static_assert(sizeof(gui_rpc_frame_request_t) == 8, "GUI frame request ABI");
static_assert(sizeof(gui_rpc_frame_reply_t) == 4, "GUI frame reply ABI");
#else
_Static_assert(sizeof(gui_rpc_frame_request_t) == 8, "GUI frame request ABI");
_Static_assert(sizeof(gui_rpc_frame_reply_t) == 4, "GUI frame reply ABI");
#endif

typedef struct {
  uint32_t window_id;
  uint32_t enabled;
  uint32_t tid;
  uint32_t generation;
} gui_rpc_event_notifications_t;

enum gui_window_create_flag {
  GUI_CREATE_HIDDEN = 1u,
  GUI_CREATE_UNFOCUSED = 2u,
  GUI_CREATE_RESIZABLE = 4u,
};

enum gui_window_control {
  GUI_WINDOW_MOVE,
  GUI_WINDOW_SHOW,
  GUI_WINDOW_HIDE,
  GUI_WINDOW_FOCUS,
  GUI_WINDOW_SET_RESIZABLE,
  GUI_WINDOW_MOUSE_MODE,
};

enum gui_mouse_mode {
  GUI_MOUSE_NORMAL = 0,
  GUI_MOUSE_CAPTURE = 1u,
  GUI_MOUSE_RELATIVE = 2u,
  GUI_MOUSE_HIDDEN = 4u,
  GUI_MOUSE_CONFINED = 8u,
};

typedef struct {
  uint32_t window_id, width, height;
  uintptr_t client_mapping;
} gui_rpc_resize_request_t;

typedef struct {
  uint32_t window_id;
  uint32_t operation;
  int32_t x, y;
} gui_rpc_window_control_t;

#ifdef __cplusplus
static_assert(sizeof(gui_rpc_create_request_t) == (sizeof(uintptr_t) == 8 ? 32 : 28),
              "GUI create request ABI");
static_assert(sizeof(gui_window_state_t) == 40, "GUI window state ABI");
static_assert(sizeof(gui_event_t) == 32, "GUI event ABI");
static_assert(sizeof(gui_rpc_resize_request_t) ==
                  (sizeof(uintptr_t) == 8 ? 24 : 16),
              "GUI resize request ABI");
static_assert(sizeof(gui_rpc_window_control_t) == 16, "GUI window control ABI");
static_assert(sizeof(gui_rpc_event_notifications_t) == 16,
              "GUI event target ABI");
#else
_Static_assert(sizeof(gui_rpc_create_request_t) == (sizeof(uintptr_t) == 8 ? 32 : 28),
               "GUI create request ABI");
_Static_assert(sizeof(gui_window_state_t) == 40, "GUI window state ABI");
_Static_assert(sizeof(gui_event_t) == 32, "GUI event ABI");
_Static_assert(sizeof(gui_rpc_resize_request_t) ==
                   (sizeof(uintptr_t) == 8 ? 24 : 16),
               "GUI resize request ABI");
_Static_assert(sizeof(gui_rpc_window_control_t) == 16,
               "GUI window control ABI");
_Static_assert(sizeof(gui_rpc_event_notifications_t) == 16,
               "GUI event target ABI");
#endif

#endif
