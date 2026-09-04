#include <arch.h>
#include <arch/x86/i386/control.h>
#include <arch/x86/i386/interrupt.h>
#include <arch/x86/i386/memory.h>
#include <dos.h>
#include <irq.h>
#include <smp.h>

#define SMP_TRAMPOLINE_PHYS 0x6000u
#define SMP_BOOT_STACK_SIZE 4096u

typedef struct {
  uint32_t lapic_id;
  volatile uint32_t online;
} smp_cpu_t;

typedef enum {
  SMP_SECONDARIES_PARKED,
  SMP_SECONDARIES_RELEASE_PENDING,
  SMP_SECONDARIES_RELEASED,
} smp_secondary_state_t;

static smp_cpu_t smp_cpus[SMP_MAX_CPUS];
static uint8_t smp_boot_stacks[SMP_MAX_CPUS][SMP_BOOT_STACK_SIZE]
    __attribute__((aligned(16)));
static uint32_t smp_cpu_total = 1;
static volatile uint32_t smp_online_total = 1;
static volatile smp_secondary_state_t smp_secondary_state;
static volatile uint32_t kernel_lock_owner;
static uint32_t kernel_lock_nesting[SMP_MAX_CPUS];

extern uint8_t x86_smp_trampoline_start[];
extern uint8_t x86_smp_trampoline_end[];
extern uint8_t x86_smp_trampoline_stack[];
extern uint8_t x86_smp_trampoline_cr3[];
extern uint8_t x86_smp_trampoline_cpu[];
extern uint8_t x86_smp_trampoline_entry[];

static __attribute__((noreturn)) void x86_smp_secondary_entry(uint32_t cpu);

static void smp_trampoline_write(uint8_t *symbol, uint32_t value) {
  uintptr_t offset = (uintptr_t)symbol - (uintptr_t)x86_smp_trampoline_start;
  *(uint32_t *)(uintptr_t)(SMP_TRAMPOLINE_PHYS + offset) = value;
}

uint32_t smp_current_cpu(void) {
  if (!apic_ready()) {
    return 0;
  }
  uint32_t lapic_id = apic_current_id();
  for (uint32_t cpu = 0; cpu < smp_cpu_total; cpu++) {
    if (smp_cpus[cpu].lapic_id == lapic_id) {
      return cpu;
    }
  }
  return 0;
}

void smp_topology_init(void) {
  uint32_t bsp_id = apic_current_id();
  smp_cpus[0].lapic_id = bsp_id;
  smp_cpus[0].online = 1;
  smp_cpu_total = 1;
  smp_online_total = 1;

  if (!apic_ready()) {
    return;
  }
  for (uint32_t index = 0;
       index < acpi_cpu_count() && smp_cpu_total < SMP_MAX_CPUS; index++) {
    uint32_t lapic_id = acpi_cpu_lapic_id(index);
    if (!acpi_cpu_usable(index) || lapic_id == bsp_id) {
      continue;
    }
    smp_cpus[smp_cpu_total].lapic_id = lapic_id;
    smp_cpus[smp_cpu_total].online = 0;
    smp_cpu_total++;
  }
}

static void smp_start_cpu(uint32_t cpu) {
  uint32_t trampoline_size =
      (uint32_t)(x86_smp_trampoline_end - x86_smp_trampoline_start);
  memcpy((void *)(uintptr_t)SMP_TRAMPOLINE_PHYS, x86_smp_trampoline_start,
         trampoline_size);
  smp_trampoline_write(x86_smp_trampoline_stack,
                       (uintptr_t)&smp_boot_stacks[cpu][SMP_BOOT_STACK_SIZE]);
  smp_trampoline_write(x86_smp_trampoline_cr3,
                       I386_KERNEL_PAGE_DIRECTORY);
  smp_trampoline_write(x86_smp_trampoline_cpu, cpu);
  smp_trampoline_write(x86_smp_trampoline_entry,
                       (uintptr_t)x86_smp_secondary_entry);

  uint32_t lapic_id = smp_cpus[cpu].lapic_id;
  logk("smp: starting cpu=%d lapic=%d\n", cpu, lapic_id);
  apic_send_init_ipi(lapic_id);
  usleep(10000000ull);
  apic_send_startup_ipi(lapic_id, SMP_TRAMPOLINE_PHYS >> 12);
  usleep(200000ull);
  apic_send_startup_ipi(lapic_id, SMP_TRAMPOLINE_PHYS >> 12);

  uint64_t deadline = monotonic_time_ns() + 100000000ull;
  while (!smp_cpus[cpu].online && monotonic_time_ns() < deadline) {
    asm volatile("pause");
  }
  if (!smp_cpus[cpu].online) {
    logk("smp: cpu=%d lapic=%d failed to start\n", cpu, lapic_id);
  }
}

