#include <arch/x86/cpuid.h>
#include <arch/x86/io.h>
#include <dos.h>
#include <limits.h>
#include <smp.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_TSC_DEADLINE_MSR 0x6E0
#define IA32_X2APIC_EOI 0x80B
#define IA32_X2APIC_SVR 0x80F
#define IA32_X2APIC_ICR 0x830

#define APIC_BASE_MSR_BSP (1u << 8)
#define APIC_BASE_MSR_ENABLE (1u << 11)
#define APIC_BASE_MSR_X2APIC (1u << 10)

#define LAPIC_REG_ID 0x020
#define LAPIC_REG_EOI 0x0B0
#define LAPIC_REG_SVR 0x0F0
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_TIMER_INITIAL 0x380
#define LAPIC_REG_TIMER_CURRENT 0x390
#define LAPIC_REG_TIMER_DIVIDE 0x3E0
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310

#define APIC_SVR_ENABLE 0x100
#define APIC_LVT_MASKED 0x00010000u
#define APIC_LVT_TIMER_PERIODIC (1u << 17)
#define APIC_LVT_TIMER_TSC_DEADLINE (2u << 17)

#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_REDIR_BASE 0x10

#define APIC_TIMER_IRQ 0
#define APIC_TIMER_VECTOR IRQ_BASE_VECTOR
#define APIC_SPURIOUS_VECTOR 0xFF
#define APIC_TIMER_HZ 100
#define APIC_TIMER_INTERVAL_NS (1000000000ull / APIC_TIMER_HZ)

#define MAX_IOAPICS 8
#define MAX_IRQS 24

typedef struct {
  uint32_t phys;
  volatile uint32_t* virt;
  uint32_t gsi_base;
  uint32_t gsi_count;
} IoApicState;

static uint8_t pic_irq_masks[2] = {0xfb, 0xff};
static int apic_enabled;
static int x2apic_enabled;
static int apic_tsc_deadline_supported;
static uint8_t apic_tsc_deadline_enabled[SMP_MAX_CPUS];
static uint32_t lapic_base_phys = 0xfee00000u;
static volatile uint32_t* lapic_mmio = (volatile uint32_t*)(uintptr_t)0xfee00000u;
static IoApicState ioapics[MAX_IOAPICS];
static uint32_t ioapic_total;
static uint32_t irq_to_gsi[MAX_IRQS];
static uint8_t irq_trigger[MAX_IRQS];
static uint8_t irq_polarity[MAX_IRQS];
static uint8_t irq_masked[MAX_IRQS];
static uint32_t bsp_lapic_id;
static uint64_t tsc_khz;
static uint64_t apic_timer_deadline_interval_tsc;
static uint64_t apic_timer_next_deadline_tsc[SMP_MAX_CPUS];

static inline uint64_t rdmsr64(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc64(void) {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr64(uint32_t msr, uint64_t value) {
  asm volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value),
               "d"((uint32_t)(value >> 32)));
}

static inline uint32_t lapic_read(uint32_t reg) {
  if (x2apic_enabled) {
    return (uint32_t)rdmsr64(0x800u + (reg >> 4));
  }
  return lapic_mmio[reg >> 2];
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
  if (x2apic_enabled) {
    wrmsr64(0x800u + (reg >> 4), value);
    return;
  }
  lapic_mmio[reg >> 2] = value;
  (void)lapic_mmio[LAPIC_REG_ID >> 2];
}

static inline void lapic_send_ipi(uint32_t apic_id, uint32_t low) {
  if (x2apic_enabled) {
    wrmsr64(IA32_X2APIC_ICR, ((uint64_t)apic_id << 32) | low);
    return;
  }
  lapic_write(LAPIC_REG_ICR_HIGH, apic_id << 24);
  lapic_write(LAPIC_REG_ICR_LOW, low);
}

