#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <rpc.h>
#include <stdint.h>
#include <tty_rpc.h>

#define FARTTY_TIMEOUT_MS 2000
#define FARTTY_TEXT_MAX                                                        \
  (IPC_MAX_MSG_SIZE - sizeof(rpc_wire_t) - sizeof(tty_rpc_request_t))

extern struct List *tty_list;
static uint32_t next_handle = 1;

static void fartty_wake(struct tty *tty) {
  task_iterator_t iterator = {0};
  mtask *task;
  while ((task = task_iter_next(&iterator)) != NULL) {
    if (task->TTY == tty && task->state == WAITING &&
        task->wait_reason == WAIT_REASON_TTY)
      task_run(task);
  }
}

static int fartty_call(struct tty *tty, tty_rpc_request_t *request,
                       unsigned size, tty_rpc_reply_t *out_reply) {
  mtask *self = current_task();
  if (self->tid == tty->remote.server.tid)
    return -1;
  irq_state_t state = irq_save();
  while (tty->remote.busy.generation != 0) {
    task_fall_blocked_reason(WAITING, WAIT_REASON_TTY);
  }
  if (tty->remote.disconnected) {
    irq_restore(state);
    return -1;
  }
  tty->remote.busy = (rpc_endpoint_t){self->tid, self->generation};
  request->handle = tty->remote.handle;
  request->state =
      (tty_rpc_state_t){tty->x, tty->y, tty->color, tty->cur_moving != 0};
  tty_rpc_reply_t reply;
  unsigned length;
  int result = rpc_call(&tty->remote.server, tty->remote.opcode, request, size,
                        &reply, sizeof(reply), &length, FARTTY_TIMEOUT_MS);
  bool valid = result == RPC_OK && length == sizeof(reply) &&
               reply.state.x >= 0 && reply.state.x <= tty->xsize &&
               reply.state.y >= 0 && reply.state.y < tty->ysize &&
               reply.state.color <= 255 && reply.state.cursor_visible <= 1;
  if (valid) {
    tty->x = reply.state.x;
    tty->y = reply.state.y;
    if (tty->color == request->state.color)
      tty->color = reply.state.color;
  } else {
    tty->remote.disconnected = true;
    logk("TTY RPC disconnected: handle=%u status=%d\n", tty->remote.handle,
         result == RPC_OK ? RPC_ERR_TRANSPORT : result);
  }
  tty->remote.busy = (rpc_endpoint_t){0};
  fartty_wake(tty);
  irq_restore(state);
  if (valid && out_reply != NULL)
    *out_reply = reply;
  return valid ? reply.value : -1;
}

int fartty_get_pointer(struct tty *tty, tty_pointer_t *pointer) {
  if (tty == NULL || !tty->using1 || tty->remote.handle == 0)
    return -1;
  tty_rpc_request_t request = {.operation = TTY_RPC_POINTER};
  tty_rpc_reply_t reply;
  if (fartty_call(tty, &request, sizeof(request), &reply) != 0 ||
      reply.pointer.column <= 0 || reply.pointer.column > tty->xsize ||
      reply.pointer.row <= 0 || reply.pointer.row > tty->ysize)
    return -1;
  *pointer = reply.pointer;
  return 0;
}

static void fartty_putchar(struct tty *tty, int c) {
  struct {
    tty_rpc_request_t request;
    char text;
  } message = {.request.operation = TTY_RPC_WRITE, .text = (char)c};
  fartty_call(tty, &message.request, sizeof(message.request) + 1, NULL);
}

static void fartty_print(struct tty *tty, const char *text) {
  struct {
    tty_rpc_request_t request;
    char text[FARTTY_TEXT_MAX];
  } message;
  memset(&message.request, 0, sizeof(message.request));
  message.request.operation = TTY_RPC_WRITE;
  while (*text != '\0' && !tty->remote.disconnected) {
    unsigned length = 0;
    while (length < sizeof(message.text) && text[length] != '\0') {
      message.text[length] = text[length];
      length++;
    }
    fartty_call(tty, &message.request, sizeof(message.request) + length, NULL);
    text += length;
  }
}

