#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

extern int dynamic_value(void);
extern int shared_data;
extern int *const relro_pointer;
static int initialized;
int overridden(void) { return 24; }
void dynamic_trace(char *message) { logk(message); }
static void __attribute__((constructor)) initialize(void) {
  initialized = dynamic_value();
}
static void __attribute__((destructor)) finalize(void) {
  logk("DYNAMIC FINI main\n");
}
static void exited(void) { logk("DYNAMIC ATEXIT\n"); }
int main(int argc, char **argv) {
  if (initialized != 42 || dynamic_value() != 42 || argc != 3 ||
      strcmp(argv[0], "custom argv0") || strcmp(argv[1], "hello world") ||
      strcmp(argv[2], ""))
    return 10;
  if (atexit(exited))
    return 11;
  int child = fork();
  if (child < 0)
    return 12;
  if (!child) {
    if (dynamic_value() != 42)
      _exit(13);
    shared_data = 29;
    if (dynamic_value() != 64)
      _exit(15);
    _exit(0);
  }
  if (waittid(child) || dynamic_value() != 42)
    return 14;
  child = fork();
  if (child < 0)
    return 16;
  if (!child) {
    *(int *volatile *)&relro_pointer = NULL;
    _exit(0);
  }
  if (!waittid(child) || relro_pointer != &shared_data)
    return 17;
  puts("DYNAMIC PASS");
  logk("DYNAMIC PASS\n");
  return 0;
}
