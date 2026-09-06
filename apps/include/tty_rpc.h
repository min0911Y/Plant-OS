#ifndef PLOS_TTY_RPC_H
#define PLOS_TTY_RPC_H
#include <ctypes.h>

#define TTY_RPC_DISPATCH 0x545459u

typedef enum {
  TTY_RPC_WRITE,
  TTY_RPC_MOVE,
  TTY_RPC_CLEAR,
  TTY_RPC_SCROLL,
  TTY_RPC_DRAW_BOX,
  TTY_RPC_INPUT_STATUS,
  TTY_RPC_INPUT_GET,
  TTY_RPC_COUNT
} tty_rpc_operation_t;

typedef struct {
  int32_t x, y;
  uint32_t color, cursor_visible;
} tty_rpc_state_t;

typedef struct {
  uint32_t handle, operation;
  tty_rpc_state_t state;
  union {
    struct {
      int32_t x, y;
    } cursor;
    struct {
      int32_t x, y, x1, y1;
      uint32_t color;
    } box;
  } args;
  // TTY_RPC_WRITE appends text bytes, without a terminating NUL.
} tty_rpc_request_t;

typedef struct {
  tty_rpc_state_t state;
  int32_t value;
} tty_rpc_reply_t;

#if defined(__cplusplus)
static_assert(sizeof(tty_rpc_request_t) == 44, "TTY request ABI");
static_assert(sizeof(tty_rpc_reply_t) == 20, "TTY reply ABI");
#else
_Static_assert(sizeof(tty_rpc_request_t) == 44, "TTY request ABI");
_Static_assert(sizeof(tty_rpc_reply_t) == 20, "TTY reply ABI");
#endif
#endif