static void ioapic_write(IoApicState* ioapic, uint32_t reg, uint32_t value) {
  volatile uint32_t* mmio = ioapic->virt;
  mmio[0] = reg;
  mmio[4] = value;
}

static uint32_t ioapic_read(IoApicState* ioapic, uint32_t reg) {
  volatile uint32_t* mmio = ioapic->virt;
  mmio[0] = reg;
  return mmio[4];
}

static IoApicState* ioapic_for_gsi(uint32_t gsi) {
  for (uint32_t i = 0; i < ioapic_total; i++) {
    if (gsi >= ioapics[i].gsi_base &&
        gsi < ioapics[i].gsi_base + ioapics[i].gsi_count) {
      return &ioapics[i];
    }
  }
  return NULL;
}

static void ioapic_program_gsi(uint32_t gsi, uint8_t vector, int masked,
                               int trigger_mode, int polarity) {
  IoApicState* ioapic = ioapic_for_gsi(gsi);
  if (!ioapic) {
    return;
  }

  uint32_t index = gsi - ioapic->gsi_base;
  uint32_t low = vector;
  if (polarity == IRQ_POLARITY_LOW) {
    low |= 1u << 13;
  }
  if (trigger_mode == IRQ_TRIGGER_LEVEL) {
    low |= 1u << 15;
  }
  if (masked) {
    low |= 1u << 16;
  }

  uint32_t redir = IOAPIC_REG_REDIR_BASE + index * 2;
  ioapic_write(ioapic, redir + 1, bsp_lapic_id << 24);
  ioapic_write(ioapic, redir, low);
}

static void ioapic_init_from_acpi(void) {
  ioapic_total = acpi_ioapic_count();
  if (ioapic_total > MAX_IOAPICS) {
    ioapic_total = MAX_IOAPICS;
  }

  for (uint32_t i = 0; i < ioapic_total; i++) {
    ioapics[i].phys = acpi_ioapic_address(i);
    ioapics[i].gsi_base = acpi_ioapic_gsi_base(i);
    ioapics[i].virt = (volatile uint32_t*)(uintptr_t)ioapics[i].phys;
    uint32_t ver = ioapic_read(&ioapics[i], IOAPIC_REG_VER);
    ioapics[i].gsi_count = ((ver >> 16) & 0xff) + 1;
  }

  for (uint32_t irq = 0; irq < MAX_IRQS; irq++) {
    irq_to_gsi[irq] =
        irq < 16 ? acpi_irq_to_gsi(irq) : irq;
    irq_trigger[irq] =
        irq < 16 ? acpi_irq_trigger_mode(irq) : IRQ_TRIGGER_LEVEL;
    irq_polarity[irq] =
        irq < 16 ? acpi_irq_polarity(irq) : IRQ_POLARITY_LOW;
    irq_masked[irq] = 1;
  }

  for (uint32_t irq = 0; irq < MAX_IRQS; irq++) {
    ioapic_program_gsi(irq_to_gsi[irq], IRQ_BASE_VECTOR + irq, 1,
                       irq_trigger[irq], irq_polarity[irq]);
  }
}

void init_pic(void) {
  x86_port_write8(PIC0_IMR, 0xff);
  x86_io_wait();
  x86_port_write8(PIC1_IMR, 0xff);
  x86_io_wait();
  x86_port_write8(PIC0_ICW1, 0x11);
  x86_io_wait();
  x86_port_write8(PIC0_ICW2, IRQ_BASE_VECTOR);
  x86_io_wait();
  x86_port_write8(PIC0_ICW3, 1 << 2);
  x86_io_wait();
  x86_port_write8(PIC0_ICW4, 0x01);
  x86_io_wait();

  x86_port_write8(PIC1_ICW1, 0x11);
  x86_io_wait();
  x86_port_write8(PIC1_ICW2, IRQ_BASE_VECTOR + 8);
  x86_io_wait();
  x86_port_write8(PIC1_ICW3, 2);
  x86_io_wait();
  x86_port_write8(PIC1_ICW4, 0x01);
  x86_io_wait();

  x86_port_write8(PIC0_IMR, pic_irq_masks[0]);
  x86_port_write8(PIC1_IMR, pic_irq_masks[1]);
}

