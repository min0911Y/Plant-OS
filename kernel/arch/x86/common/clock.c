#include <arch.h>
#include <arch/x86/clock.h>
#include <arch/x86/cpuid.h>
#include <dos.h>
#include <irq.h>
#include <platform.h>
#include <smp.h>
#include <stdint.h>

typedef struct {
  uint32_t version, reserved;
  uint64_t tsc_timestamp, system_time;
  uint32_t multiplier;
  int8_t shift;
  uint8_t flags, padding[2];
} pvclock_time_t;
_Static_assert(sizeof(pvclock_time_t) == 32, "KVM clock ABI");

typedef struct {
  uint64_t counter, nanoseconds;
} clock_epoch_t;

enum {
  CLOCK_HAS_TSC = 1,
  CLOCK_TSC_ALIGNED = 2,
  CLOCK_HAS_PVCLOCK = 4,
  CLOCK_PVCLOCK_STABLE = 8,
  CLOCK_READY = 16,
};
typedef struct {
  volatile pvclock_time_t pv;
  unsigned capabilities;
  clock_epoch_t tsc_epoch;
} __attribute__((aligned(64))) task_clock_t;
/* The hypervisor may update these records until its MSR is disabled. They
 * occupy permanently resident kernel RAM, never a task stack or user page. */
static task_clock_t task_clocks[SMP_MAX_CPUS];
typedef enum {
  CLOCK_SOURCE_BOOT,
  CLOCK_SOURCE_PVCLOCK,
  CLOCK_SOURCE_TSC,
  CLOCK_SOURCE_PLATFORM,
} clock_source_t;
static const char *const clock_source_names[] = {
    "boot", "kvm-pvclock", "invariant-tsc", "platform"};
static struct {
  unsigned source, lock;
  clock_epoch_t epoch;
  uint64_t last;
} monotonic_clock;

static uint64_t cpu_tsc_khz_from_cpuid(void) {
  x86_cpuid_t maximum = x86_cpuid(0, 0);
  if (maximum.eax >= 0x15u) {
    x86_cpuid_t frequency = x86_cpuid(0x15u, 0);
    if (frequency.eax && frequency.ebx && frequency.ecx) {
      uint64_t hz = ((uint64_t)frequency.ecx * frequency.ebx) / frequency.eax;
      if (hz) {
        return hz / 1000ull;
      }
    }
  }
  return 0;
}

static uint64_t cpu_tsc_khz_from_hpet(void) {
  if (!hpet_available()) {
    return 0;
  }

  uint64_t start_ns = platform_monotonic_time_ns();
  uint64_t start_tsc = x86_tsc_read();
  uint64_t now_ns;
  do {
    now_ns = platform_monotonic_time_ns();
  } while (now_ns - start_ns < 1000000ull);
  uint64_t delta_tsc = x86_tsc_read() - start_tsc;
  uint64_t delta_ns = now_ns - start_ns;
  if (!delta_tsc || delta_tsc > UINT64_MAX / 1000000ull)
    return 0;
  return delta_tsc * 1000000ull / delta_ns;
}

uint64_t x86_tsc_frequency_khz(void) {
  /* BSP initializes this before releasing secondary schedulers. A failed
   * calibration is cached too, so APs never race to change the frequency. */
  static bool initialized;
  static uint64_t frequency;
  if (!initialized) {
    if (x86_cpuid(1, 0).edx & (1u << 4)) {
      frequency = cpu_tsc_khz_from_cpuid();
      if (!frequency)
        frequency = cpu_tsc_khz_from_hpet();
    }
    initialized = true;
  }
  return frequency;
}

static uint64_t task_clock_tsc(void) {
  uint32_t low, high, auxiliary;
  asm volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(auxiliary) : : "memory");
  return ((uint64_t)high << 32) | low;
}

static bool pvclock_read(const volatile pvclock_time_t *pv, bool require_stable,
                         uint64_t *result) {
  for (;;) {
    uint32_t version = __atomic_load_n(&pv->version, __ATOMIC_ACQUIRE);
    if (version & 1)
      continue;
    uint64_t origin = pv->tsc_timestamp;
    uint64_t base = pv->system_time;
    uint32_t multiplier = pv->multiplier;
    int shift = pv->shift;
    uint8_t flags = pv->flags;
    uint64_t delta = task_clock_tsc() - origin;
    /* Also order the timestamp before the final version check on i386, where
     * the kernel must not use SSE fences. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (version != __atomic_load_n(&pv->version, __ATOMIC_RELAXED))
      continue;
    if (require_stable && !(flags & 1))
      return false;
    if (shift < -63 || shift > 63 || !multiplier ||
        (shift > 0 && delta > (UINT64_MAX >> shift))) {
      return false;
    }
    delta = shift < 0 ? delta >> -shift : delta << shift;
    /* (delta * multiplier) >> 32, without requiring a 128-bit integer ABI. */
    uint64_t elapsed = (delta >> 32) * multiplier +
                       (((uint64_t)(uint32_t)delta * multiplier) >> 32);
    if (elapsed > UINT64_MAX - base)
      return false;
    *result = base + elapsed;
    return true;
  }
}

