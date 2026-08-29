#include <arch/x86/io.h>
#include <dos.h>

typedef struct {
  uint8_t type;
  uint8_t length;
} __attribute__((packed)) MadtEntryHeader;

typedef struct {
  MadtEntryHeader h;
  uint8_t acpi_processor_id;
  uint8_t apic_id;
  uint32_t flags;
} __attribute__((packed)) MadtLocalApic;

typedef struct {
  MadtEntryHeader h;
  uint8_t ioapic_id;
  uint8_t reserved;
  uint32_t ioapic_address;
  uint32_t gsi_base;
} __attribute__((packed)) MadtIoApic;

typedef struct {
  MadtEntryHeader h;
  uint8_t bus_source;
  uint8_t irq_source;
  uint32_t gsi;
  uint16_t flags;
} __attribute__((packed)) MadtIso;

typedef struct {
  MadtEntryHeader h;
  uint16_t reserved;
  uint32_t x2apic_id;
  uint32_t flags;
  uint32_t acpi_uid;
} __attribute__((packed)) MadtLocalX2Apic;

typedef struct {
  uint64_t configurationAndCapability;
  uint64_t comparatorValue;
  uint64_t fsbInterruptRoute;
  uint64_t unused;
} __attribute__((packed)) HpetTimer;

typedef struct {
  uint64_t generalCapabilities;
  uint64_t reserved0;
  uint64_t generalConfiguration;
  uint64_t reserved1;
  uint64_t generalInterruptStatus;
  uint8_t reserved3[0xc8];
  uint64_t mainCounterValue;
  uint64_t reserved4;
  HpetTimer timers[0];
} __attribute__((packed)) HpetInfo;

typedef struct {
  uint8_t addressSpaceID;
  uint8_t registerBitWidth;
  uint8_t registerBitOffset;
  uint8_t accessWidth;
  uint64_t address;
} __attribute__((packed)) AcpiAddress;

typedef struct {
  uint32_t signature;
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oem[6];
  uint8_t oemTableID[8];
  uint32_t oemVersion;
  uint32_t creatorID;
  uint32_t creatorVersion;
  uint8_t hardwareRevision;
  uint8_t comparatorCount : 5;
  uint8_t counterSize : 1;
  uint8_t reserved : 1;
  uint8_t legacyReplacement : 1;
  uint16_t pciVendorId;
  AcpiAddress hpetAddress;
  uint8_t hpetNumber;
  uint16_t minimumTick;
  uint8_t pageProtection;
} __attribute__((packed)) HPET;

struct ACPI_RSDP* RSDP;
struct ACPI_RSDT* RSDT;
struct ACPI_FADT* FADT;
static struct ACPI_MADT* MADT;

#define ACPI_MAX_CPUS 32
#define ACPI_MAX_IOAPICS 8
#define ACPI_IRQ_COUNT 16

typedef struct {
  uint32_t id;
  uint8_t enabled;
  uint8_t online_capable;
} AcpiCpuInfo;

typedef struct {
  uint8_t id;
  uint32_t address;
  uint32_t gsi_base;
} AcpiIoApicInfo;

typedef struct {
  uint32_t gsi;
  uint8_t overridden;
  uint8_t trigger_mode;
  uint8_t polarity;
} AcpiIrqRoute;

static uint32_t madt_lapic_address = 0xfee00000u;
static AcpiCpuInfo acpi_cpus[ACPI_MAX_CPUS];
static uint32_t acpi_cpu_total;
static uint32_t acpi_bsp_id;
static AcpiIoApicInfo acpi_ioapics[ACPI_MAX_IOAPICS];
static uint32_t acpi_ioapic_total;
static AcpiIrqRoute acpi_irq_routes[ACPI_IRQ_COUNT];
static volatile HpetInfo* hpetInfo = NULL;
static uint32_t hpetPeriodNs;
static uint32_t hpetPeriodFsRemainder;

#define HPET_CAP_COUNTER_64BIT (1ull << 13)
#define HPET_CONFIG_OFFSET 0x10u
#define HPET_COUNTER_OFFSET 0xf0u
#define HPET_CONFIG_ENABLE 1u
#define FEMTOSECONDS_PER_NANOSECOND 1000000u

static volatile uint32_t* hpet_register32(uint32_t offset) {
  return (volatile uint32_t*)((uintptr_t)hpetInfo + offset);
}

