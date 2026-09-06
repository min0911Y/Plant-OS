#include <syscall.h>

extern int shared_data, dynamic_value(void);
extern int *relocated_pointer;
int overridden(void) { return 24; }
int main(void) {
  if (shared_data != 7 || dynamic_value() != 42 ||
      relocated_pointer != &shared_data)
    return 1;
  shared_data = 29;
  if (dynamic_value() != 64)
    return 2;
  logk("DYNAMIC DATA PASS\n");
  return 0;
}
