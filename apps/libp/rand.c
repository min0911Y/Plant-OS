#include <rand.h>
#include <time.h>

uint32_t os_random32(void) {
  static uint64_t state __attribute__((aligned(8)));
  uint64_t previous = __atomic_load_n(&state, __ATOMIC_RELAXED), value_state;
  do {
    value_state =
        previous ? previous : (monotonic_ns() ^ (uintptr_t)&state) | 1;
  } while (!__atomic_compare_exchange_n(
      &state, &previous,
      value_state * 6364136223846793005ull + 1442695040888963407ull, true,
      __ATOMIC_RELAXED, __ATOMIC_RELAXED));
  previous = value_state;
  uint32_t value = (uint32_t)(((previous >> 18) ^ previous) >> 27);
  unsigned rotation = previous >> 59;
  return (value >> rotation) | (value << ((0u - rotation) & 31));
}
static unsigned long next1 = 1;
int rand(void)
{
    next1 = next1 * 1103515245 + 12345;
    return ((unsigned)(next1 / 65536) % 32768);
}

void srand(unsigned seed)
{
    next1 = seed;
}
