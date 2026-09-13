#include <dos.h>
#include <io_poll.h>
#include <irq.h>
#include <limits.h>
#include <stdint.h>
#include <user_vm.h>

typedef struct io_poll_waiter {
  struct io_poll_waiter *next, **previous;
  struct io_poll_waiter *timer_next, **timer_previous;
  mtask *task;
  uint64_t deadline;
  size_t count;
  io_poll_watch_t watches[];
} io_poll_waiter_t;
static io_poll_waiter_t *waiters, *timers;

static void detach(io_poll_waiter_t *waiter) {
  for (size_t i = 0; i < waiter->count; i++) {
    io_poll_watch_t *watch = &waiter->watches[i];
    if (!watch->previous)
      continue;
    *watch->previous = watch->next;
    if (watch->next)
      watch->next->previous = watch->previous;
    watch->previous = NULL;
  }
  if (waiter->timer_previous) {
    *waiter->timer_previous = waiter->timer_next;
    if (waiter->timer_next)
      waiter->timer_next->timer_previous = waiter->timer_previous;
    waiter->timer_previous = NULL;
  }
}

void io_poll_watch(io_poll_queue_t *queue, io_poll_watch_t *watch) {
  if (!watch)
    return;
  watch->next = queue->first;
  watch->previous = &queue->first;
  if (watch->next)
    watch->next->previous = &watch->next;
  queue->first = watch;
}

void io_poll_wake(io_poll_queue_t *queue) {
  while (queue && queue->first) {
    io_poll_waiter_t *waiter = queue->first->waiter;
    detach(waiter);
    task_run(waiter->task);
  }
}

static void destroy(io_poll_waiter_t *waiter) {
  detach(waiter);
  *waiter->previous = waiter->next;
  if (waiter->next)
    waiter->next->previous = waiter->previous;
  free(waiter);
}

static io_poll_waiter_t *create(size_t count, size_t bytes) {
  io_poll_waiter_t *waiter =
      malloc(sizeof(*waiter) + count * sizeof(io_poll_watch_t) + bytes);
  if (!waiter)
    return NULL;
  memset(waiter, 0, sizeof(*waiter) + count * sizeof(io_poll_watch_t));
  waiter->task = current_task();
  waiter->count = count;
  waiter->next = waiters;
  waiter->previous = &waiters;
  if (waiters)
    waiters->previous = &waiter->next;
  waiters = waiter;
  for (size_t i = 0; i < count; i++)
    waiter->watches[i].waiter = waiter;
  return waiter;
}

int io_poll_wait(io_poll_queue_t *queue) {
  mtask *self = current_task();
  if (self->signals.pending & ~self->signals.blocked)
    return -4;
  io_poll_waiter_t *waiter = create(1, 0);
  if (!waiter)
    return -12;
  io_poll_watch(queue, waiter->watches);
  task_fall_blocked_reason(WAITING, WAIT_REASON_IO);
  int result = self->signals.pending & ~self->signals.blocked ? -4 : 0;
  destroy(waiter);
  return result;
}

int io_poll(struct pollfd *fds, size_t count, int timeout, bool user) {
  if (count > INT_MAX ||
      count > (SIZE_MAX - sizeof(io_poll_waiter_t)) /
                  (sizeof(io_poll_watch_t) + sizeof(struct pollfd)))
    return -22;
  size_t bytes = count * sizeof(*fds);
  irq_state_t state = irq_save();
  if (user && bytes &&
      (!user_vm_readable((uintptr_t)fds, bytes) ||
       !user_vm_prepare_write((uintptr_t)fds, bytes))) {
    irq_restore(state);
    return -14;
  }
  io_poll_waiter_t *waiter = create(count, bytes);
  if (!waiter) {
    irq_restore(state);
    return -12;
  }
  waiter->deadline = timeout < 0
                         ? UINT64_MAX
                         : monotonic_time_ns() + (uint64_t)timeout * 1000000;
  struct pollfd *copy = (void *)(waiter->watches + count);
  if (bytes)
    memcpy(copy, fds, bytes);
  int result;
  for (;;) {
    result = 0;
    for (size_t i = 0; i < count; i++) {
      io_poll_watch_t *watch = &waiter->watches[i];
      int fd = copy[i].fd;
      copy[i].revents =
          fd < 0 ? 0
          : ((uint32_t)fd & NET_SOCKET_HANDLE_TAG_MASK) == NET_SOCKET_HANDLE_TAG
              ? net_socket_poll(waiter->task->tgid, fd, copy[i].events, watch)
              : vfs_fd_poll(waiter->task->fs_context, fd, copy[i].events,
                            watch);
      result += copy[i].revents != 0;
    }
    if (result || timeout == 0 ||
        (waiter->deadline != UINT64_MAX &&
         monotonic_time_ns() >= waiter->deadline))
      break;
    if (waiter->task->signals.pending & ~waiter->task->signals.blocked) {
      result = -4;
      break;
    }
    if (waiter->deadline != UINT64_MAX) {
      io_poll_waiter_t **position = &timers;
      while (*position && (*position)->deadline <= waiter->deadline)
        position = &(*position)->timer_next;
      waiter->timer_next = *position;
      waiter->timer_previous = position;
      if (*position)
        (*position)->timer_previous = &waiter->timer_next;
      *position = waiter;
    }
    task_fall_blocked_reason(WAITING, WAIT_REASON_IO);
    detach(waiter);
  }
  if (result >= 0 && bytes) {
    if (user && !user_vm_prepare_write((uintptr_t)fds, bytes))
      result = -14;
    else
      for (size_t i = 0; i < count; i++)
        fds[i].revents = copy[i].revents;
  }
  destroy(waiter);
  irq_restore(state);
  return result;
}

void io_poll_tick(void) {
  if (!timers)
    return;
  irq_state_t state = irq_save();
  uint64_t now = monotonic_time_ns();
  while (timers && timers->deadline <= now) {
    io_poll_waiter_t *waiter = timers;
    detach(waiter);
    task_run(waiter->task);
  }
  irq_restore(state);
}

void io_poll_cancel_task(mtask *task) {
  irq_state_t state = irq_save();
  io_poll_waiter_t *waiter = waiters;
  while (waiter) {
    io_poll_waiter_t *next = waiter->next;
    if (waiter->task == task)
      destroy(waiter);
    waiter = next;
  }
  irq_restore(state);
}
