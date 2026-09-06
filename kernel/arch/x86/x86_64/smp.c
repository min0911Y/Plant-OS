#include "boot.h"
#include <dos.h>
#include <irq.h>
#include <smp.h>

x64_cpu_t x64_cpus[SMP_MAX_CPUS];
static uint32_t cpu_count = 1, online_count = 1;
static uint32_t kernel_owner;
static enum { APS_PARKED, APS_PENDING, APS_RELEASED } secondary_state;

uint32_t smp_current_cpu(void) {
  uint32_t index;
  __asm__ volatile("movl %%gs:16, %0" : "=r"(index));
  return index;
}
void smp_topology_init(void) {
  struct limine_mp_response *mp = x64_mp_request.response;
  x64_cpus[0].lapic_id = apic_current_id();
  x64_cpus[0].online = 1;
  if (!mp)
    return;
  logk("smp: Limine handoff mode=%s cpus=%llu bsp=%u\n",
       mp->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC ? "x2apic" : "xapic",
       (unsigned long long)mp->cpu_count, mp->bsp_lapic_id);
  if (mp->cpu_count > SMP_MAX_CPUS) {
    Panic_K("Limine CPU count exceeds scheduler capacity");
    arch_halt();
  }
  for (size_t i = 0; i < mp->cpu_count; i++) {
    struct limine_mp_info *cpu = mp->cpus[i];
    if (cpu->lapic_id == mp->bsp_lapic_id)
      continue;
    x64_cpus[cpu_count].index = cpu_count;
    x64_cpus[cpu_count].lapic_id = cpu->lapic_id;
    cpu->extra_argument = cpu_count++;
  }
}

static __attribute__((noreturn)) void
secondary_entry(struct limine_mp_info *info) {
  __asm__ volatile("cli; cld" : : : "memory");
  x64_cpu_t *cpu = &x64_cpus[info->extra_argument];
  x64_cpu_initialize(cpu);
  arch_address_space_activate(x64_kernel_cr3);
  arch_interrupt_init_secondary();
  apic_init_secondary();
  __atomic_store_n(&cpu->online, 1, __ATOMIC_RELEASE);
  __atomic_add_fetch(&online_count, 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&secondary_state, __ATOMIC_ACQUIRE) != APS_RELEASED) {
    __asm__ volatile("sti; hlt; cli" : : : "memory");
  }
  apic_timer_init_secondary();
  scheduler_start_secondary(cpu->index);
}

void smp_start_aps(void) {
  if (!x64_tlb_prepare()) {
    Panic_K("unable to allocate PCID contexts");
    arch_halt();
  }
  struct limine_mp_response *mp = x64_mp_request.response;
  if (!mp || !apic_ready())
    return;
  for (size_t i = 0; i < mp->cpu_count; i++) {
    struct limine_mp_info *cpu = mp->cpus[i];
    if (cpu->lapic_id == mp->bsp_lapic_id)
      continue;
    __atomic_store_n(&cpu->goto_address, secondary_entry, __ATOMIC_RELEASE);
  }
  uint64_t deadline = monotonic_time_ns() + 1000000000ull;
  while (__atomic_load_n(&online_count, __ATOMIC_ACQUIRE) != cpu_count &&
         monotonic_time_ns() < deadline)
    arch_cpu_relax();
  if (online_count != cpu_count) {
    Panic_K("Limine AP startup timed out");
    arch_halt();
  }
}
void smp_request_secondary_release(void) {
  __atomic_store_n(&secondary_state, APS_PENDING, __ATOMIC_RELEASE);
}
uint32_t smp_cpu_count(void) { return cpu_count; }
uint32_t smp_online_cpu_count(void) { return online_count; }
uint32_t smp_cpu_lapic_id(uint32_t cpu) {
  return cpu < cpu_count ? x64_cpus[cpu].lapic_id : 0;
}
int smp_cpu_online(uint32_t cpu) {
  return cpu < cpu_count && x64_cpus[cpu].online;
}
void smp_send_reschedule(uint32_t cpu) {
  if (cpu < cpu_count && cpu != smp_current_cpu() && x64_cpus[cpu].online) {
    apic_send_fixed_ipi(x64_cpus[cpu].lapic_id, 0xf0);
  }
}

void kernel_lock_enter(void) {
  uint32_t cpu = smp_current_cpu();
  if (__atomic_load_n(&kernel_owner, __ATOMIC_RELAXED) == cpu + 1) {
    x64_cpus[cpu].lock_depth++;
    return;
  }
  for (;;) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&kernel_owner, &expected, cpu + 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      break;
    while (__atomic_load_n(&kernel_owner, __ATOMIC_RELAXED))
      arch_cpu_relax();
  }
  x64_cpus[cpu].lock_depth = 1;
}
void kernel_lock_leave(void) {
  (void)irq_save();
  uint32_t cpu = smp_current_cpu();
  if (kernel_owner != cpu + 1 || !x64_cpus[cpu].lock_depth)
    arch_halt();
  if (x64_cpus[cpu].lock_depth == 1)
    scheduler_preempt_if_needed();
  cpu = smp_current_cpu();
  if (--x64_cpus[cpu].lock_depth)
    return;
  __atomic_store_n(&kernel_owner, 0, __ATOMIC_RELEASE);
  if (secondary_state == APS_PENDING) {
    __atomic_store_n(&secondary_state, APS_RELEASED, __ATOMIC_RELEASE);
    for (uint32_t target = 1; target < cpu_count; target++) {
      apic_send_fixed_ipi(x64_cpus[target].lapic_id, 0xf1);
    }
  }
}
uint32_t kernel_lock_depth(void) {
  return x64_cpus[smp_current_cpu()].lock_depth;
}
