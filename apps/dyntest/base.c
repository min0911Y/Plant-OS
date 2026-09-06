#include <syscall.h>

int base_state;
extern void dynamic_trace(char *) __attribute__((weak));
static void __attribute__((constructor)) initialize(void) { base_state = 11; }
static void __attribute__((destructor)) finalize(void) {
  if (dynamic_trace)
    dynamic_trace("DYNAMIC FINI base\n");
}
int base_value(void) { return base_state; }
__attribute__((weak)) int overridden(void) { return -1000; }
