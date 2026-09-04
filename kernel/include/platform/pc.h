#ifndef KERNEL_PLATFORM_PC_H
#define KERNEL_PLATFORM_PC_H

#include <ctypes.h>

/* 8259/PIT/CMOS and PS/2 register layout used by the PC platform. */
#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

#define BCD_HEX(n) ((((n) >> 4) * 10) + ((n) & 0xf))
#define HEX_BCD(n) (((n) / 10) << 4 | ((n) % 10))
#define CMOS_CUR_SEC 0x00
#define CMOS_ALA_SEC 0x01
#define CMOS_CUR_MIN 0x02
#define CMOS_ALA_MIN 0x03
#define CMOS_CUR_HOUR 0x04
#define CMOS_ALA_HOUR 0x05
#define CMOS_WEEK_DAY 0x06
#define CMOS_MON_DAY 0x07
#define CMOS_CUR_MON 0x08
#define CMOS_CUR_YEAR 0x09
#define CMOS_DEV_TYPE 0x12
#define CMOS_CUR_CEN 0x32
#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71

#define PORT_KEYDAT 0x0060
#define PORT_KEYSTA 0x0064
#define PORT_KEYCMD 0x0064

#define VGA_AC_INDEX 0x3c0
#define VGA_AC_WRITE 0x3c0
#define VGA_AC_READ 0x3c1
#define VGA_MISC_WRITE 0x3c2
#define VGA_SEQ_INDEX 0x3c4
#define VGA_SEQ_DATA 0x3c5
#define VGA_DAC_READ_INDEX 0x3c7
#define VGA_DAC_WRITE_INDEX 0x3c8
#define VGA_DAC_DATA 0x3c9
#define VGA_MISC_READ 0x3cc
#define VGA_GC_INDEX 0x3ce
#define VGA_GC_DATA 0x3cf
#define VGA_CRTC_INDEX 0x3d4
#define VGA_CRTC_DATA 0x3d5
#define VGA_INSTAT_READ 0x3da
#define VGA_NUM_SEQ_REGS 5
#define VGA_NUM_CRTC_REGS 25
#define VGA_NUM_GC_REGS 9
#define VGA_NUM_AC_REGS 21
#define VGA_NUM_REGS                                                        \
  (1 + VGA_NUM_SEQ_REGS + VGA_NUM_CRTC_REGS + VGA_NUM_GC_REGS +             \
   VGA_NUM_AC_REGS)

/* The legacy BIOS disk information structure is a PC device ABI. */
struct IDEHardDiskInfomationBlock {
  char reserve1[2];
  uint16_t CylinesNum;
  char reserve2[2];
  uint16_t HeadersNum;
  uint16_t TrackBytes;
  uint16_t SectorBytes;
  uint16_t TrackSectors;
  char reserve3[6];
  char OEM[20];
  char reserve4[2];
  uint16_t BuffersBytes;
  uint16_t EECCheckSumLength;
  char Version[8];
  char ID[40];
};

/* ACPI tables consumed by the PC firmware implementation. */
struct ACPI_RSDP {
  char Signature[8];
  uint8_t Checksum;
  char OEMID[6];
  uint8_t Revision;
  uint32_t RsdtAddress;
  uint32_t Length;
  uint32_t XsdtAddress[2];
  uint8_t ExtendedChecksum;
  uint8_t Reserved[3];
};

struct ACPISDTHeader {
  char Signature[4];
  uint32_t Length;
  uint8_t Revision;
  uint8_t Checksum;
  char OEMID[6];
  char OEMTableID[8];
  uint32_t OEMRevision;
  uint32_t CreatorID;
  uint32_t CreatorRevision;
};

struct ACPI_RSDT {
  struct ACPISDTHeader header;
  uint32_t Entry;
};

typedef struct {
  uint8_t AddressSpace;
  uint8_t BitWidth;
  uint8_t BitOffset;
  uint8_t AccessSize;
  uint32_t Address[2];
} GenericAddressStructure;

struct ACPI_FADT {
  struct ACPISDTHeader h;
  uint32_t FirmwareCtrl;
  uint32_t Dsdt;
  uint8_t Reserved;
  uint8_t PreferredPowerManagementProfile;
  uint16_t SCI_Interrupt;
  uint32_t SMI_CommandPort;
  uint8_t AcpiEnable;
  uint8_t AcpiDisable;
  uint8_t S4BIOS_REQ;
  uint8_t PSTATE_Control;
  uint32_t PM1aEventBlock;
  uint32_t PM1bEventBlock;
  uint32_t PM1aControlBlock;
  uint32_t PM1bControlBlock;
  uint32_t PM2ControlBlock;
  uint32_t PMTimerBlock;
  uint32_t GPE0Block;
  uint32_t GPE1Block;
  uint8_t PM1EventLength;
  uint8_t PM1ControlLength;
  uint8_t PM2ControlLength;
  uint8_t PMTimerLength;
  uint8_t GPE0Length;
  uint8_t GPE1Length;
  uint8_t GPE1Base;
  uint8_t CStateControl;
  uint16_t WorstC2Latency;
  uint16_t WorstC3Latency;
  uint16_t FlushSize;
  uint16_t FlushStride;
  uint8_t DutyOffset;
  uint8_t DutyWidth;
  uint8_t DayAlarm;
  uint8_t MonthAlarm;
  uint8_t Century;
  uint16_t BootArchitectureFlags;
  uint8_t Reserved2;
  uint32_t Flags;
  GenericAddressStructure ResetReg;
  uint8_t ResetValue;
  uint8_t Reserved3[3];
  uint32_t X_FirmwareControl[2];
  uint32_t X_Dsdt[2];
  GenericAddressStructure X_PM1aEventBlock;
  GenericAddressStructure X_PM1bEventBlock;
  GenericAddressStructure X_PM1aControlBlock;
  GenericAddressStructure X_PM1bControlBlock;
  GenericAddressStructure X_PM2ControlBlock;
  GenericAddressStructure X_PMTimerBlock;
  GenericAddressStructure X_GPE0Block;
  GenericAddressStructure X_GPE1Block;
} __attribute__((packed));

struct ACPI_MADT {
  struct ACPISDTHeader h;
  uint32_t LocalApicAddress;
  uint32_t Flags;
  uint8_t Entries[0];
} __attribute__((packed));

#endif
