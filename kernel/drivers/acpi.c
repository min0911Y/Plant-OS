#include <dos.h>
struct ACPI_RSDP *RSDP;
struct ACPI_RSDT *RSDT;
struct ACPI_FADT *FADT;

char checksum(u8 *addr, u32 length) {
  int i   = 0;
  u8  sum = 0;

  for (i = 0; i < length; i++) {
    sum += ((u8 *)addr)[i];
  }

  return sum == 0;
}

u32 *acpi_find_rsdp() {
  u32 *addr;

  for (addr = (u32 *)0x000e0000; addr < (u32 *)0x00100000; addr++) {
    if (memcmp(addr, "RSD PTR ", 8) == 0) {
      if (checksum((u8 *)addr, ((struct ACPI_RSDP *)addr)->Length)) { return addr; }
    }
  }
  return 0;
}

u32 acpi_find_table(char *Signature) {
  u8 *ptr, *ptr2;
  u32 len;
  u8 *rsdt = (u8 *)RSDT;
  // iterate on ACPI table pointers
  for (len = *((u32 *)(rsdt + 4)), ptr2 = rsdt + 36; ptr2 < rsdt + len;
       ptr2 += rsdt[0] == 'X' ? 8 : 4) {
    ptr = (u8 *)(uintptr_t)(rsdt[0] == 'X' ? *((uint64_t *)ptr2) : *((u32 *)ptr2));
    if (!memcmp(ptr, Signature, 4)) { return (unsigned)ptr; }
  }
  // printk("not found.\n");
  return 0;
}

void init_acpi(void) {
  RSDP = (struct ACPI_RSDP *)acpi_find_rsdp();
  if (RSDP == 0) return;
  RSDT = (struct ACPI_RSDT *)RSDP->RsdtAddress;
  // checksum(RSDT, RSDT->header.Length);
  if (!checksum((u8 *)RSDT, RSDT->header.Length)) return;

  FADT = (struct ACPI_FADT *)acpi_find_table("FACP");
  if (!checksum((u8 *)FADT, FADT->h.Length)) return;

  if (!(io_in16(FADT->PM1aControlBlock) & 1)) {
    if (FADT->SMI_CommandPort && FADT->AcpiEnable) {
      io_out8(FADT->SMI_CommandPort, FADT->AcpiEnable);
      int i, j;
      for (i = 0; i < 300; i++) {
        if (io_in16(FADT->PM1aControlBlock) & 1) break;
        for (j = 0; j < 1000000; j++)
          ;
      }
      if (FADT->PM1bControlBlock) {
        for (; i < 300; i++) {
          if (io_in16(FADT->PM1bControlBlock) & 1) break;
          for (j = 0; j < 1000000; j++)
            ;
        }
      }
    }
  }
  lapic_find();
  hpet_initialize();
  return;
}

/*
 * \_S5 Object
 * -----------------------------------
 * NameOP | \(可选) | _  | S  | 5  | _
 * 08     | 5A     | 5F | 53 | 35 | 5F
 * -----------------------------------
 * PackageOP | PkgLength | NumElements | prefix Num | prefix Num | prefix Num |
 * prefix Num 12        | 0A        | 04          | 0A     05  | 0A     05  | 0A
 * 05  | 0A     05
 * -----------------------------------
 * PkgLength: bit6~7为长度的字节数-1;bit4~5保留;bit0~3为长度的低4位
 * prefix:	0A Byte
 * 			0B Word
 * 			0C DWord
 * 			0D String
 * 			0E Qword
 */

