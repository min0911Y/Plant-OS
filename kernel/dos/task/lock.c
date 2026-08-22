/*
 * Powerint DOS 386 LOCK的实现
 * Copyright (c) min0911_ 2022
 * @date 2022-6-8
 */
#include <dos.h>
void mtask_stop();
void mtask_start();
extern char mtask_stop_flag;
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
bool cas(int *ptr, int old, int New) {
  int old_value = *ptr;
  if (old_value == old) {
    *ptr = New;
    return true;
  }
  return false;
}
// FIXME:!!!!
void lock_init(lock_t *l) {
  lock_set_owner(l, NULL);
  lock_set_value(l, LOCK_UNLOCKED);
  lock_set_waiter(l, NULL);
}
// FUXK!!!!!!!!!!
bool interrupt_disable()
{
    unsigned flags;
    asm volatile(
        "pushfl\n"    // 保存 cli 之前的 eflags
        "popl %0\n"
        "cli\n"
        : "=r"(flags)
        :
        : "memory");
    return (flags >> 9) & 1;
}

// 获得 IF 位
bool get_interrupt_state()
{
    unsigned flags;
    asm volatile(
        "pushfl\n"
        "popl %0\n"
        : "=r"(flags)
        :
        : "memory");
    return (flags >> 9) & 1;
}

// 设置 IF 位
void set_interrupt_state(bool state)
{
    if (state)
        asm volatile("sti\n" ::: "memory");
    else
        asm volatile("cli\n" ::: "memory");
}
void lock(lock_t *key) {
  int state = interrupt_disable();
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
  interrupt_disable();
  lock_set_waiter(key, NULL);
  lock_set_value(key, LOCK_LOCKED);
  lock_set_owner(key, current_task());
  set_interrupt_state(state);
}
void unlock(lock_t *key) {
  int state = interrupt_disable();
  mtask *waiter;
  lock_set_value(key, LOCK_UNLOCKED);
  waiter = lock_get_waiter(key);
  if (waiter) {
    mtask_run_now(waiter);
    task_run(waiter);
    lock_set_waiter(key, NULL);
    task_next();
  }
  set_interrupt_state(state);
}