static void fartty_move(struct tty *tty, int x, int y) {
  if (x < 0 || x > tty->xsize || y < 0 || y >= tty->ysize)
    return;
  tty_rpc_request_t request = {.operation = TTY_RPC_MOVE,
                               .args.cursor = {x, y}};
  fartty_call(tty, &request, sizeof(request), NULL);
}

static void fartty_clear(struct tty *tty) {
  tty_rpc_request_t request = {.operation = TTY_RPC_CLEAR};
  fartty_call(tty, &request, sizeof(request), NULL);
}

static void fartty_scroll(struct tty *tty) {
  tty_rpc_request_t request = {.operation = TTY_RPC_SCROLL};
  fartty_call(tty, &request, sizeof(request), NULL);
}

static void fartty_draw_box(struct tty *tty, int x, int y, int x1, int y1,
                            unsigned char color) {
  if (x < 0 || y < 0 || x1 < x || y1 < y || x1 >= tty->xsize ||
      y1 >= tty->ysize)
    return;
  tty_rpc_request_t request = {.operation = TTY_RPC_DRAW_BOX,
                               .args.box = {x, y, x1, y1, color}};
  fartty_call(tty, &request, sizeof(request), NULL);
}

static int fartty_fifo_status(struct tty *tty) {
  tty_rpc_request_t request = {.operation = TTY_RPC_INPUT_STATUS};
  int count = fartty_call(tty, &request, sizeof(request), NULL);
  return count > 0 ? count : 0;
}

static int fartty_fifo_get(struct tty *tty) {
  tty_rpc_request_t request = {.operation = TTY_RPC_INPUT_GET};
  int key = fartty_call(tty, &request, sizeof(request), NULL);
  return key >= 0 && key <= 255 ? key : -1;
}

struct tty *fartty_alloc(mtask *server, unsigned opcode, int xsize, int ysize) {
  if (server == NULL || server->state == DIED || server->terminate_pending ||
      server->tgid != current_task()->tgid || next_handle == 0 || xsize <= 0 ||
      ysize <= 0 || xsize > INT_MAX / ysize)
    return NULL;
  struct tty *tty = tty_alloc(NULL, xsize, ysize, fartty_putchar, fartty_move,
                              fartty_clear, fartty_scroll, fartty_draw_box,
                              fartty_fifo_status, fartty_fifo_get);
  if (tty == NULL)
    return NULL;
  tty->remote.server = (rpc_endpoint_t){server->tid, server->generation};
  tty->remote.opcode = opcode;
  tty->remote.handle = next_handle++;
  tty->native_ansi = true;
  tty->print = fartty_print;
  return tty;
}

struct tty *fartty_lookup(uintptr_t handle) {
  if (handle == 0 || handle > UINT32_MAX || tty_list == NULL)
    return NULL;
  for (size_t index = 1;; index++) {
    struct List *entry = list_get(index, tty_list);
    if (entry == NULL)
      return NULL;
    struct tty *tty = (struct tty *)entry->val;
    if (tty->remote.handle != handle)
      continue;
    mtask *server = get_task(tty->remote.server.tid);
    return server != NULL && server->state != DIED &&
                   server->generation == tty->remote.server.generation &&
                   server->tgid == current_task()->tgid
               ? tty
               : NULL;
  }
}

void fartty_task_cleanup(mtask *task) {
  if (tty_list == NULL)
    return;
  for (size_t index = 1;;) {
    struct List *entry = list_get(index, tty_list);
    if (entry == NULL)
      return;
    struct tty *tty = (struct tty *)entry->val;
    if (tty->using1 && tty->remote.handle != 0 &&
        tty->remote.server.tid == task->tid &&
        tty->remote.server.generation == task->generation) {
      extern struct tty *tty_default;
      if (task->TTY == tty)
        task->TTY = tty_default;
      if (task->tty_session == tty)
        task->tty_session = tty_default;
      tty_free(tty);
      index = 1; // Closing sessions can recursively retire other TTYs.
      continue;
    }
    if (tty->remote.busy.tid == task->tid &&
        tty->remote.busy.generation == task->generation) {
      tty->remote.busy = (rpc_endpoint_t){0};
      fartty_wake(tty);
    }
    index++;
  }
}
