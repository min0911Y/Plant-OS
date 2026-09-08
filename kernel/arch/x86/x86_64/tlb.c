#include <arch/x86/cpuid.h>
#include <arch/x86/x86_64/cpu.h>
#include <dos.h>
#include <irq.h>
#include <smp.h>
#include <user_space.h>

enum {
  PCID_COUNT = 1 << 12,
  PAGE_BYTES = 4096,
  CR4_PGE = 1 << 7,
  CR4_PCIDE = 1 << 17,
  INVPCID_CONTEXT = 1,
  INVPCID_ALL = 2,
  TLB_PAGE_FLUSH_LIMIT = 32,
};
#define CR3_NOFLUSH (1ull << 63)

/* Direct-mapped hardware tags: collisions evict a slot, never limit the number
 * of address spaces. The kernel lock serializes this cache with page edits. */
static arch_address_space_t (*pcid_roots)[PCID_COUNT];
static arch_address_space_t active_roots[SMP_MAX_CPUS];
/* The kernel lock serializes senders. Receivers update only their hardware,
 * tag and acknowledgement, without waiting for the sender's kernel lock. */
static struct {
  arch_address_space_t root;
  uintptr_t address;
  size_t size;
  uint32_t pending;
} shootdown;
static bool use_pcid, use_invpcid;

static void invpcid(unsigned type, unsigned pcid) {
  const struct {
    uint64_t pcid, address;
  } descriptor = {pcid, 0};
  __asm__ volatile("invpcid %0, %1"
                   :
                   : "m"(descriptor), "r"((uintptr_t)type)
                   : "memory");
}

static void tlb_flush_all(void) {
  if (use_invpcid) {
    invpcid(INVPCID_ALL, 0);
    return;
  }
  uintptr_t control;
  __asm__ volatile("mov %%cr4, %0" : "=r"(control));
  /* Changing PGE flushes every PCID, including global translations, even
   * when PGE was initially clear. Restore the original control bits. */
  __asm__ volatile("mov %0, %%cr4; mov %1, %%cr4"
                   :
                   : "r"(control ^ CR4_PGE), "r"(control)
                   : "memory");
}

void x64_tlb_initialize(void) {
  bool pcid = (x86_cpuid(1, 0).ecx & (1u << 17)) != 0;
  bool invalidate = x86_cpuid(0, 0).eax >= 7 &&
                    (x86_cpuid(7, 0).ebx & (1u << 10));
  if (!smp_current_cpu()) {
    use_pcid = pcid;
    use_invpcid = invalidate;
  }
  if ((use_pcid && !pcid) || (use_invpcid && !invalidate)) {
    Panic_K("CPU lacks the selected TLB backend");
    arch_halt();
  }
  uintptr_t root = arch_address_space_current(), control;
  /* CR3[11:0] must be zero when enabling PCIDE. Discard bootloader tags. */
  __asm__ volatile("mov %1, %%cr3; mov %%cr4, %0"
                   : "=r"(control)
                   : "r"(root)
                   : "memory");
  control = (control & ~CR4_PCIDE) | (use_pcid ? CR4_PCIDE : 0);
  __asm__ volatile("mov %0, %%cr4" : : "r"(control) : "memory");
  tlb_flush_all();
  if (!smp_current_cpu())
    logk("tlb: pcid=%u invpcid=%u\n", use_pcid, use_invpcid);
}

bool x64_tlb_prepare(void) {
  if (use_pcid)
    pcid_roots = page_malloc(smp_cpu_count() * sizeof(*pcid_roots));
  return !use_pcid || pcid_roots != NULL;
}

arch_address_space_t arch_address_space_current(void) {
  uintptr_t value;
  __asm__ volatile("mov %%cr3, %0" : "=r"(value));
  return value & ~(uintptr_t)(PCID_COUNT - 1);
}

arch_address_space_t arch_address_space_kernel(void) { return x64_kernel_cr3; }