static uint64_t hpet_counter_read(void) {
  volatile uint32_t* counter = hpet_register32(HPET_COUNTER_OFFSET);
  uint32_t high_before;
  uint32_t high_after;
  uint32_t low;
  do {
    high_before = counter[1];
    low = counter[0];
    high_after = counter[1];
  } while (high_before != high_after);
  return ((uint64_t)high_after << 32) | low;
}

/* Keep division behind a call boundary so freestanding GCC does not combine
 * quotient and remainder into the non-exported __udivmoddi4 helper. */
static __attribute__((noinline)) uint64_t hpet_divide_fs_per_ns(
    uint64_t value) {
  return value / FEMTOSECONDS_PER_NANOSECOND;
}

static void acpi_init_irq_routes(void) {
  for (int i = 0; i < ACPI_IRQ_COUNT; i++) {
    acpi_irq_routes[i].gsi = (uint32_t)i;
    acpi_irq_routes[i].overridden = 0;
    acpi_irq_routes[i].trigger_mode = IRQ_TRIGGER_EDGE;
    acpi_irq_routes[i].polarity = IRQ_POLARITY_HIGH;
  }
}

static void acpi_register_cpu(uint32_t apic_id, uint32_t flags) {
  if (acpi_cpu_total >= ACPI_MAX_CPUS) {
    return;
  }
  acpi_cpus[acpi_cpu_total].id = apic_id;
  acpi_cpus[acpi_cpu_total].enabled = (flags & 1) != 0;
  acpi_cpus[acpi_cpu_total].online_capable = (flags & 2) != 0;
  acpi_cpu_total++;
}

static void acpi_parse_madt(void) {
  acpi_cpu_total = 0;
  acpi_ioapic_total = 0;
  acpi_init_irq_routes();

  if (!MADT) {
    return;
  }

  madt_lapic_address = MADT->LocalApicAddress;

  uint8_t* ptr = MADT->Entries;
  uint8_t* end = ((uint8_t*)MADT) + MADT->h.Length;
  while (ptr + sizeof(MadtEntryHeader) <= end) {
    MadtEntryHeader* hdr = (MadtEntryHeader*)ptr;
    if (hdr->length < sizeof(MadtEntryHeader) || ptr + hdr->length > end) {
      break;
    }

    switch (hdr->type) {
      case 0: {
        MadtLocalApic* lapic = (MadtLocalApic*)ptr;
        acpi_register_cpu(lapic->apic_id, lapic->flags);
        break;
      }
      case 1: {
        if (acpi_ioapic_total < ACPI_MAX_IOAPICS) {
          MadtIoApic* ioapic = (MadtIoApic*)ptr;
          acpi_ioapics[acpi_ioapic_total].id = ioapic->ioapic_id;
          acpi_ioapics[acpi_ioapic_total].address = ioapic->ioapic_address;
          acpi_ioapics[acpi_ioapic_total].gsi_base = ioapic->gsi_base;
          acpi_ioapic_total++;
        }
        break;
      }
      case 2: {
        MadtIso* iso = (MadtIso*)ptr;
        if (iso->bus_source == 0 && iso->irq_source < ACPI_IRQ_COUNT) {
          acpi_irq_routes[iso->irq_source].gsi = iso->gsi;
          acpi_irq_routes[iso->irq_source].overridden = 1;
          switch (iso->flags & 0x3) {
            case 3:
              acpi_irq_routes[iso->irq_source].polarity = IRQ_POLARITY_LOW;
              break;
            case 1:
            default:
              acpi_irq_routes[iso->irq_source].polarity = IRQ_POLARITY_HIGH;
              break;
          }
          switch ((iso->flags >> 2) & 0x3) {
            case 3:
              acpi_irq_routes[iso->irq_source].trigger_mode = IRQ_TRIGGER_LEVEL;
              break;
            case 1:
            default:
              acpi_irq_routes[iso->irq_source].trigger_mode = IRQ_TRIGGER_EDGE;
              break;
          }
        }
        break;
      }
      case 9: {
        MadtLocalX2Apic* x2 = (MadtLocalX2Apic*)ptr;
        acpi_register_cpu(x2->x2apic_id, x2->flags);
        break;
      }
      default:
        break;
    }

    ptr += hdr->length;
  }

  acpi_bsp_id = acpi_cpu_total ? acpi_cpus[0].id : 0;
}

char checksum(unsigned char* addr, unsigned int length) {
  unsigned char sum = 0;

  for (unsigned int i = 0; i < length; i++) {
    sum += addr[i];
  }

  return sum == 0;
}

