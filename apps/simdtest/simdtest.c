#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <task.h>

int simd_probe(const void *expected, void *observed, unsigned mode,
               uint32_t control, uint32_t *observed_control, unsigned avx);
void simd_signal_handler(int signal);
unsigned simd_signal_avx;
volatile unsigned simd_signal_received;

enum probe_mode { PROBE_YIELD, PROBE_FORK, PROBE_RESET, PROBE_SIGNAL,
                  PROBE_CLEAN = 4, PROBE_SIGNAL_CLEAN = PROBE_SIGNAL | PROBE_CLEAN };

static int probe(unsigned seed, enum probe_mode mode, unsigned avx) {
  uint64_t expected[64] __attribute__((aligned(16)));
  uint64_t observed[67] __attribute__((aligned(16))) = {0};
  for (unsigned i = 0; i < 64; i++) {
    expected[i] =
        ((uint64_t)seed << 48) ^ (0x123456789abcdef0ull + i * 0x100010001ull);
  }
  expected[0] = 0x3ff0000000000000ull | seed;
  uint32_t control = 0x1f80 | (seed & 3) << 13;
  uint32_t observed_control = 0;
  int child = simd_probe(expected, observed, mode, control, &observed_control, avx);
  uint64_t x87_value = expected[0];
  if (mode & PROBE_CLEAN)
    memset(expected + 32, 0, 256);
  if (mode == PROBE_RESET) {
    memset(expected, 0, sizeof(expected));
    control = 0x1f80;
  }
  int valid = !memcmp(expected, observed, avx ? sizeof(expected) : 256) &&
              observed_control == control;
  if ((avx & 2) && ((mode & PROBE_CLEAN) || mode == PROBE_RESET))
    valid &= !(observed[66] & 4);
  /* Ignore reserved/opcode bits in FXSAVE; check FCW, FSW and the abridged tag. */
  valid &= (observed[64] & 0xffffffffffull) ==
           (mode == PROBE_RESET ? 0x37full : 0x803800037full);
  if (mode != PROBE_RESET)
    valid &= observed[65] == x87_value;
  if (mode == PROBE_FORK && child == 0)
    _exit(valid ? 0 : 1);
  if (mode == PROBE_FORK)
    valid &= child > 0 && waittid(child) == 0;
  return valid;
}

int main(int argc, char **argv) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(1), "c"(0));
  unsigned avx = (ecx & (3u << 27)) == (3u << 27);
  if (avx) {
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    avx = (eax & 7) == 7;
    if (avx) {
      __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                       : "a"(0xd), "c"(1));
      if (eax & 4)
        avx |= 2;
    }
  }
  unsigned workers = cpu_count() * 2 + 1;
  unsigned *children = malloc(workers * sizeof(*children));
  if (!children)
    return 1;
  unsigned created = 0;
  int failed = !probe(0x55, PROBE_FORK, avx);
  failed |= !probe(0x66, PROBE_RESET, avx);
  failed |= !probe(0x88, PROBE_CLEAN, avx);
  for (unsigned i = 0; i < workers; i++) {
    int child = fork();
    if (child < 0) {
      failed = 1;
      break;
    }
    if (!child) {
      int valid = probe(i + 1, PROBE_YIELD, avx) && probe(i + 1, PROBE_RESET, avx);
      _exit(valid ? 0 : 1);
    }
    children[created++] = child;
  }
  for (unsigned i = 0; i < created; i++)
    failed |= waittid(children[i]) != 0;
  free(children);
  if (argc == 2 && !strcmp(argv[1], "signal")) {
    simd_signal_avx = avx;
    signal(SIGINT, simd_signal_handler);
    failed |= !probe(0x77, PROBE_SIGNAL, avx) || simd_signal_received != 1;
    simd_signal_received = 0;
    failed |= !probe(0x99, PROBE_SIGNAL_CLEAN, avx) || simd_signal_received != 1;
    logkf("SIMDTEST SIGNAL %s\n", failed ? "FAIL" : "PASS");
  }
  logkf("SIMDTEST %s cpus=%u workers=%u xmm=16 ymm=%u x87/mxcsr=fork/yield/reset/clean\n",
        failed ? "FAIL" : "PASS", cpu_count(), created, avx ? 16 : 0);
  return failed;
}
