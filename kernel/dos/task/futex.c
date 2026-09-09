#include <dos.h>
#include <futex.h>
#include <irq.h>
#include <limits.h>
#include <stdint.h>
#include <user_vm.h>

/* Wait records live on the blocked syscall's kernel stack. The kernel lock
 * serializes both queues; teardown detaches records before freeing stacks.
 * Buckets limit lookup work, not the number of waiters. */
typedef struct futex_waiter {
  struct futex_waiter *next, **previous;
  struct futex_waiter *deadline_next, **deadline_previous;
  mtask *task;
  uintptr_t address;
  uint64_t deadline_ns;
  int result;
} futex_waiter_t;

enum { FUTEX_BUCKET_COUNT = 64 };
static futex_waiter_t *buckets[FUTEX_BUCKET_COUNT];
static futex_waiter_t *deadlines;

static void futex_remove(futex_waiter_t *waiter) {
  if (waiter->previous) {
    *waiter->previous = waiter->next;
    if (waiter->next)
      waiter->next->previous = waiter->previous;
    waiter->previous = NULL;
  }
  if (waiter->deadline_previous) {
    *waiter->deadline_previous = waiter->deadline_next;
    if (waiter->deadline_next)
      waiter->deadline_next->deadline_previous = waiter->deadline_previous;
    waiter->deadline_previous = NULL;
  }
}

int futex_operation(unsigned operation, const futex_request_t *request) {
  uintptr_t address = request->address;
  if (operation >= FUTEX_OPERATION_COUNT || (address & 3) ||
      (operation == FUTEX_WAKE &&
       (request->value > INT_MAX || request->deadline_ns)))
    return FUTEX_INVALID;

  irq_state_t state = irq_save();
  if (!user_vm_readable(address, sizeof(uint32_t))) {
    irq_restore(state);
    return FUTEX_FAULT;
  }
  mtask *self = current_task();
  uintptr_t hash = (address >> 2) ^ ((uintptr_t)self->tgid * 2654435761u);
  hash ^= hash >> 16;
  futex_waiter_t **bucket = &buckets[hash % FUTEX_BUCKET_COUNT];
  if (operation == FUTEX_WAKE) {
    unsigned woken = 0;
    uint64_t now = 0;
    bool have_time = false;
    futex_waiter_t *waiter = *bucket;
    while (waiter && woken < request->value) {
      futex_waiter_t *next = waiter->next;
      if (waiter->task->tgid == self->tgid && waiter->address == address) {
        futex_remove(waiter);
        if (waiter->deadline_ns != UINT64_MAX && !have_time) {
          now = monotonic_time_ns();
          have_time = true;
        }
        bool expired =
            waiter->deadline_ns != UINT64_MAX && now >= waiter->deadline_ns;
        waiter->result = expired ? FUTEX_TIMED_OUT : FUTEX_OK;
        woken += !expired;
        task_run(waiter->task);
      }
      waiter = next;
    }
    irq_restore(state);
    return woken;
  }

  if (__atomic_load_n((uint32_t *)address, __ATOMIC_RELAXED) !=
      request->value) {
    irq_restore(state);
    return FUTEX_CHANGED;
  }
  if (request->deadline_ns != UINT64_MAX &&
      monotonic_time_ns() >= request->deadline_ns) {
    irq_restore(state);
    return FUTEX_TIMED_OUT;
  }

  futex_waiter_t waiter = {
      .next = *bucket,
      .previous = bucket,
      .task = self,
      .address = address,
      .deadline_ns = request->deadline_ns,
      .result = FUTEX_INTERRUPTED,
  };
  if (waiter.next)
    waiter.next->previous = &waiter.next;
  *bucket = &waiter;
  if (waiter.deadline_ns != UINT64_MAX) {
    futex_waiter_t **position = &deadlines;
    while (*position && (*position)->deadline_ns <= waiter.deadline_ns)
      position = &(*position)->deadline_next;
    waiter.deadline_previous = position;
    waiter.deadline_next = *position;
    if (*position)
      (*position)->deadline_previous = &waiter.deadline_next;
    *position = &waiter;
  }

  /* Comparison, queue publication and the scheduler's ready check share this
   * critical section, so a concurrent unlock cannot lose its wakeup. */
  task_fall_blocked_reason(WAITING, WAIT_REASON_FUTEX);
  futex_remove(&waiter);
  irq_restore(state);
  return waiter.result;
}

void futex_tick(void) {
  if (!deadlines)
    return;
  irq_state_t state = irq_save();
  uint64_t now = monotonic_time_ns();
  while (deadlines && deadlines->deadline_ns <= now) {
    futex_waiter_t *waiter = deadlines;
    futex_remove(waiter);
    waiter->result = FUTEX_TIMED_OUT;
    task_run(waiter->task);
  }
  irq_restore(state);
}

void futex_cancel_task(mtask *task) {
  irq_state_t state = irq_save();
  for (unsigned i = 0; i < FUTEX_BUCKET_COUNT; i++) {
    for (futex_waiter_t *waiter = buckets[i]; waiter; waiter = waiter->next) {
      if (waiter->task == task) {
        futex_remove(waiter);
        irq_restore(state);
        return;
      }
    }
  }
  irq_restore(state);
}
