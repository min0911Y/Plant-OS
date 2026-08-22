#include <syscall.h>
#include <string.h>
typedef void (*EXIT_CALL)(void);
static EXIT_CALL *e = NULL;
static int exit_call_number = 0;
static int the_seat_has_alloced = 0;
void atexit(void (*func)(void)) {
  if(e == NULL) {
    e = (EXIT_CALL *)malloc(10 * sizeof(EXIT_CALL));
    the_seat_has_alloced = 10;
  }
  if (exit_call_number >= the_seat_has_alloced) {
    e = realloc(e,the_seat_has_alloced * 2 * sizeof(EXIT_CALL));
  }
  e[exit_call_number++] = func;
}
void exit(unsigned status) {
  TaskLock();
  for(int i = 0;i<exit_call_number;i++) {
    ((e[i]))();
  }
  _exit(status);
  TaskUnlock();
}