unsigned int* acpi_find_rsdp(void) {
  unsigned int* addr;

  for (addr = (unsigned int*)0x000e0000; addr < (unsigned int*)0x00100000;
       addr++) {
    if (memcmp(addr, "RSD PTR ", 8) == 0) {
      if (checksum((unsigned char*)addr, ((struct ACPI_RSDP*)addr)->Length)) {
        return addr;
      }
    }
  }
  return 0;
}

void* acpi_find_table(char* Signature) {
  uint8_t *ptr, *ptr2;
  uint32_t len;
  uint8_t* rsdt = (uint8_t*)RSDT;
  for (len = *((uint32_t*)(rsdt + 4)), ptr2 = rsdt + 36; ptr2 < rsdt + len;
       ptr2 += rsdt[0] == 'X' ? 8 : 4) {
    ptr = (uint8_t*)(uintptr_t)(rsdt[0] == 'X' ? *((uint64_t*)ptr2)
                                               : *((uint32_t*)ptr2));
    if (!memcmp(ptr, Signature, 4)) {
      return ptr;
    }
  }
  return NULL;
}

static void hpet_initialize(void) {
  HPET* hpet = (HPET*)acpi_find_table("HPET");
  if (!hpet) {
    logk("acpi: HPET not found\n");
    return;
  }

  uint64_t address = hpet->hpetAddress.address;
  if (hpet->hpetAddress.addressSpaceID != 0 || address == 0 ||
      address > 0xffffffffull) {
    logk("acpi: unsupported HPET address\n");
    return;
  }

  hpetInfo = (volatile HpetInfo*)(uintptr_t)(uint32_t)address;
  uint64_t capabilities = hpetInfo->generalCapabilities;
  uint32_t period_fs = (uint32_t)(capabilities >> 32);
  if (period_fs == 0 || (capabilities & HPET_CAP_COUNTER_64BIT) == 0) {
    logk("acpi: HPET counter is unavailable or not 64-bit\n");
    hpetInfo = NULL;
    return;
  }

  hpetPeriodNs = period_fs / FEMTOSECONDS_PER_NANOSECOND;
  hpetPeriodFsRemainder = period_fs % FEMTOSECONDS_PER_NANOSECOND;
  volatile uint32_t* config = hpet_register32(HPET_CONFIG_OFFSET);
  volatile uint32_t* counter = hpet_register32(HPET_COUNTER_OFFSET);
  config[0] &= ~HPET_CONFIG_ENABLE;
  counter[0] = 0;
  counter[1] = 0;
  config[0] |= HPET_CONFIG_ENABLE;
  logk("acpi: HPET %08x period_fs=%d\n", (uint32_t)(uintptr_t)hpetInfo,
       period_fs);
}

void init_acpi(void) {
  RSDP = (struct ACPI_RSDP*)acpi_find_rsdp();
  if (RSDP == 0) {
    logk("acpi: RSDP not found\n");
    return;
  }

  RSDT = (struct ACPI_RSDT*)RSDP->RsdtAddress;
  if (!checksum((unsigned char*)RSDT, RSDT->header.Length)) {
    logk("acpi: invalid RSDT checksum\n");
    return;
  }

  FADT = (struct ACPI_FADT*)acpi_find_table("FACP");
  if (FADT && !checksum((unsigned char*)FADT, FADT->h.Length)) {
    logk("acpi: invalid FADT checksum\n");
    FADT = NULL;
  }

  if (FADT && !(x86_port_read16(FADT->PM1aControlBlock) & 1)) {
    if (FADT->SMI_CommandPort && FADT->AcpiEnable) {
      x86_port_write8(FADT->SMI_CommandPort, FADT->AcpiEnable);
      for (int i = 0; i < 300; i++) {
        if (x86_port_read16(FADT->PM1aControlBlock) & 1) {
          break;
        }
        for (volatile int j = 0; j < 1000000; j++) {
        }
      }
      if (FADT->PM1bControlBlock) {
        for (int i = 0; i < 300; i++) {
          if (x86_port_read16(FADT->PM1bControlBlock) & 1) {
            break;
          }
          for (volatile int j = 0; j < 1000000; j++) {
          }
        }
      }
    }
  }

  MADT = (struct ACPI_MADT*)acpi_find_table("APIC");
  if (MADT && checksum((unsigned char*)MADT, MADT->h.Length)) {
    acpi_parse_madt();
    logk("acpi: MADT lapic=%08x cpus=%d ioapics=%d\n", madt_lapic_address,
         acpi_cpu_total, acpi_ioapic_total);
  } else {
    MADT = NULL;
    acpi_init_irq_routes();
    logk("acpi: MADT not available\n");
  }

  hpet_initialize();
}

