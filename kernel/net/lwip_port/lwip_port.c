#include <dos.h>

#include <lwip/arch.h>
#include <lwip/sys.h>

u32_t lwip_port_rand(void) {
  return rand();
}

int lwip_port_atoi(const char *text) {
  return (int)strtol(text, NULL, 10);
}

u32_t sys_now(void) {
  return timerctl.count * 10u;
}

void lwip_port_assert(const char *message) {
  Panic_K("lwIP assertion: %s", message);
}
