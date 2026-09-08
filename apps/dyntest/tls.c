#include <stdint.h>

static _Thread_local struct {
  uint64_t initialized;
  unsigned char zero[257];
} __attribute__((aligned(256))) local = {.initialized = 41};

uint64_t *tls_library_value(void) { return &local.initialized; }
int tls_library_zero(void) {
  for (unsigned i = 0; i < sizeof(local.zero); i++)
    if (local.zero[i])
      return 0;
  local.zero[0] = 1;
  return 1;
}
