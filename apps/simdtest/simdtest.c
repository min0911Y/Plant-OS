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

static int probe(unsigned seed, int snapshot) {
  uint64_t expected[32] __attribute__((aligned(16)));
  uint64_t observed[32] __attribute__((aligned(16)));
  for (unsigned i = 0; i < 32; i++) {
    expected[i] =
        ((uint64_t)seed << 48) ^ (0x123456789abcdef0ull + i * 0x100010001ull);
  }
  uint32_t control = 0x1f80 | (seed & 3) << 13;
  uint32_t observed_control = 0;
  int child = snapshot ? simd_fork_probe(expected, observed, 1, control,
                                         &observed_control)
                       : simd_yield_probe(expected, observed, 400, control,
                                          &observed_control);
  int valid = !memcmp(expected, observed, sizeof(expected)) &&
              observed_control == control;
  if (snapshot && child == 0)
    _exit(valid ? 0 : 1);
  if (snapshot)
    valid &= child > 0 && waittid(child) == 0;
  return valid;
}

int main(void) {
  unsigned workers = cpu_count() * 2 + 1;
  unsigned *children = malloc(workers * sizeof(*children));
  if (!children)
    return 1;
  unsigned created = 0;
  int failed = !probe(0x55, 1);
  for (unsigned i = 0; i < workers; i++) {
    int child = fork();
    if (child < 0) {
      failed = 1;
      break;
    }
    if (!child)
      _exit(probe(i + 1, 0) ? 0 : 1);
    children[created++] = child;
  }
  for (unsigned i = 0; i < created; i++)
    failed |= waittid(children[i]) != 0;
  free(children);
  logkf("SIMDTEST %s cpus=%u workers=%u xmm=16 mxcsr=fork/yield\n",
        failed ? "FAIL" : "PASS", cpu_count(), created);
  return failed;
}