void pic_disable(void) {
  pic_irq_masks[0] = 0xff;
  pic_irq_masks[1] = 0xff;
  x86_port_write8(PIC0_IMR, 0xff);
  x86_port_write8(PIC1_IMR, 0xff);
}

static void pic_mask(unsigned irq, int masked) {
  if (irq >= 16) {
    return;
  }
  uint16_t port;
  uint8_t bit;
  if (irq < 8) {
    port = PIC0_IMR;
    bit = irq;
  } else {
    port = PIC1_IMR;
    bit = irq - 8;
  }

  uint8_t* mask = irq < 8 ? &pic_irq_masks[0] : &pic_irq_masks[1];
  if (masked) {
    *mask |= 1u << bit;
  } else {
    *mask &= ~(1u << bit);
  }
  x86_port_write8(port, *mask);
}

bool irq_is_valid(unsigned irq) { return irq < MAX_IRQS; }

void send_eoi(int irq) {
  if (apic_enabled) {
    apic_send_eoi();
    return;
  }

  if (irq >= 8) {
    x86_port_write8(PIC1_OCW2, 0x60 | (irq - 8));
    x86_port_write8(PIC0_OCW2, 0x60 | 2);
  } else {
    x86_port_write8(PIC0_OCW2, 0x60 | irq);
  }
}

void irq_mask_clear(unsigned irq) {
  if (!irq_is_valid(irq)) {
    return;
  }
  if (apic_enabled) {
    apic_unmask_irq(irq);
    return;
  }
  pic_mask(irq, 0);
}

void irq_mask_set(unsigned irq) {
  if (!irq_is_valid(irq)) {
    return;
  }
  if (apic_enabled) {
    apic_mask_irq(irq);
    return;
  }
  pic_mask(irq, 1);
}

void irq_configure(unsigned irq, int trigger_mode, int polarity) {
  if (!irq_is_valid(irq)) {
    return;
  }

  irq_trigger[irq] = (uint8_t)trigger_mode;
  irq_polarity[irq] = (uint8_t)polarity;
  if (apic_enabled) {
    apic_configure_irq(irq, trigger_mode, polarity);
  }
}

int interrupt_controller_uses_apic(void) { return apic_enabled; }

static int cpu_has_apic(void) {
  return (x86_cpuid(1, 0).edx & (1u << 9)) != 0;
}

static int cpu_has_x2apic(void) {
  return (x86_cpuid(1, 0).ecx & (1u << 21)) != 0;
}

static int cpu_has_tsc_deadline(void) {
  return (x86_cpuid(1, 0).ecx & (1u << 24)) != 0;
}

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
  if (maximum.eax >= 0x16u) {
    x86_cpuid_t frequency = x86_cpuid(0x16u, 0);
    if (frequency.eax) {
      return (uint64_t)frequency.eax * 1000ull;
    }
  }
  return 0;
}

static uint64_t cpu_tsc_khz_from_hpet(void) {
  if (!hpet_available()) {
    return 0;
  }

  uint64_t start_tsc = rdtsc64();
  uint64_t start_ns = monotonic_time_ns();
  uint64_t target_ns = start_ns + 1000000ull;
  while (monotonic_time_ns() < target_ns) {
  }
  uint64_t delta_tsc = rdtsc64() - start_tsc;
  if (!delta_tsc) {
    return 0;
  }
  return delta_tsc;
}

static uint64_t cpu_detect_tsc_khz(void) {
  uint64_t khz = cpu_tsc_khz_from_cpuid();
  if (khz) {
    return khz;
  }
  return cpu_tsc_khz_from_hpet();
}

