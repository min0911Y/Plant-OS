#include <scheduler.h>
#include <dos.h>
#include <input_device.h>
#include <irq.h>
#include <key_input.h>

_Static_assert(sizeof(mouse_event_t) == 16, "mouse event ABI");

mtask *keyboard_use_task;

char keytable[0x54] = { // 按下Shift
    0,    '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t',   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    10,   0,      'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    '|',    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    'D', '8', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};
char keytable1[0x54] = { // 未按下Shift
    0,    '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t',   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    10,   0,      'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\',   'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.'};

extern struct tty *tty_default;
static struct tty *tty_for_input(void) {
  mtask *task = current_task();
  if (task != NULL && task->TTY != NULL && task->TTY->using1 == 1) {
    return task->TTY;
  }
  return tty_default;
}
int tty_fifo_status() {
  struct tty *tty = tty_for_input();
  if (tty == NULL) {
    return 0;
  }
  return tty->fifo_status(tty);
}
int tty_fifo_get() {
  struct tty *tty = tty_for_input();
  if (tty == NULL) {
    return -1;
  }
  return tty->fifo_get(tty);
}
int input_char_inSM() {
  for (;;) {
    irq_state_t state = irq_save();
    struct tty *tty = tty_for_input();
    uint32_t sequence = tty != NULL ? tty->input_sequence : 0;
    int input = tty != NULL ? tty->fifo_get(tty) : -1;
    if (input != -1) {
      irq_restore(state);
      return input;
    }
    // An RPC read can sleep. Do not lose input delivered before its reply.
    if (tty == NULL || tty->input_sequence == sequence)
      task_fall_blocked_reason(WAITING, WAIT_REASON_KEYBOARD);
    irq_restore(state);
  }
}
int kbhit() {
  return tty_fifo_status() != 0; // 进程的键盘FIFO缓冲区是否为空
}

static bool keyboard_wake_task(mtask *task) {
  if (task == NULL) {
    return false;
  }
  task_set_weight(task, 5);
  task_run(task);
  if (task == current_task()) {
    return false;
  }
  mtask_run_now(task);
  return true;
}

static mtask *keyboard_foreground_task(void) {
  struct tty *foreground = now_tty();
  if (foreground == NULL) {
    return NULL;
  }

  mtask *process = NULL;
  mtask *fallback = NULL;
  task_iterator_t iterator = {0};
  mtask *task;
  while ((task = task_iter_next(&iterator)) != NULL) {
    if (task->TTY != foreground || task->keyfifo == NULL ||
        task->state == DIED || task->terminate_pending) {
      continue;
    }
    if (task == current_task()) {
      return task;
    }
    if (process == NULL && task->kind == TASK_PROCESS) {
      process = task;
    }
    if (fallback == NULL) {
      fallback = task;
    }
  }
  return process != NULL ? process : fallback;
}

static uint16_t key_references[512];
static bool caps_lock;
static input_keyboard_t *repeat_source;
static uint16_t repeat_code;
static uint64_t repeat_deadline;

static bool input_key_deliver(uint16_t code, bool pressed) {
  mtask *target = keyboard_use_task != NULL ? keyboard_use_task
                                            : keyboard_foreground_task();
  if (target == NULL) {
    return false;
  }
  uint8_t scan = (code & 0x7f) | (pressed ? 0 : 0x80);
  void (*callback)(unsigned char, uint32_t) =
      pressed ? target->keyboard_press : target->keyboard_release;
  struct FIFO8 *queue = pressed ? target->Pkeyfifo : target->Ukeyfifo;
  unsigned needed = (code & 0x100) ? 2 : 1;
  if (callback != NULL && (queue == NULL || (unsigned)queue->free >= needed)) {
    if (code & 0x100) {
      callback(0xe0, target->tid);
    }
    callback(scan, target->tid);
  }
  if (keyboard_use_task == NULL && pressed && target->keyfifo != NULL) {
    unsigned bytes = (code & 0x100) ? 2 : 1;
    if ((unsigned)target->keyfifo->free >= bytes) {
      if (bytes == 2) {
        fifo8_put(target->keyfifo, 0xe0);
      }
      fifo8_put(target->keyfifo, scan);
    }
  }
  return keyboard_wake_task(target);
}

bool input_keyboard_event(input_keyboard_t *source, uint16_t code,
                          bool pressed) {
  irq_state_t state = irq_save();
  if (code == 0 || code >= 512) {
    irq_restore(state);
    return false;
  }
  bool was_down = (source->down[code / 8] & (1u << (code % 8))) != 0;
  if (!pressed && !was_down) {
    irq_restore(state);
    return false;
  }
  if (pressed != was_down) {
    source->down[code / 8] ^= 1u << (code % 8);
    if (pressed) {
      if (key_references[code]++ == 0 && code == 0x3a) {
        caps_lock = !caps_lock;
      }
    } else if (--key_references[code] != 0) {
      irq_restore(state);
      return false;
    }
  }
  bool modifier = code == 0x2a || code == 0x36 || code == 0x1d ||
                  code == 0x11d || code == 0x38 || code == 0x138 ||
                  code == 0x15b || code == 0x15c || code == 0x3a ||
                  code == 0x45 || code == 0x46;
  if (pressed && source->software_repeat && !modifier) {
    repeat_source = source;
    repeat_code = code;
    repeat_deadline = monotonic_time_ns() + 500000000ull;
  } else if (!pressed && source == repeat_source && code == repeat_code) {
    repeat_source = NULL;
  }
  if (pressed && code == 0x2e &&
      (key_references[0x1d] || key_references[0x11d])) {
    mtask *target = keyboard_use_task != NULL ? keyboard_use_task
                                              : keyboard_foreground_task();
    if (target != NULL && target->sigint_up) {
      target->signal |= SIGMASK(SIGINT);
    }
  }
  bool reschedule = input_key_deliver(code, pressed);
  irq_restore(state);
  return reschedule;
}

void input_keyboard_release(input_keyboard_t *source) {
  irq_state_t state = irq_save();
  if (repeat_source == source) {
    repeat_source = NULL;
  }
  for (unsigned code = 1; code < 512; code++) {
    if (source->down[code / 8] & (1u << (code % 8))) {
      input_keyboard_event(source, code, false);
    }
  }
  irq_restore(state);
}

void input_keyboard_tick(void) {
  if (repeat_source != NULL && monotonic_time_ns() >= repeat_deadline) {
    repeat_deadline = monotonic_time_ns() + 33000000ull;
    input_key_deliver(repeat_code, true);
  }
}

int sc2a(int sc) {
  if (sc >= 0x80) {
    switch (sc - 0x80) {
    case 0x48:
      return KEY_INPUT_UP;
    case 0x50:
      return KEY_INPUT_DOWN;
    case 0x4b:
      return KEY_INPUT_LEFT;
    case 0x4d:
      return KEY_INPUT_RIGHT;
    case 0x47:
      return KEY_INPUT_HOME;
    case 0x4f:
      return KEY_INPUT_END;
    case 0x49:
      return KEY_INPUT_PAGE_UP;
    case 0x51:
      return KEY_INPUT_PAGE_DOWN;
    case 0x53:
      return KEY_INPUT_DELETE;
    case 0x52:
      return KEY_INPUT_INSERT;
    case 0x1c:
      return '\n';
    case 0x35:
      return '/';
    default:
      return 0;
    }
  }
  if (sc < 0 || sc >= (int)sizeof(keytable)) {
    return 0;
  }
  bool shifted = key_references[0x2a] || key_references[0x36];
  if (keytable1[sc] >= 'a' && keytable1[sc] <= 'z') {
    shifted ^= caps_lock;
  }
  int character = shifted ? keytable[sc] : keytable1[sc];
  if (key_references[0x1d] || key_references[0x11d]) {
    if (character >= 'a' && character <= 'z')
      character -= 'a' - 'A';
    if (character >= '@' && character <= '_')
      return character & 0x1f;
    if (character == '?')
      return 127;
    if (character == ' ')
      return 0;
  }
  return character;
}

int getch(void) {
  int code = input_char_inSM();
  return sc2a(code == 0xe0 ? input_char_inSM() | 0x80 : code);
}

mtask *mouse_use_task;
void mouse_sleep(void) { mouse_use_task = NULL; }
void mouse_ready(void) { mouse_use_task = current_task(); }

bool input_mouse_event(input_pointer_t *source, const mouse_event_t *event) {
  irq_state_t state = irq_save();
  static uint16_t button_references[32];
  static uint32_t buttons;
  uint32_t changed = source->buttons ^ event->buttons;
  for (unsigned bit = 0; bit < 32; bit++) {
    uint32_t mask = 1u << bit;
    if (!(changed & mask))
      continue;
    if (event->buttons & mask) {
      button_references[bit]++;
      buttons |= mask;
    } else if (--button_references[bit] == 0) {
      buttons &= ~mask;
    }
  }
  source->buttons = event->buttons;
  mouse_event_t combined = *event;
  combined.buttons = buttons;
  mtask *task = mouse_use_task;
  if (task == NULL || task->mousefifo == NULL ||
      task->mousefifo->free < (int)sizeof(*event)) {
    irq_restore(state);
    return false;
  }
  const uint8_t *bytes = (const uint8_t *)&combined;
  for (unsigned i = 0; i < sizeof(*event); i++) {
    fifo8_put(task->mousefifo, bytes[i]);
  }
  task_set_weight(task, 5);
  task_run(task);
  if (task != current_task()) {
    mtask_run_now(task);
    irq_restore(state);
    return true;
  }
  irq_restore(state);
  return false;
}

bool input_mouse_read(mouse_event_t *event) {
  irq_state_t state = irq_save();
  mtask *task = current_task();
  if (mouse_use_task != task || task->mousefifo == NULL ||
      fifo8_status(task->mousefifo) < (int)sizeof(*event)) {
    irq_restore(state);
    return false;
  }
  uint8_t *bytes = (uint8_t *)event;
  for (unsigned i = 0; i < sizeof(*event); i++) {
    bytes[i] = fifo8_get(task->mousefifo);
  }
  irq_restore(state);
  return true;
}
