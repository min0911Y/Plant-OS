/*
 * Powerint DOS 386 LOCK的实现
 * Copyright (c) min0911_ 2022
 * @date 2022-6-8
 */
#include <dos.h>
#include <irq.h>
static mtask *lock_get_owner(lock_t *l) {
  mtask *owner;
  memcpy(&owner, &l->owner, sizeof(owner));
  return owner;
}
static void lock_set_owner(lock_t *l, mtask *owner) {
  memcpy(&l->owner, &owner, sizeof(owner));
}
static unsigned lock_get_value(lock_t *l) {
  unsigned value;
  memcpy(&value, &l->value, sizeof(value));
  return value;
}
static void lock_set_value(lock_t *l, unsigned value) {
  memcpy(&l->value, &value, sizeof(value));
}
static mtask *lock_get_waiter(lock_t *l) {
  mtask *waiter;
  memcpy(&waiter, &l->waiter, sizeof(waiter));
  return waiter;
}
static void lock_set_waiter(lock_t *l, mtask *waiter) {
  memcpy(&l->waiter, &waiter, sizeof(waiter));
}
void lock_init(lock_t *l) {
  lock_set_owner(l, NULL);
  lock_set_value(l, LOCK_UNLOCKED);
  lock_set_waiter(l, NULL);
}
void lock(lock_t *key) {
  irq_state_t state = irq_save();
  if (lock_get_value(key) != LOCK_UNLOCKED) {
    lock_set_waiter(key, current_task());
    if (lock_get_value(key) != LOCK_UNLOCKED && lock_get_waiter(key)) {
      
      mtask_run_now(lock_get_owner(key));
      if (current_task()->ready == 1) {
        current_task()->ready = 0;
      }
      task_fall_blocked_reason(WAITING, WAIT_REASON_LOCK);
      
    }
  }
  lock_set_waiter(key, NULL);
  lock_set_value(key, LOCK_LOCKED);
  lock_set_owner(key, current_task());
  irq_restore(state);
}
void unlock(lock_t *key) {
  irq_state_t state = irq_save();
  mtask *waiter;
  lock_set_value(key, LOCK_UNLOCKED);
  waiter = lock_get_waiter(key);
  if (waiter) {
    mtask_run_now(waiter);
    task_run(waiter);
    lock_set_waiter(key, NULL);
    task_next();
  }
  irq_restore(state);
}