static void apic_timer_arm_next_deadline(void) {
  uint32_t cpu = smp_current_cpu();
  if (!apic_tsc_deadline_enabled[cpu] ||
      !apic_timer_deadline_interval_tsc) {
    return;
  }

  uint64_t now = rdtsc64();
  uint64_t next = apic_timer_next_deadline_tsc[cpu];
  if (!next || next <= now) {
    next = now + apic_timer_deadline_interval_tsc;
  } else {
    next += apic_timer_deadline_interval_tsc;
    if (next <= now) {
      next = now + apic_timer_deadline_interval_tsc;
    }
  }
  apic_timer_next_deadline_tsc[cpu] = next;
  wrmsr64(IA32_TSC_DEADLINE_MSR, next);
}

static void apic_timer_disable_tsc_deadline(void) {
  uint32_t cpu = smp_current_cpu();
  if (!apic_tsc_deadline_enabled[cpu]) {
    return;
  }

  wrmsr64(IA32_TSC_DEADLINE_MSR, 0);
  lapic_write(LAPIC_REG_LVT_TIMER, APIC_TIMER_VECTOR | APIC_LVT_MASKED);
  apic_tsc_deadline_enabled[cpu] = 0;
  apic_timer_next_deadline_tsc[cpu] = 0;
}

static void apic_timer_init_tsc_deadline(void) {
  uint32_t cpu = smp_current_cpu();
  apic_tsc_deadline_enabled[cpu] = 0;
  apic_timer_next_deadline_tsc[cpu] = 0;

  if (!apic_enabled || !apic_tsc_deadline_supported) {
    return;
  }

  if (!tsc_khz) {
    tsc_khz = cpu_detect_tsc_khz();
  }
  if (!tsc_khz) {
    logk("apic: TSC frequency unavailable, keep PIT timer\n");
    return;
  }

  apic_timer_deadline_interval_tsc =
      (tsc_khz * APIC_TIMER_INTERVAL_NS) / 1000000ull;
  if (!apic_timer_deadline_interval_tsc) {
    logk("apic: TSC deadline interval computed as zero, keep PIT timer\n");
    return;
  }

  wrmsr64(IA32_TSC_DEADLINE_MSR, 0);
  lapic_write(LAPIC_REG_TIMER_DIVIDE, 0);
  lapic_write(LAPIC_REG_LVT_TIMER,
              APIC_TIMER_VECTOR | APIC_LVT_TIMER_TSC_DEADLINE);
  apic_tsc_deadline_enabled[cpu] = 1;
  apic_timer_arm_next_deadline();
  logk("apic: timer=tsc-deadline tsc_khz=%d tick_tsc=%d\n", (uint32_t)tsc_khz,
       (uint32_t)apic_timer_deadline_interval_tsc);
}

static uint32_t cpu_initial_apic_id(void) {
  return (x86_cpuid(1, 0).ebx >> 24) & 0xff;
}

