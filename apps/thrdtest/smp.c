#include <errno.h>
#include <futex.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <syscall.h>
#include <task.h>
#include <vm.h>

#if defined(PLANT_ARCH_X86_64)
typedef struct {
  uint32_t generation, acknowledged, stopped, cpu;
  uint32_t *pages;
  unsigned count;
  int protect;
} vm_probe_t;

static void *probe_worker(void *argument) {
  vm_probe_t *probe = argument;
  probe->cpu = cpu_current();
  /* Warm a writable translation, then keep doing user-mode work during the
   * remote edit. A blocking barrier here would only test lazy invalidation on
   * reactivation. The finite instruction budget also bounds failed probes. */
  __atomic_store_n(probe->pages, 1, __ATOMIC_RELAXED);
  __atomic_store_n(&probe->acknowledged, 1, __ATOMIC_RELEASE);
  os_futex_wake(&probe->acknowledged, 1);
  uint32_t seen = 1;
  for (unsigned long i = 0; i < 500000000; i++) {
    if (__atomic_load_n(&probe->stopped, __ATOMIC_ACQUIRE))
      return NULL;
    uint32_t generation = __atomic_load_n(&probe->generation, __ATOMIC_ACQUIRE);
    if (generation == seen)
      continue;
    seen = generation;
    if (probe->protect) {
      __atomic_store_n(probe->pages, 0xbad, __ATOMIC_RELAXED);
      return (void *)1; /* A real protection fault terminates this thread. */
    }
    for (unsigned p = 0; p < probe->count; p++) {
      if (__atomic_load_n(probe->pages + p * (VM_PAGE_SIZE / 4),
                          __ATOMIC_RELAXED) != generation)
        return (void *)2;
    }
    __atomic_store_n(&probe->acknowledged, generation, __ATOMIC_RELEASE);
    os_futex_wake(&probe->acknowledged, 1);
  }
  return (void *)3;
}

static int probe_wait(vm_probe_t *probe, uint32_t generation) {
  uint64_t deadline = monotonic_ns() + 2000000000ull;
  uint32_t value;
  while ((value = __atomic_load_n(&probe->acknowledged, __ATOMIC_ACQUIRE)) !=
         generation) {
    if (os_futex_wait(&probe->acknowledged, value, deadline) == FUTEX_TIMED_OUT)
      return 0;
  }
  return 1;
}

static int probe_mapping(unsigned count, int protect) {
  size_t length = count * VM_PAGE_SIZE;
  vm_probe_t probe = {.generation = 1, .count = count, .protect = protect};
  probe.pages = vm_map(NULL, length);
  if (!probe.pages)
    return 0;
  void *quarantine_address = vm_map(NULL, length);
  if (!quarantine_address) {
    vm_unmap(probe.pages, length);
    return 0;
  }
  pthread_t thread;
  if (pthread_create(&thread, NULL, probe_worker, &probe)) {
    vm_unmap(quarantine_address, length);
    vm_unmap(probe.pages, length);
    return 0;
  }
  int valid = probe_wait(&probe, 1);
  valid &= vm_unmap(quarantine_address, length) == 0;
  /* The scheduler must place a shared-address-space thread on another CPU. */
  valid &= probe.cpu != cpu_current();
  for (uint32_t generation = 2; valid && generation <= (protect ? 2 : 41);
       generation++) {
    if (protect) {
      valid = vm_protect(probe.pages, length, VM_READ) == 0;
    } else {
      valid = vm_unmap(probe.pages, length) == 0;
      /* Occupy retired physical storage so reuse cannot hide a stale TLB. */
      void *quarantine = valid ? vm_map(quarantine_address, length) : NULL;
      void *replacement = quarantine ? vm_map(probe.pages, length) : NULL;
      valid = replacement == probe.pages;
      if (valid) {
        for (unsigned p = 0; p < count; p++)
          probe.pages[p * (VM_PAGE_SIZE / 4)] = generation;
      }
      if (quarantine)
        vm_unmap(quarantine, length);
    }
    if (!valid)
      break;
    __atomic_store_n(&probe.generation, generation, __ATOMIC_RELEASE);
    if (!protect)
      valid = probe_wait(&probe, generation);
  }
  if (!protect || !valid)
    __atomic_store_n(&probe.stopped, 1, __ATOMIC_RELEASE);
  void *result = (void *)4;
  int joined = pthread_join(thread, &result);
  valid &= protect ? joined == EINVAL : joined == 0 && !result;
  if (protect && valid)
    valid = *probe.pages == 1;
  if (!valid)
    logkf("THRDTEST SMP probe pages=%u protect=%d cpu=%u/%u generation=%u "
          "ack=%u result=%u\n",
          count, protect, probe.cpu, cpu_current(), probe.generation,
          probe.acknowledged, (unsigned)(uintptr_t)result);
  vm_unmap(probe.pages, length);
  return valid;
}

static void *exit_worker(void *argument) {
  pthread_barrier_wait(argument);
  for (;;) {
    uint32_t *page = vm_map(NULL, VM_PAGE_SIZE);
    if (!page)
      _Exit(2);
    *page = 42;
    if (vm_unmap(page, VM_PAGE_SIZE))
      _Exit(3);
  }
}

static int probe_group_exit(void) {
  for (unsigned round = 0; round < 8; round++) {
    int child = fork();
    if (!child) {
      pthread_barrier_t start;
      if (pthread_barrier_init(&start, NULL, 5))
        _Exit(1);
      for (unsigned i = 0; i < 4; i++) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, exit_worker, &start))
          _Exit(1);
      }
      pthread_barrier_wait(&start);
      sleep(10);
      _Exit(37);
    }
    if (child < 0 || waittid(child) != 37)
      return 0;
  }
  return 1;
}
#endif

int test_smp_vm(void) {
#if defined(PLANT_ARCH_X86_64)
  if (cpu_count() > 1) {
    int valid = probe_mapping(1, 0) && probe_mapping(64, 0);
    int child = valid ? fork() : -1;
    if (!child)
      _exit(probe_mapping(1, 1) ? 0 : 1);
    valid &= child > 0 && waittid(child) == 0;
    valid &= probe_group_exit();
    logkf("THRDTEST SMP %s remap=1,64 protection=remote\n",
          valid ? "PASS" : "FAIL");
    return valid;
  }
#endif
  return 1;
}