static bool tsc_nanoseconds(uint64_t ticks, uint64_t *result) {
  uint64_t khz = x86_tsc_frequency_khz();
  if (!khz || khz > UINT64_MAX / 1000000ull)
    return false;
  uint64_t whole = ticks / khz, fraction = ticks % khz * 1000000ull / khz;
  if (whole > (UINT64_MAX - fraction) / 1000000ull)
    return false;
  *result = whole * 1000000ull + fraction;
  return true;
}

/* Align each CPU independently to the common platform epoch. Invariant TSC
 * describes its rate, not its offset relative to another CPU. Bracket both
 * samples and check the common frequency against the reference interval. */
static bool tsc_align(clock_epoch_t *epoch) {
  if (!hpet_available())
    return false;
  uint64_t first_before = platform_monotonic_time_ns();
  uint64_t first_ticks = task_clock_tsc();
  uint64_t first_after = platform_monotonic_time_ns();
  uint64_t last_before;
  do {
    last_before = platform_monotonic_time_ns();
  } while (last_before - first_after < 1000000ull);
  uint64_t last_ticks = task_clock_tsc();
  uint64_t last_after = platform_monotonic_time_ns();
  uint64_t elapsed;
  if (last_ticks < first_ticks || first_after < first_before ||
      last_before < first_after || last_after < last_before ||
      !tsc_nanoseconds(last_ticks - first_ticks, &elapsed) ||
      elapsed < last_before - first_after || elapsed > last_after - first_before)
    return false;
  /* Prefer the tighter bracket if the host descheduled a vCPU during a read. */
  if (first_after - first_before < last_after - last_before)
    *epoch = (clock_epoch_t){first_ticks,
                            first_before + (first_after - first_before) / 2};
  else
    *epoch = (clock_epoch_t){last_ticks,
                            last_before + (last_after - last_before) / 2};
  return true;
}

static bool clock_read(const task_clock_t *clock, clock_source_t source,
                        uint64_t *result) {
  if (source == CLOCK_SOURCE_PVCLOCK)
    return (clock->capabilities & CLOCK_PVCLOCK_STABLE) &&
           pvclock_read(&clock->pv, true, result);
  if (source == CLOCK_SOURCE_TSC) {
    if (!(clock->capabilities & CLOCK_TSC_ALIGNED))
      return false;
    uint64_t ticks = task_clock_tsc(), elapsed;
    if (ticks < clock->tsc_epoch.counter ||
        !tsc_nanoseconds(ticks - clock->tsc_epoch.counter, &elapsed) ||
        elapsed > UINT64_MAX - clock->tsc_epoch.nanoseconds)
      return false;
    *result = clock->tsc_epoch.nanoseconds + elapsed;
    return true;
  }
  *result = platform_monotonic_time_ns();
  return true;
}

uint64_t monotonic_time_ns(void) {
  if (__atomic_load_n(&monotonic_clock.source, __ATOMIC_ACQUIRE) ==
      CLOCK_SOURCE_BOOT)
    return platform_monotonic_time_ns();

  irq_state_t state = irq_save();
  while (__atomic_exchange_n(&monotonic_clock.lock, 1, __ATOMIC_ACQUIRE))
    arch_cpu_relax();
  task_clock_t *clock = &task_clocks[smp_current_cpu()];
  clock_source_t source = monotonic_clock.source, previous = source;
  uint64_t counter, now;
  if (!(clock->capabilities & CLOCK_READY)) {
    /* Secondary startup has not published its calibrated counters yet. */
    now = platform_monotonic_time_ns();
  } else if (clock_read(clock, source, &counter) &&
             counter >= monotonic_clock.epoch.counter &&
             counter - monotonic_clock.epoch.counter <=
                 UINT64_MAX - monotonic_clock.epoch.nanoseconds) {
    now = monotonic_clock.epoch.nanoseconds +
          (counter - monotonic_clock.epoch.counter);
  } else {
    /* Fall through the same priority order at boot and at runtime. Rebase
     * onto the last published time so a failed source cannot freeze time. */
    do {
      if (source < CLOCK_SOURCE_PLATFORM)
        source++;
    } while (!clock_read(clock, source, &counter));
    __atomic_store_n(&monotonic_clock.source, source, __ATOMIC_RELAXED);
    monotonic_clock.epoch = (clock_epoch_t){counter, monotonic_clock.last};
    now = monotonic_clock.last;
  }
  if (now > monotonic_clock.last)
    monotonic_clock.last = now;
  now = monotonic_clock.last;
  __atomic_store_n(&monotonic_clock.lock, 0, __ATOMIC_RELEASE);
  irq_restore(state);
  if (source != previous)
    logk("clock: monotonic fallback=%s\n", clock_source_names[source]);
  return now;
}

