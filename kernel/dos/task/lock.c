/*
 * Powerint DOS 386 LOCK的实现
 * Copyright (c) min0911_ 2022
 * @date 2022-6-8
 */
#include <dos.h>
bool cas(int *ptr, int old, int New) {
  int old_value = *ptr;
  if (old_value == old) {
    *ptr = New;
    return true;
  }
  return false;
}
void lock(lock_t *key) {
  while (!cas((int *)key, LOCK_UNLOCKED, LOCK_LOCKED))
    ;
}
void unlock(lock_t *key) {
  if(*key == LOCK_UNLOCKED) return;
  while (!cas((int *)key, LOCK_LOCKED, LOCK_UNLOCKED))
    ;
}