int acpi_shutdown() {
  int                   i;
  u16                   SLP_TYPa, SLP_TYPb;
  struct ACPISDTHeader *header     = (struct ACPISDTHeader *)acpi_find_table("DSDT");
  char                 *S5Addr     = (char *)header;
  int                   dsdtLength = (header->Length - sizeof(struct ACPISDTHeader)) / 4;

  for (i = 0; i < dsdtLength; i++) {
    if (memcmp(S5Addr, "_S5_", 4) == 0) break;
    S5Addr++;
  }
  if (i < dsdtLength) {
    if ((*(S5Addr - 1) == 0x08 || (*(S5Addr - 2) == 0x08 && *(S5Addr - 1) == '\\')) &&
        *(S5Addr + 4) == 0x12) {
      S5Addr += 5;
      S5Addr += ((*S5Addr & 0xc0) >> 6) + 2;

      if (*S5Addr == 0x0a) S5Addr++;
      SLP_TYPa = *(S5Addr) << 10;
      S5Addr++;

      if (*S5Addr == 0x0a) S5Addr++;
      SLP_TYPb = *(S5Addr) << 10;
      S5Addr++;
    }
    // 关于PM1x_CNT_BLK的描述见 ACPI Specification Ver6.3 4.8.3.2.1
    io_out16(FADT->PM1aControlBlock, SLP_TYPa | 1 << 13);
    if (FADT->PM1bControlBlock != 0) { io_out16(FADT->PM1bControlBlock, SLP_TYPb | 1 << 13); }
  }
  return 1;
}
// copy from EXOS(https://gitee.com/yywd123/EXOS)
typedef struct {
  char     sign[4];
  u32      len;
  char     revision;
  char     checksum;
  char     oemid[6];
  uint64_t oem_table_id;
  u32      oem_revision;
  u32      creator_id;
  u32      creator_revision;
} __attribute__((packed)) MADT;
typedef struct {
  uint64_t configurationAndCapability;
  uint64_t comparatorValue;
  uint64_t fsbInterruptRoute;
  uint64_t unused;
} __attribute__((packed)) HpetTimer;

typedef struct {
  uint64_t  generalCapabilities;
  uint64_t  reserved0;
  uint64_t  generalConfiguration;
  uint64_t  reserved1;
  uint64_t  generalIntrruptStatus;
  u8        reserved3[0xc8];
  uint64_t  mainCounterValue;
  uint64_t  reserved4;
  HpetTimer timers[0];
} __attribute__((packed)) HpetInfo;

typedef struct {
  u8        addressSpaceID;
  u8        registerBitWidth;
  u8        registerBitOffset;
  u8        accessWidth; //  acpi 3.0
  uintptr_t address;
} __attribute__((packed)) AcpiAddress;

typedef struct {
  u32         signature;
  u32         length;
  u8          revision;
  u8          checksum;
  u8          oem[6];
  u8          oemTableID[8];
  u32         oemVersion;
  u32         creatorID;
  u32         creatorVersion;
  u8          hardwareRevision;
  u8          comparatorCount   : 5;
  u8          counterSize       : 1;
  u8          reserved          : 1;
  u8          legacyReplacement : 1;
  u16         pciVendorId;
  AcpiAddress hpetAddress;
  u8          hpetNumber;
  u16         minimumTick;
  u8          pageProtection;
} __attribute__((packed)) HPET;

static HpetInfo *hpetInfo   = NULL;
static uint64_t  hpetPeriod = 0;

void hpet_initialize() {
  HPET *hpet = acpi_find_table("HPET");
  if (!hpet) { logk("can not found acpi hpet table"); }
  hpetInfo = (HpetInfo *)hpet->hpetAddress.address;
  logk("hpetInfo %016x\n", hpetInfo);

  u32 counterClockPeriod = hpetInfo->generalCapabilities >> 32;
  hpetPeriod             = counterClockPeriod / 1000000;
  logk("hpet period: 0x%016x\n", hpetPeriod);

  hpetInfo->generalConfiguration |= 1; //  启用hpet

  // Logger::log(Logger::INFO, "hpet successfully enabled");
}

#define NANOSEC_IN_SEC 1000000000

void gettime_ns(time_ns_t *ptr) {
  static uint64_t time_sec  = 0;
  static uint64_t time_ns   = 0;
  static uint64_t val_old   = 0;
  uint64_t        val       = hpetInfo->mainCounterValue * hpetPeriod - val_old;
  val_old                  += val;
  time_ns                  += val;
  while (time_ns > NANOSEC_IN_SEC) {
    time_sec += 1;
    time_ns  -= NANOSEC_IN_SEC;
  }
  if (ptr == NULL) return;
  ptr->sec  = time_sec;
  ptr->nsec = time_ns;
}

void usleep(uint64_t time_us) {
  time_ns_t end_time;
  gettime_ns(&end_time);
  end_time.sec  = time_us / 1000000;
  end_time.nsec = time_us % 1000000 * 1000;
  if (end_time.nsec > NANOSEC_IN_SEC) {
    end_time.sec  += 1;
    end_time.nsec -= NANOSEC_IN_SEC;
  }
  time_ns_t now_time;
  do {
    gettime_ns(&now_time);
  } while (now_time.sec < end_time.sec || now_time.nsec < end_time.nsec);
}

void lapic_find() {
  MADT *p = acpi_find_table("APIC");
  logk("%08x\n", p);
  logk("MADT : len = %08x\n", p->len);
  logk("oemid : ");
  for (int i = 0; i < 6; i++) {
    logk("%c", p->oemid[i]);
  }
}