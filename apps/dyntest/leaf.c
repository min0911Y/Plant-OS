#include <stdlib.h>
#include <syscall.h>

extern int base_value(void);
extern int overridden(void);
extern int optional_symbol(void) __attribute__((weak));
extern void dynamic_trace(char *) __attribute__((weak));
static int initialized;
int shared_data = 7;
int *relocated_pointer = &shared_data;
int *const relro_pointer = &shared_data;
static void __attribute__((constructor)) initialize(void) {
  int *storage = malloc(sizeof(*storage));
  if (!storage)
    exit(21);
  *storage = base_value();
  initialized = *storage;
  free(storage);
}
static void __attribute__((destructor)) finalize(void) {
  if (dynamic_trace)
    dynamic_trace("DYNAMIC FINI leaf\n");
}
int dynamic_value(void) {
  return initialized + *relocated_pointer + overridden() +
         (optional_symbol ? optional_symbol() : 0);
}
