#ifndef KERNEL_IO_POLL_H
#define KERNEL_IO_POLL_H
#include "../../apps/include/poll_abi.h"
#include <ctypes.h>

struct mtask;
struct io_poll_waiter;
typedef struct io_poll_watch {
  struct io_poll_watch *next, **previous;
  struct io_poll_waiter *waiter;
} io_poll_watch_t;
typedef struct {
  io_poll_watch_t *first;
} io_poll_queue_t;

/* Kernel lock held: subscriptions never own the watched resource. Closing it
 * wakes and detaches all subscriptions before releasing its memory. */
void io_poll_watch(io_poll_queue_t *queue, io_poll_watch_t *watch);
void io_poll_wake(io_poll_queue_t *queue);
int io_poll(struct pollfd *fds, size_t count, int timeout, bool user);
int io_poll_wait(io_poll_queue_t *queue);
void io_poll_tick(void);
void io_poll_cancel_task(struct mtask *task);
#endif