void apic_init(void) {
  apic_enabled = 0;
  x2apic_enabled = 0;
  apic_tsc_deadline_supported = 0;
  memset(apic_tsc_deadline_enabled, 0, sizeof(apic_tsc_deadline_enabled));
  tsc_khz = 0;
  apic_timer_deadline_interval_tsc = 0;
  memset(apic_timer_next_deadline_tsc, 0,
         sizeof(apic_timer_next_deadline_tsc));

  if (!cpu_has_apic()) {
    logk("apic: CPU does not support APIC, falling back to PIC\n");
    return;
  }

  apic_tsc_deadline_supported = cpu_has_tsc_deadline();

  lapic_base_phys = acpi_lapic_address();
  if (!lapic_base_phys) {
    lapic_base_phys = 0xfee00000u;
  }
  lapic_mmio = (volatile uint32_t*)(uintptr_t)lapic_base_phys;

  uint64_t apic_base = rdmsr64(IA32_APIC_BASE_MSR);
  apic_base |= APIC_BASE_MSR_ENABLE;
  if (cpu_has_x2apic()) {
    apic_base |= APIC_BASE_MSR_X2APIC;
    x2apic_enabled = 1;
  }
  wrmsr64(IA32_APIC_BASE_MSR, apic_base);

  lapic_write(LAPIC_REG_SVR, APIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR);
  apic_send_eoi();

  bsp_lapic_id = x2apic_enabled ? (uint32_t)rdmsr64(0x802) : lapic_read(LAPIC_REG_ID) >> 24;
  if (!bsp_lapic_id) {
    bsp_lapic_id = cpu_initial_apic_id();
  }

  if (acpi_have_madt() && acpi_ioapic_count() > 0) {
    ioapic_init_from_acpi();
    apic_enabled = 1;
    pic_disable();
    apic_timer_init_tsc_deadline();
    if (!apic_tsc_deadline_enabled[0]) {
      irq_mask_clear(APIC_TIMER_IRQ);
    }
    logk("apic: enabled %s lapic=%08x bsp=%d cpus=%d ioapics=%d\n",
         x2apic_enabled ? "x2apic" : "xapic", lapic_base_phys, bsp_lapic_id,
         acpi_cpu_count(), ioapic_total);
  } else {
    logk("apic: MADT/IOAPIC unavailable, using PIC\n");
  }
}

int apic_ready(void) { return apic_enabled; }

int apic_x2apic_enabled(void) { return x2apic_enabled; }

int apic_timer_uses_tsc_deadline(void) {
  return apic_tsc_deadline_enabled[smp_current_cpu()] != 0;
}

int apic_timer_tsc_deadline_available(void) {
  return apic_enabled && apic_tsc_deadline_supported &&
         apic_timer_deadline_interval_tsc != 0;
}

uint32_t apic_current_id(void) {
  if (x2apic_enabled) {
    return (uint32_t)rdmsr64(0x802);
  }
  return lapic_read(LAPIC_REG_ID) >> 24;
}

void apic_send_eoi(void) {
  if (!apic_enabled && !x2apic_enabled) {
    return;
  }
  if (x2apic_enabled) {
    wrmsr64(IA32_X2APIC_EOI, 0);
  } else {
    lapic_write(LAPIC_REG_EOI, 0);
  }
}

void apic_timer_on_interrupt(void) {
  if (apic_tsc_deadline_enabled[smp_current_cpu()]) {
    apic_timer_arm_next_deadline();
  }
}

int apic_timer_use_tsc_deadline(void) {
  if (!apic_timer_tsc_deadline_available()) {
    return 0;
  }

  irq_mask_set(APIC_TIMER_IRQ);
  wrmsr64(IA32_TSC_DEADLINE_MSR, 0);
  lapic_write(LAPIC_REG_TIMER_DIVIDE, 0);
  lapic_write(LAPIC_REG_LVT_TIMER,
              APIC_TIMER_VECTOR | APIC_LVT_TIMER_TSC_DEADLINE);
  uint32_t cpu = smp_current_cpu();
  apic_tsc_deadline_enabled[cpu] = 1;
  apic_timer_next_deadline_tsc[cpu] = 0;
  apic_timer_arm_next_deadline();
  return 1;
}

void apic_timer_use_irq0(void) {
  if (!apic_enabled) {
    return;
  }

  apic_timer_disable_tsc_deadline();
  irq_mask_clear(APIC_TIMER_IRQ);
}

void apic_route_irq(unsigned irq, unsigned char vector) {
  if (!apic_enabled || !irq_is_valid(irq)) {
    return;
  }
  ioapic_program_gsi(irq_to_gsi[irq], vector, irq_masked[irq], irq_trigger[irq],
                     irq_polarity[irq]);
}

void apic_configure_irq(unsigned irq, int trigger_mode, int polarity) {
  if (!apic_enabled || !irq_is_valid(irq)) {
    return;
  }
  ioapic_program_gsi(irq_to_gsi[irq], IRQ_BASE_VECTOR + irq, irq_masked[irq],
                     trigger_mode, polarity);
}

