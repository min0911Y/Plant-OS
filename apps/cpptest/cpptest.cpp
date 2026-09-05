#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

static unsigned state;
static unsigned order;
static unsigned count;
static unsigned object_status;
static unsigned local_constructions, local_destructions;
struct Local {
  Local() { local_constructions++; }
  ~Local() { local_destructions++; }
};
static Local &local() {
  static Local instance;
  return instance;
}
static void counted() { count++; }
extern "C" int __cxa_atexit(void (*)(void *), void *, void *);
static void object_exit(void *argument) { object_status = argument == &state; }
static void first() { order = order * 10 + 1; }
static void second() { order = order * 10 + 2; }

struct Lifetime {
  Lifetime() { state = 41; }
  ~Lifetime() {
    logkf("CPPTEST %s constructors/destructors/atexit\n",
          state == 42 && order == 21 && count == 40 && object_status == 1 &&
                  local_constructions == 1 && local_destructions == 1
              ? "PASS"
              : "FAIL");
  }
};
static Lifetime lifetime;

int main() {
  static_assert(sizeof(void *) == 8, "native pointer ABI");
  int *value = new int(42);
  bool valid = state == 41 && value && *value == 42 && !((uintptr_t)value & 15);
  delete value;
  (void)local();
  (void)local();
  valid &= local_constructions == 1;
  state = 42;
  for (unsigned i = 0; i < 40; i++)
    if (atexit(counted))
      valid = false;
  if (__cxa_atexit(object_exit, &state, NULL))
    valid = false;
  if (atexit(first) || atexit(second))
    valid = false;
  return valid ? 0 : 1;
}