void smp_start_aps(void) {
  if (smp_cpu_total <= 1) {
    return;
  }
  if ((uintptr_t)(x86_smp_trampoline_end - x86_smp_trampoline_start) >
      0x1000u) {
    Panic_K("SMP trampoline exceeds one page");
    return;
  }
  smp_secondary_state = SMP_SECONDARIES_PARKED;
  for (uint32_t cpu = 1; cpu < smp_cpu_total; cpu++) {
    smp_start_cpu(cpu);
  }
  logk("smp: discovered=%d online=%d\n", smp_cpu_total, smp_online_total);
}

static __attribute__((noreturn)) void x86_smp_secondary_entry(uint32_t cpu) {
  if (cpu == 0 || cpu >= smp_cpu_total) {
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  arch_interrupt_init_secondary();
  apic_init_secondary();
  arch_task_state_init();
  arch_fpu_init_cpu();

  if (__sync_bool_compare_and_swap(&smp_cpus[cpu].online, 0, 1)) {
    __sync_fetch_and_add(&smp_online_total, 1);
  }
  while (smp_secondary_state != SMP_SECONDARIES_RELEASED) {
    asm volatile("sti; hlt; cli" ::: "memory");
  }
  __sync_synchronize();
  apic_timer_init_secondary();
  scheduler_start_secondary(cpu);
}

void smp_request_secondary_release(void) {
  if (smp_cpu_total <= 1 ||
      smp_secondary_state != SMP_SECONDARIES_PARKED) {
    return;
  }
  __sync_synchronize();
  smp_secondary_state = SMP_SECONDARIES_RELEASE_PENDING;
}

uint32_t smp_cpu_count(void) { return smp_cpu_total; }

uint32_t smp_online_cpu_count(void) { return smp_online_total; }

uint32_t smp_cpu_lapic_id(uint32_t cpu) {
  return cpu < smp_cpu_total ? smp_cpus[cpu].lapic_id : 0;
}

int smp_cpu_online(uint32_t cpu) {
  return cpu < smp_cpu_total && smp_cpus[cpu].online != 0;
}

void smp_send_reschedule(uint32_t cpu) {
  if (cpu >= smp_cpu_total || cpu == smp_current_cpu() ||
      !smp_cpus[cpu].online) {
    return;
  }
  apic_send_fixed_ipi(smp_cpus[cpu].lapic_id, X86_VECTOR_RESCHEDULE);
}

static int kernel_lock_try_acquire(uint32_t owner) {
  uint32_t previous = 0;
  asm volatile("lock; cmpxchgl %2, %1"
               : "+a"(previous), "+m"(kernel_lock_owner)
               : "r"(owner)
               : "cc", "memory");
  return previous == 0;
}

void kernel_lock_enter(void) {
  uint32_t cpu = smp_current_cpu();
  uint32_t owner = cpu + 1;
  if (kernel_lock_owner == owner) {
    kernel_lock_nesting[cpu]++;
    return;
  }
  for (;;) {
    while (kernel_lock_owner != 0) {
      asm volatile("pause");
    }
    if (kernel_lock_try_acquire(owner)) {
      break;
    }
  }
  kernel_lock_nesting[cpu] = 1;
}

void kernel_lock_leave(void) {
  asm volatile("cli" ::: "memory");
  uint32_t cpu = smp_current_cpu();
  if (kernel_lock_owner != cpu + 1 || kernel_lock_nesting[cpu] == 0) {
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  if (kernel_lock_nesting[cpu] == 1) {
    scheduler_preempt_if_needed();
  }
  cpu = smp_current_cpu();
  if (kernel_lock_owner != cpu + 1 || kernel_lock_nesting[cpu] == 0) {
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  if (--kernel_lock_nesting[cpu] == 0) {
    asm volatile("" ::: "memory");
    kernel_lock_owner = 0;
    if (smp_secondary_state == SMP_SECONDARIES_RELEASE_PENDING) {
      __sync_synchronize();
      smp_secondary_state = SMP_SECONDARIES_RELEASED;
      __sync_synchronize();
      for (uint32_t target = 1; target < smp_cpu_total; target++) {
        if (smp_cpus[target].online) {
          apic_send_fixed_ipi(smp_cpus[target].lapic_id, X86_VECTOR_SMP_WAKE);
        }
      }
    }
  }
}

uint32_t kernel_lock_depth(void) {
  return kernel_lock_nesting[smp_current_cpu()];
}