void arch_task_clock_init(uint32_t cpu) {
  uint64_t tsc_khz = x86_tsc_frequency_khz();
  task_clock_t *clock = &task_clocks[cpu];
  clock->capabilities = 0;
  uint32_t extended = x86_cpuid(0x80000000u, 0).eax;
  bool rdtscp = extended >= 0x80000001u &&
                (x86_cpuid(0x80000001u, 0).edx & (1u << 27));
  if (rdtscp && tsc_khz && tsc_khz <= UINT64_MAX / 1000000ull &&
      extended >= 0x80000007u &&
      (x86_cpuid(0x80000007u, 0).edx & (1u << 8)))
    clock->capabilities |= CLOCK_HAS_TSC;
  if ((clock->capabilities & CLOCK_HAS_TSC) && tsc_align(&clock->tsc_epoch))
    clock->capabilities |= CLOCK_TSC_ALIGNED;

  /* Scheduling can use the CPU-local record without a stable-clock flag.
   * The global clock additionally requires CPUID bit 24 and pvclock flag 0. */
  if (rdtscp && (x86_cpuid(1, 0).ecx & (1u << 31))) {
    x86_cpuid_t hypervisor = x86_cpuid(0x40000000u, 0);
    uint64_t physical;
    if (hypervisor.eax >= 0x40000001u &&
        !memcmp(&hypervisor.ebx, "KVMKVMKVM\0\0", 12) &&
        (x86_cpuid(0x40000001u, 0).eax & (1u << 3)) &&
        arch_dma_map((const void *)&clock->pv, sizeof(clock->pv), &physical)) {
      physical |= 1;
      asm volatile("wrmsr" : : "c"(0x4b564d01u), "a"((uint32_t)physical),
                   "d"((uint32_t)(physical >> 32)) : "memory");
      if (clock->pv.version && clock->pv.multiplier) {
        clock->capabilities |= CLOCK_HAS_PVCLOCK;
        if (x86_cpuid(0x40000001u, 0).eax & (1u << 24))
          clock->capabilities |= CLOCK_PVCLOCK_STABLE;
      } else
        asm volatile("wrmsr" : : "c"(0x4b564d01u), "a"(0), "d"(0) : "memory");
    }
  }
  clock->capabilities |= CLOCK_READY;
  if (cpu == 0) {
    clock_source_t task_source = clock->capabilities & CLOCK_HAS_PVCLOCK
                                    ? CLOCK_SOURCE_PVCLOCK
                                : clock->capabilities & CLOCK_HAS_TSC
                                    ? CLOCK_SOURCE_TSC : CLOCK_SOURCE_PLATFORM;
    logk("sched: clock=%s\n", clock_source_names[task_source]);
    for (clock_source_t source = CLOCK_SOURCE_PVCLOCK;
         source <= CLOCK_SOURCE_PLATFORM; source++) {
      uint64_t before, after;
      if (!clock_read(clock, source, &before))
        continue;
      uint64_t reference = platform_monotonic_time_ns();
      if (!clock_read(clock, source, &after) || after < before)
        continue;
      monotonic_clock.epoch =
          (clock_epoch_t){before + (after - before) / 2, reference};
      monotonic_clock.last = reference;
      __atomic_store_n(&monotonic_clock.source, source, __ATOMIC_RELEASE);
      logk("clock: monotonic=%s platform epoch retained\n",
           clock_source_names[source]);
      break;
    }
  }
}

uint64_t arch_task_clock_ns(uint32_t cpu) {
  task_clock_t *clock = &task_clocks[cpu];
  uint64_t now;
  if (clock->capabilities & CLOCK_HAS_PVCLOCK) {
    if (pvclock_read(&clock->pv, false, &now))
      return now;
    Panic_K("invalid KVM clock conversion");
    arch_halt();
  }
  if (clock->capabilities & CLOCK_HAS_TSC) {
    if (tsc_nanoseconds(task_clock_tsc(), &now))
      return now;
    Panic_K("invalid TSC clock conversion");
    arch_halt();
  }
  return monotonic_time_ns();
}
