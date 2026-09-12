/*
 * Powerint DOS 386 LOCK的实现
 * Copyright (c) min0911_ 2022
 * @date 2022-6-8
 */
#include <dos.h>
#include <irq.h>

void lock_init(lock_t *l) {
  l->owner = NULL;
  l->value = LOCK_UNLOCKED;
  l->waiter = NULL;
  l->waiter_tail = NULL;
}

static void lock_waiter_remove(mtask *task) {
  lock_t *lock = task->waiting_lock;
  if (!lock)
    return;
  if (task->lock_previous)
    task->lock_previous->lock_next = task->lock_next;
  else
    lock->waiter = task->lock_next;
  if (task->lock_next)
    task->lock_next->lock_previous = task->lock_previous;
  else
    lock->waiter_tail = task->lock_previous;
  task->waiting_lock = NULL;
  task->lock_next = NULL;
  task->lock_previous = NULL;
}

static void lock_waiter_add(lock_t *lock, mtask *task) {
  if (task->waiting_lock == lock)
    return;
  if (task->waiting_lock)
    lock_waiter_remove(task);
  task->waiting_lock = lock;
  task->lock_next = NULL;
  task->lock_previous = lock->waiter_tail;
  if (lock->waiter_tail)
    lock->waiter_tail->lock_next = task;
  else
    lock->waiter = task;
  lock->waiter_tail = task;
}

/* Every blocked task has its own queue node. The old single waiter slot could
 * strand all but the last contender when several CPUs entered the lock. */
void lock(lock_t *key) {
  irq_state_t state = irq_save();
  mtask *self = current_task();
  while (key->value != LOCK_UNLOCKED) {
    lock_waiter_add(key, self);
    mtask_run_now(key->owner);
    task_fall_blocked_reason(WAITING, WAIT_REASON_LOCK);
  }
  lock_waiter_remove(self);
  key->value = LOCK_LOCKED;
  key->owner = self;
  irq_restore(state);
}

void unlock(lock_t *key) {
  irq_state_t state = irq_save();
  if (key->owner != current_task()) {
    irq_restore(state);
    return;
  }
  key->value = LOCK_UNLOCKED;
  key->owner = NULL;
  mtask *waiter = key->waiter;
  if (waiter) {
    lock_waiter_remove(waiter);
    task_run(waiter);
    mtask_run_now(waiter);
    task_next();
  }
  irq_restore(state);
}

void lock_cancel_task(mtask *task) {
  irq_state_t state = irq_save();
  lock_waiter_remove(task);
  irq_restore(state);
}