void arch_address_space_activate(arch_address_space_t root) {
  unsigned cpu = smp_current_cpu();
  active_roots[cpu] = root;
  uintptr_t current, value = root;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current));
  if (pcid_roots) {
    unsigned pcid = (root >> 12) & (PCID_COUNT - 1);
    value |= pcid;
    if (pcid_roots[cpu][pcid] == root) {
      if (current == value)
        return;
      value |= CR3_NOFLUSH;
    } else {
      pcid_roots[cpu][pcid] = root;
    }
  } else if (current == root) {
    return;
  }
  __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static void tlb_invalidate_local(arch_address_space_t root, uintptr_t address,
                                 size_t size) {
  if (address >= USER_SPACE_END) {
    tlb_flush_all();
    return;
  }
  uintptr_t current;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current));
  if ((current & ~(uintptr_t)(PCID_COUNT - 1)) != root)
    return;
  if (size && size <= TLB_PAGE_FLUSH_LIMIT * PAGE_BYTES) {
    for (size_t offset = 0; offset < size; offset += PAGE_BYTES)
      __asm__ volatile("invlpg (%0)" : : "r"(address + offset) : "memory");
  } else if (use_invpcid) {
    invpcid(INVPCID_CONTEXT, use_pcid ? current & (PCID_COUNT - 1) : 0);
  } else {
    __asm__ volatile("mov %0, %%cr3" : : "r"(current) : "memory");
  }
}

void x64_tlb_poll(void) {
  unsigned cpu = smp_current_cpu();
  uint32_t bit = 1u << cpu;
  if (!(__atomic_load_n(&shootdown.pending, __ATOMIC_ACQUIRE) & bit))
    return;
  /* Called with IRQs disabled, including from the kernel-lock wait loop. */
  tlb_invalidate_local(shootdown.root, shootdown.address, shootdown.size);
  if (pcid_roots && shootdown.address < USER_SPACE_END &&
      arch_address_space_current() == shootdown.root)
    pcid_roots[cpu][(shootdown.root >> 12) & (PCID_COUNT - 1)] = shootdown.root;
  __atomic_fetch_and(&shootdown.pending, ~bit, __ATOMIC_RELEASE);
}

void x64_tlb_invalidate(arch_address_space_t root, uintptr_t address,
                        size_t size) {
  irq_state_t state = irq_save();
  unsigned current_cpu = smp_current_cpu();
  unsigned pcid = (root >> 12) & (PCID_COUNT - 1);
  bool kernel = address >= USER_SPACE_END;
  uint32_t targets = 0;
  for (unsigned cpu = 0; cpu < smp_cpu_count(); cpu++) {
    /* Retired inactive tags are flushed on their next activation. Active
     * roots must acknowledge now, before an old page can be reclaimed. */
    if (!kernel && pcid_roots && pcid_roots[cpu][pcid] == root)
      pcid_roots[cpu][pcid] = 0;
    if (cpu != current_cpu && smp_cpu_online(cpu) &&
        (kernel || active_roots[cpu] == root))
      targets |= 1u << cpu;
  }
  if (targets) {
    shootdown.root = root;
    shootdown.address = address;
    shootdown.size = size;
    __atomic_store_n(&shootdown.pending, targets, __ATOMIC_RELEASE);
    for (unsigned cpu = 0; cpu < smp_cpu_count(); cpu++) {
      if (targets & (1u << cpu))
        apic_send_fixed_ipi(smp_cpu_lapic_id(cpu), X64_TLB_VECTOR);
    }
  }
  tlb_invalidate_local(root, address, size);
  if (!kernel && pcid_roots && active_roots[current_cpu] == root)
    pcid_roots[current_cpu][pcid] = root;
  if (targets) {
    uint64_t deadline = monotonic_time_ns() + 1000000000ull;
    while (__atomic_load_n(&shootdown.pending, __ATOMIC_ACQUIRE)) {
      if (monotonic_time_ns() >= deadline) {
        Panic_K("TLB shootdown acknowledgement timed out");
        arch_halt();
      }
      arch_cpu_relax();
    }
  }
  irq_restore(state);
}
