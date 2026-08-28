#ifndef PLOS_GUI_RPC_H
#define PLOS_GUI_RPC_H

#include <ctypes.h>

#define GUI_SERVICE_NAME "gui"
#define GUI_TITLE_MAX 255u

/* This range is reserved for client-side GUI shared mappings. */
#define GUI_SHARED_REGION_START 0xf0100000u
#define GUI_SHARED_REGION_END 0xf1000000u

#define GUI_EVENT_QUEUE_CAPACITY 64u

enum gui_event {
  GUI_EVENT_MOUSE_STAY = 1,
  GUI_EVENT_MOUSE_CLICK_LEFT,
  GUI_EVENT_MOUSE_CLICK_RIGHT,
  GUI_EVENT_CLOSE_WINDOW,
  GUI_EVENT_MOUSE_WHEEL,
};

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
  gui_event_queue_t events;
  gui_event_queue_t key_press;
  gui_event_queue_t key_up;
  gui_damage_t damage;
} gui_window_shared_t;

static inline void gui_damage_lock(gui_damage_t *damage) {
  uint32_t locked = 1;
  do {
    asm volatile("xchgl %0, %1"
                 : "+r"(locked), "+m"(damage->lock)
                 :
                 : "memory");
  } while (locked);
}

static inline void gui_damage_unlock(gui_damage_t *damage) {
  asm volatile("" ::: "memory");
  damage->lock = 0;
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
  if (write - queue->read >= GUI_EVENT_QUEUE_CAPACITY) {
    return 0;
  }
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = value;
  asm volatile("" ::: "memory");
  queue->write = write + 1;
  return 1;
}

static inline int gui_event_queue_push2(gui_event_queue_t *queue,
                                        uint32_t first, uint32_t second) {
  uint32_t write = queue->write;
  if (write - queue->read > GUI_EVENT_QUEUE_CAPACITY - 2) {
    return 0;
  }
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = first;
  queue->data[(write + 1) % GUI_EVENT_QUEUE_CAPACITY] = second;
  asm volatile("" ::: "memory");
  queue->write = write + 2;
  return 1;
}

static inline int gui_event_queue_push3(gui_event_queue_t *queue,
                                        uint32_t first, uint32_t second,
                                        uint32_t third) {
  uint32_t write = queue->write;
  if (write - queue->read > GUI_EVENT_QUEUE_CAPACITY - 3) {
    return 0;
  }
  queue->data[write % GUI_EVENT_QUEUE_CAPACITY] = first;
  queue->data[(write + 1) % GUI_EVENT_QUEUE_CAPACITY] = second;
  queue->data[(write + 2) % GUI_EVENT_QUEUE_CAPACITY] = third;
  asm volatile("" ::: "memory");
  queue->write = write + 3;
  return 1;
}

static inline int gui_event_queue_pop(gui_event_queue_t *queue) {
  uint32_t read = queue->read;
  if (read == queue->write) {
    return -1;
  }
  uint32_t value = queue->data[read % GUI_EVENT_QUEUE_CAPACITY];
  asm volatile("" ::: "memory");
  queue->read = read + 1;
  return (int)value;
}

static inline unsigned gui_event_queue_count(const gui_event_queue_t *queue) {
  return queue->write - queue->read;
}

enum gui_rpc_opcode {
  GUI_RPC_CREATE_WINDOW = 1,
  GUI_RPC_CLOSE_WINDOW,
  GUI_RPC_REFRESH_WINDOW,
  GUI_RPC_START_KEYBOARD,
  GUI_RPC_STOP_KEYBOARD,
  GUI_RPC_COUNT,
};

static inline int gui_window_shared_mapping_size(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t *size_out) {
  if (size_out == NULL || width == 0 || height == 0 ||
      width > 0xffffffffu / height) {
    return 0;
  }
  uint32_t pixels = width * height;
  if (pixels > (0xffffffffu - sizeof(gui_window_shared_t)) / sizeof(uint32_t)) {
    return 0;
  }
  uint32_t size = sizeof(gui_window_shared_t) + pixels * sizeof(uint32_t);
  if (size > 0xffffffffu - 0xfffu) {
    return 0;
  }
  *size_out = (size + 0xfffu) & ~0xfffu;
  return 1;
}

typedef struct {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t client_mapping;
  uint32_t title_length;
} gui_rpc_create_request_t;

typedef struct {
  uint32_t window_id;
} gui_rpc_create_reply_t;

typedef struct {
  uint32_t window_id;
} gui_rpc_window_request_t;

#endif