int acpi_shutdown(void) {
  unsigned short SLP_TYPa = 0, SLP_TYPb = 0;
  struct ACPISDTHeader* header = (struct ACPISDTHeader*)acpi_find_table("DSDT");
  if (header == NULL || FADT == NULL) {
    return 0;
  }
  char* S5Addr = (char*)header;
  int dsdtLength = (header->Length - sizeof(struct ACPISDTHeader)) / 4;

  for (int i = 0; i < dsdtLength; i++) {
    if (memcmp(S5Addr, "_S5_", 4) == 0) {
      break;
    }
    S5Addr++;
  }

  if ((*(S5Addr - 1) == 0x08 ||
       (*(S5Addr - 2) == 0x08 && *(S5Addr - 1) == '\\')) &&
      *(S5Addr + 4) == 0x12) {
    S5Addr += 5;
    S5Addr += ((*S5Addr & 0xc0) >> 6) + 2;

    if (*S5Addr == 0x0a) {
      S5Addr++;
    }
    SLP_TYPa = *(S5Addr) << 10;
    S5Addr++;

    if (*S5Addr == 0x0a) {
      S5Addr++;
    }
    SLP_TYPb = *(S5Addr) << 10;
  }

  x86_port_write16(FADT->PM1aControlBlock, SLP_TYPa | 1 << 13);
  if (FADT->PM1bControlBlock != 0) {
    x86_port_write16(FADT->PM1bControlBlock, SLP_TYPb | 1 << 13);
  }
  return 1;
}

bool hpet_available(void) { return hpetInfo != NULL; }

uint64_t monotonic_time_ns(void) {
  if (!hpetInfo) {
    return (uint64_t)timerctl.count * 10000000ull;
  }
  uint64_t counter = hpet_counter_read();
  uint64_t nanoseconds = counter * hpetPeriodNs;
  if (hpetPeriodFsRemainder != 0) {
    uint64_t whole = hpet_divide_fs_per_ns(counter);
    uint64_t remainder =
        counter - whole * FEMTOSECONDS_PER_NANOSECOND;
    nanoseconds += whole * hpetPeriodFsRemainder;
    nanoseconds +=
        hpet_divide_fs_per_ns(remainder * hpetPeriodFsRemainder);
  }
  return nanoseconds;
}

void usleep(uint64_t nano) {
  if (!hpetInfo) {
    volatile uint64_t loops = nano / 100;
    while (loops--) {
    }
    return;
  }

  uint64_t started = monotonic_time_ns();
  while (monotonic_time_ns() - started < nano) {
  }
}

int acpi_have_madt(void) { return MADT != NULL; }

uint32_t acpi_lapic_address(void) { return madt_lapic_address; }

uint32_t acpi_cpu_count(void) { return acpi_cpu_total; }

uint32_t acpi_bsp_lapic_id(void) { return acpi_bsp_id; }

uint32_t acpi_cpu_lapic_id(uint32_t index) {
  if (index >= acpi_cpu_total) {
    return 0;
  }
  return acpi_cpus[index].id;
}

int acpi_cpu_usable(uint32_t index) {
  return index < acpi_cpu_total &&
         (acpi_cpus[index].enabled || acpi_cpus[index].online_capable);
}

uint32_t acpi_ioapic_count(void) { return acpi_ioapic_total; }

uint32_t acpi_ioapic_address(uint32_t index) {
  if (index >= acpi_ioapic_total) {
    return 0;
  }
  return acpi_ioapics[index].address;
}

uint32_t acpi_ioapic_gsi_base(uint32_t index) {
  if (index >= acpi_ioapic_total) {
    return 0;
  }
  return acpi_ioapics[index].gsi_base;
}

uint32_t acpi_irq_to_gsi(uint32_t irq) {
  if (irq >= ACPI_IRQ_COUNT) {
    return irq;
  }
  return acpi_irq_routes[irq].gsi;
}

int acpi_irq_trigger_mode(uint32_t irq) {
  if (irq >= ACPI_IRQ_COUNT) {
    return IRQ_TRIGGER_EDGE;
  }
  return acpi_irq_routes[irq].trigger_mode;
}

int acpi_irq_polarity(uint32_t irq) {
  if (irq >= ACPI_IRQ_COUNT) {
    return IRQ_POLARITY_HIGH;
  }
  return acpi_irq_routes[irq].polarity;
}

int acpi_isa_irq_overridden(uint32_t irq) {
  if (irq >= ACPI_IRQ_COUNT) {
    return 0;
  }
  return acpi_irq_routes[irq].overridden;
}
