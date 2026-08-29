#include <stdio.h>
#include <syscall.h>
#include <task.h>

enum {
  FPU_TEST_MAX_WORKERS = 16,
  FPU_TEST_WORKERS_PER_CPU = 3,
  FPU_TEST_ROUNDS = 2000,
};

typedef union {
  double value;
  uint64_t bits;
} fpu_value_t;

int fputest_fork_preserving_fpu(const double *expected, double *actual);
void fputest_yield_preserving_fpu(const double *expected, double *actual);

static int fork_preserving_fpu(unsigned worker, int *state_ok) {
  fpu_value_t expected = {
      .bits = 0x3ff0000000000000ull | ((uint64_t)(worker + 1) << 32),
  };
  fpu_value_t actual;
  int child =
      fputest_fork_preserving_fpu(&expected.value, &actual.value);
  *state_ok = actual.bits == expected.bits;
  return child;
}

static int fpu_survives_yields(unsigned worker, unsigned round,
                               uint64_t *actual_bits,
                               uint64_t *expected_bits) {
  fpu_value_t expected = {
      .bits = 0x3ff0000000000000ull | ((uint64_t)(worker + 1) << 32) | round,
  };
  fpu_value_t actual;

  fputest_yield_preserving_fpu(&expected.value, &actual.value);
  *actual_bits = actual.bits;
  *expected_bits = expected.bits;
  return actual.bits == expected.bits;
}

static int run_worker(unsigned worker, int fork_state_ok) {
  uint32_t cpu_mask = 0;
  if (!fork_state_ok) {
    logkf("FPUTEST worker=%d fork state corrupted\n", worker);
    return 1;
  }

  for (unsigned round = 0; round < FPU_TEST_ROUNDS; round++) {
    uint64_t actual;
    uint64_t expected;
    if (!fpu_survives_yields(worker, round, &actual, &expected)) {
      logkf("FPUTEST worker=%d round=%d expected=%08x%08x actual=%08x%08x\n",
            worker, round, (uint32_t)(expected >> 32), (uint32_t)expected,
            (uint32_t)(actual >> 32), (uint32_t)actual);
      return 1;
    }
    unsigned cpu = cpu_current();
    if (cpu < 32) {
      cpu_mask |= 1u << cpu;
    }
  }

  logkf("FPUTEST worker=%d PASS cpu_mask=%08x\n", worker, cpu_mask);
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  unsigned workers = cpu_count() * FPU_TEST_WORKERS_PER_CPU;
  if (workers < 2) {
    workers = 2;
  } else if (workers > FPU_TEST_MAX_WORKERS) {
    workers = FPU_TEST_MAX_WORKERS;
  }

  int children[FPU_TEST_MAX_WORKERS - 1];
  unsigned child_count = 0;
  unsigned worker = 0;
  int fork_state_ok = 1;

  for (unsigned next_worker = 1; next_worker < workers; next_worker++) {
    int state_ok;
    int child = fork_preserving_fpu(next_worker, &state_ok);
    if (!state_ok) {
      fork_state_ok = 0;
    }
    if (child < 0) {
      logkf("FPUTEST fork failed worker=%d\n", next_worker);
      fork_state_ok = 0;
      break;
    }
    if (child == 0) {
      worker = next_worker;
      fork_state_ok = state_ok;
      child_count = 0;
      break;
    }
    children[child_count++] = child;
  }

  int fails = run_worker(worker, fork_state_ok);
  if (worker != 0) {
    return fails;
  }

  for (unsigned i = 0; i < child_count; i++) {
    if (waittid((unsigned)children[i]) != 0) {
      fails++;
    }
  }
  logkf("FPUTEST %s workers=%d fails=%d\n", fails ? "FAIL" : "PASS",
        child_count + 1, fails);
  printf("FPUTEST %s workers=%d fails=%d\n", fails ? "FAIL" : "PASS",
         child_count + 1, fails);
  return fails;
}
