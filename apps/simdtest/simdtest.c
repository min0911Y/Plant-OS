#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>

int simd_yield_probe(const void *expected, void *observed, unsigned loops,
                     uint32_t control, uint32_t *observed_control);
int simd_fork_probe(const void *expected, void *observed, unsigned loops,
                    uint32_t control, uint32_t *observed_control);
int simd_reset_probe(const void *expected, void *observed, unsigned loops,
                     uint32_t control, uint32_t *observed_control);

enum probe_mode { PROBE_YIELD, PROBE_FORK, PROBE_RESET };

static int probe(unsigned seed, enum probe_mode mode) {
  uint64_t expected[32] __attribute__((aligned(16)));
  uint64_t observed[34] __attribute__((aligned(16))) = {0};
  for (unsigned i = 0; i < 32; i++) {
    expected[i] =
        ((uint64_t)seed << 48) ^ (0x123456789abcdef0ull + i * 0x100010001ull);
  }
  expected[0] = 0x3ff0000000000000ull | seed;
  uint32_t control = 0x1f80 | (seed & 3) << 13;
  uint32_t observed_control = 0;
  int (*run)(const void *, void *, unsigned, uint32_t, uint32_t *) =
      mode == PROBE_FORK ? simd_fork_probe
      : mode == PROBE_RESET ? simd_reset_probe : simd_yield_probe;
  int child = run(expected, observed, mode == PROBE_FORK ? 1 : 400, control,
                  &observed_control);
  uint64_t x87_value = expected[0];
  if (mode == PROBE_RESET) {
    memset(expected, 0, sizeof(expected));
    control = 0x1f80;
  }
  int valid = !memcmp(expected, observed, sizeof(expected)) &&
              observed_control == control;
  /* Ignore reserved/opcode bits in FXSAVE; check FCW, FSW and the abridged tag. */
  valid &= (observed[32] & 0xffffffffffull) ==
           (mode == PROBE_RESET ? 0x37full : 0x803800037full);
  if (mode != PROBE_RESET)
    valid &= observed[33] == x87_value;
  if (mode == PROBE_FORK && child == 0)
    _exit(valid ? 0 : 1);
  if (mode == PROBE_FORK)
    valid &= child > 0 && waittid(child) == 0;
  return valid;
}

int main(void) {
  unsigned workers = cpu_count() * 2 + 1;
  unsigned *children = malloc(workers * sizeof(*children));
  if (!children)
    return 1;
  unsigned created = 0;
  int failed = !probe(0x55, PROBE_FORK);
  failed |= !probe(0x66, PROBE_RESET);
  for (unsigned i = 0; i < workers; i++) {
    int child = fork();
    if (child < 0) {
      failed = 1;
      break;
    }
    if (!child)
      _exit(probe(i + 1, PROBE_YIELD) && probe(i + 1, PROBE_RESET) ? 0 : 1);
    children[created++] = child;
  }
  for (unsigned i = 0; i < created; i++)
    failed |= waittid(children[i]) != 0;
  free(children);
  logkf("SIMDTEST %s cpus=%u workers=%u xmm=16 x87/mxcsr=fork/yield/reset\n",
        failed ? "FAIL" : "PASS", cpu_count(), created);
  return failed;
}