void apic_mask_irq(unsigned irq) {
  if (!apic_enabled || !irq_is_valid(irq)) {
    return;
  }
  irq_masked[irq] = 1;
  ioapic_program_gsi(irq_to_gsi[irq], IRQ_BASE_VECTOR + irq, 1,
                     irq_trigger[irq], irq_polarity[irq]);
}

void apic_unmask_irq(unsigned irq) {
  if (!apic_enabled || !irq_is_valid(irq)) {
    return;
  }
  irq_masked[irq] = 0;
  ioapic_program_gsi(irq_to_gsi[irq], IRQ_BASE_VECTOR + irq, 0,
                     irq_trigger[irq], irq_polarity[irq]);
}

void apic_mask_all_irqs(void) {
  if (!apic_enabled) {
    return;
  }
  for (unsigned char irq = 0; irq < MAX_IRQS; irq++) {
    apic_mask_irq(irq);
  }
}

static void apic_wait_icr_idle(void) {
  if (x2apic_enabled) {
    return;
  }
  while (lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12)) {
  }
}

void apic_send_init_ipi(uint32_t apic_id) {
  if (!cpu_has_apic()) {
    return;
  }
  apic_wait_icr_idle();
  lapic_send_ipi(apic_id, 0x0000c500u);
}

void apic_send_startup_ipi(uint32_t apic_id, uint32_t vector) {
  if (!cpu_has_apic()) {
    return;
  }
  apic_wait_icr_idle();
  lapic_send_ipi(apic_id, 0x00004600u | (vector & 0xff));
}

void apic_send_fixed_ipi(uint32_t apic_id, uint8_t vector) {
  if (!apic_enabled || vector < 0x20) {
    return;
  }
  apic_wait_icr_idle();
  lapic_send_ipi(apic_id, vector);
}

static void apic_timer_init_periodic(void) {
  lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
  lapic_write(LAPIC_REG_LVT_TIMER, APIC_TIMER_VECTOR | APIC_LVT_MASKED);
  lapic_write(LAPIC_REG_TIMER_INITIAL, UINT_MAX);

  if (tsc_khz) {
    uint64_t start = rdtsc64();
    while (rdtsc64() - start < tsc_khz) {
      asm volatile("pause");
    }
  } else if (hpet_available()) {
    uint64_t deadline = monotonic_time_ns() + 1000000ull;
    while (monotonic_time_ns() < deadline) {
      asm volatile("pause");
    }
  } else {
    lapic_write(LAPIC_REG_LVT_TIMER,
                APIC_TIMER_VECTOR | APIC_LVT_TIMER_PERIODIC);
    lapic_write(LAPIC_REG_TIMER_INITIAL, 1000000u);
    return;
  }
  uint32_t elapsed = UINT_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT);
  uint64_t initial = (uint64_t)elapsed * 10;
  if (!initial || initial > UINT_MAX) {
    initial = 1000000u;
  }
  lapic_write(LAPIC_REG_LVT_TIMER,
              APIC_TIMER_VECTOR | APIC_LVT_TIMER_PERIODIC);
  lapic_write(LAPIC_REG_TIMER_INITIAL, (uint32_t)initial);
}

void apic_init_secondary(void) {
  uint64_t apic_base = rdmsr64(IA32_APIC_BASE_MSR) | APIC_BASE_MSR_ENABLE;
  if (x2apic_enabled) {
    apic_base |= APIC_BASE_MSR_X2APIC;
  }
  wrmsr64(IA32_APIC_BASE_MSR, apic_base);
  lapic_write(LAPIC_REG_SVR, APIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR);
  apic_send_eoi();

  if (apic_tsc_deadline_supported && apic_timer_deadline_interval_tsc) {
    apic_timer_init_tsc_deadline();
  } else {
    if (!tsc_khz) {
      tsc_khz = cpu_detect_tsc_khz();
    }
    apic_timer_init_periodic();
  }
}
