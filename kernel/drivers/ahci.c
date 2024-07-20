// AHCI Controller Driver Implement

#include <dos.h>

static u8 *cache;
#define SATA_SIG_ATA   0x00000101 // SATA drive
#define SATA_SIG_ATAPI 0xEB140101 // SATAPI drive
#define SATA_SIG_SEMB  0xC33C0101 // Enclosure management bridge
#define SATA_SIG_PM    0x96690101 // Port multiplier
#define HBA_PxCMD_ST   0x0001
#define HBA_PxCMD_FRE  0x0010
#define HBA_PxCMD_FR   0x4000
#define HBA_PxCMD_CR   0x8000
#define HBA_PxIS_TFES  (1 << 30) /* TFES - Task File Error Status */

#define AHCI_DEV_NULL   0
#define AHCI_DEV_SATA   1
#define AHCI_DEV_SEMB   2
#define AHCI_DEV_PM     3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE  1
#define HBA_PORT_DET_PRESENT 3
typedef enum {
  FIS_TYPE_REG_H2D   = 0x27, // Register FIS - host to device
  FIS_TYPE_REG_D2H   = 0x34, // Register FIS - device to host
  FIS_TYPE_DMA_ACT   = 0x39, // DMA activate FIS - device to host
  FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
  FIS_TYPE_DATA      = 0x46, // Data FIS - bidirectional
  FIS_TYPE_BIST      = 0x58, // BIST activate FIS - bidirectional
  FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
  FIS_TYPE_DEV_BITS  = 0xA1, // Set device bits FIS - device to host
} FIS_TYPE;
typedef volatile struct tagHBA_PORT {
  u32 clb;       // 0x00, command list base address, 1K-byte aligned
  u32 clbu;      // 0x04, command list base address upper 32 bits
  u32 fb;        // 0x08, FIS base address, 256-byte aligned
  u32 fbu;       // 0x0C, FIS base address upper 32 bits
  u32 is;        // 0x10, interrupt status
  u32 ie;        // 0x14, interrupt enable
  u32 cmd;       // 0x18, command and status
  u32 rsv0;      // 0x1C, Reserved
  u32 tfd;       // 0x20, task file data
  u32 sig;       // 0x24, signature
  u32 ssts;      // 0x28, SATA status (SCR0:SStatus)
  u32 sctl;      // 0x2C, SATA control (SCR2:SControl)
  u32 serr;      // 0x30, SATA error (SCR1:SError)
  u32 sact;      // 0x34, SATA active (SCR3:SActive)
  u32 ci;        // 0x38, command issue
  u32 sntf;      // 0x3C, SATA notification (SCR4:SNotification)
  u32 fbs;       // 0x40, FIS-based switch control
  u32 rsv1[11];  // 0x44 ~ 0x6F, Reserved
  u32 vendor[4]; // 0x70 ~ 0x7F, vendor specific
} HBA_PORT;
typedef volatile struct tagHBA_MEM {
  // 0x00 - 0x2B, Generic Host Control
  u32 cap;     // 0x00, Host capability
  u32 ghc;     // 0x04, Global host control
  u32 is;      // 0x08, Interrupt status
  u32 pi;      // 0x0C, Port implemented
  u32 vs;      // 0x10, Version
  u32 ccc_ctl; // 0x14, Command completion coalescing control
  u32 ccc_pts; // 0x18, Command completion coalescing ports
  u32 em_loc;  // 0x1C, Enclosure management location
  u32 em_ctl;  // 0x20, Enclosure management control
  u32 cap2;    // 0x24, Host capabilities extended
  u32 bohc;    // 0x28, BIOS/OS handoff control and status

  // 0x2C - 0x9F, Reserved
  u8 rsv[0xA0 - 0x2C];

  // 0xA0 - 0xFF, Vendor specific registers
  u8 vendor[0x100 - 0xA0];

  // 0x100 - 0x10FF, Port control registers
  HBA_PORT ports[1]; // 1 ~ 32
} HBA_MEM;
typedef struct tagHBA_CMD_HEADER {
  // DW0
  u8 cfl : 5; // Command FIS length in DWORDS, 2 ~ 16
  u8 a   : 1; // ATAPI
  u8 w   : 1; // Write, 1: H2D, 0: D2H
  u8 p   : 1; // Prefetchable

  u8 r    : 1; // Reset
  u8 b    : 1; // BIST
  u8 c    : 1; // Clear busy upon R_OK
  u8 rsv0 : 1; // Reserved
  u8 pmp  : 4; // Port multiplier port

  u16 prdtl; // Physical region descriptor table length in entries

  // DW1
  volatile u32 prdbc; // Physical region descriptor byte count transferred

  // DW2, 3
  u32 ctba;  // Command table descriptor base address
  u32 ctbau; // Command table descriptor base address upper 32 bits

  // DW4 - 7
  u32 rsv1[4]; // Reserved
} HBA_CMD_HEADER;
typedef struct tagFIS_REG_H2D {
  // DWORD 0
  u8 fis_type; // FIS_TYPE_REG_H2D

  u8 pmport : 4; // Port multiplier
  u8 rsv0   : 3; // Reserved
  u8 c      : 1; // 1: Command, 0: Control

  u8 command;  // Command register
  u8 featurel; // Feature register, 7:0

  // DWORD 1
  u8 lba0;   // LBA low register, 7:0
  u8 lba1;   // LBA mid register, 15:8
  u8 lba2;   // LBA high register, 23:16
  u8 device; // Device register

  // DWORD 2
  u8 lba3;     // LBA register, 31:24
  u8 lba4;     // LBA register, 39:32
  u8 lba5;     // LBA register, 47:40
  u8 featureh; // Feature register, 15:8

  // DWORD 3
  u8 countl;  // Count register, 7:0
  u8 counth;  // Count register, 15:8
  u8 icc;     // Isochronous command completion
  u8 control; // Control register

  // DWORD 4
  u8 rsv1[4]; // Reserved
} FIS_REG_H2D;
typedef struct tagFIS_REG_D2H {
  // DWORD 0
  u8 fis_type; // FIS_TYPE_REG_D2H

  u8 pmport : 4; // Port multiplier
  u8 rsv0   : 2; // Reserved
  u8 i      : 1; // Interrupt bit
  u8 rsv1   : 1; // Reserved

  u8 status; // Status register
  u8 error;  // Error register

  // DWORD 1
  u8 lba0;   // LBA low register, 7:0
  u8 lba1;   // LBA mid register, 15:8
  u8 lba2;   // LBA high register, 23:16
  u8 device; // Device register

  // DWORD 2
  u8 lba3; // LBA register, 31:24
  u8 lba4; // LBA register, 39:32
  u8 lba5; // LBA register, 47:40
  u8 rsv2; // Reserved

  // DWORD 3
  u8 countl;  // Count register, 7:0
  u8 counth;  // Count register, 15:8
  u8 rsv3[2]; // Reserved

  // DWORD 4
  u8 rsv4[4]; // Reserved
} FIS_REG_D2H;
typedef struct tagFIS_DATA {
  // DWORD 0
  u8 fis_type; // FIS_TYPE_DATA

  u8 pmport : 4; // Port multiplier
  u8 rsv0   : 4; // Reserved

  u8 rsv1[2]; // Reserved

  // DWORD 1 ~ N
  u32 data[1]; // Payload
} FIS_DATA;
typedef struct tagFIS_PIO_SETUP {
  // DWORD 0
  u8 fis_type; // FIS_TYPE_PIO_SETUP

  u8 pmport : 4; // Port multiplier
  u8 rsv0   : 1; // Reserved
  u8 d      : 1; // Data transfer direction, 1 - device to host
  u8 i      : 1; // Interrupt bit
  u8 rsv1   : 1;

  u8 status; // Status register
  u8 error;  // Error register

  // DWORD 1
  u8 lba0;   // LBA low register, 7:0
  u8 lba1;   // LBA mid register, 15:8
  u8 lba2;   // LBA high register, 23:16
  u8 device; // Device register

  // DWORD 2
  u8 lba3; // LBA register, 31:24
  u8 lba4; // LBA register, 39:32
  u8 lba5; // LBA register, 47:40
  u8 rsv2; // Reserved

  // DWORD 3
  u8 countl;   // Count register, 7:0
  u8 counth;   // Count register, 15:8
  u8 rsv3;     // Reserved
  u8 e_status; // New value of status register

  // DWORD 4
  u16 tc;      // Transfer count
  u8  rsv4[2]; // Reserved
} FIS_PIO_SETUP;
typedef struct tagFIS_DMA_SETUP {
  // DWORD 0
  u8 fis_type; // FIS_TYPE_DMA_SETUP

  u8 pmport : 4; // Port multiplier
  u8 rsv0   : 1; // Reserved
  u8 d      : 1; // Data transfer direction, 1 - device to host
  u8 i      : 1; // Interrupt bit
  u8 a      : 1; // Auto-activate. Specifies if DMA Activate FIS is needed

  u8 rsved[2]; // Reserved

  // DWORD 1&2

  u64 DMAbufferID; // DMA Buffer Identifier. Used to Identify DMA buffer in
                   // host memory. SATA Spec says host specific and not in
                   // Spec. Trying AHCI spec might work.

  // DWORD 3
  u32 rsvd; // More reserved

  // DWORD 4
  u32 DMAbufOffset; // Byte offset into buffer. First 2 bits must be 0

  // DWORD 5
  u32 TransferCount; // Number of bytes to transfer. Bit 0 must be 0

  // DWORD 6
  u32 resvd; // Reserved

} FIS_DMA_SETUP;
typedef struct tagHBA_PRDT_ENTRY {
  u32 dba;  // Data base address
  u32 dbau; // Data base address upper 32 bits
  u32 rsv0; // Reserved

  // DW3
  u32 dbc  : 22; // Byte count, 4M max
  u32 rsv1 : 9;  // Reserved
  u32 i    : 1;  // Interrupt on completion
} HBA_PRDT_ENTRY;
typedef struct tagHBA_CMD_TBL {
  // 0x00
  u8 cfis[64]; // Command FIS

  // 0x40
  u8 acmd[16]; // ATAPI command, 12 or 16 bytes

  // 0x50
  u8 rsv[48]; // Reserved

  // 0x80
  HBA_PRDT_ENTRY
  prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TBL;
typedef struct SATA_Ident {
  u16 config;        /* lots of obsolete bit flags */
  u16 cyls;          /* obsolete */
  u16 reserved2;     /* special config */
  u16 heads;         /* "physical" heads */
  u16 track_bytes;   /* unformatted bytes per track */
  u16 sector_bytes;  /* unformatted bytes per sector */
  u16 sectors;       /* "physical" sectors per track */
  u16 vendor0;       /* vendor unique */
  u16 vendor1;       /* vendor unique */
  u16 vendor2;       /* vendor unique */
  u8  serial_no[20]; /* 0 = not_specified */
  u16 buf_type;
  u16 buf_size;             /* 512 byte increments; 0 = not_specified */
  u16 ecc_bytes;            /* for r/w long cmds; 0 = not_specified */
  u8  fw_rev[8];            /* 0 = not_specified */
  u8  model[40];            /* 0 = not_specified */
  u16 multi_count;          /* Multiple Count */
  u16 dword_io;             /* 0=not_implemented; 1=implemented */
  u16 capability1;          /* vendor unique */
  u16 capability2;          /* bits 0:DMA 1:LBA 2:IORDYsw 3:IORDYsup word: 50 */
  u8  vendor5;              /* vendor unique */
  u8  tPIO;                 /* 0=slow, 1=medium, 2=fast */
  u8  vendor6;              /* vendor unique */
  u8  tDMA;                 /* 0=slow, 1=medium, 2=fast */
  u16 field_valid;          /* bits 0:cur_ok 1:eide_ok */
  u16 cur_cyls;             /* logical cylinders */
  u16 cur_heads;            /* logical heads word 55*/
  u16 cur_sectors;          /* logical sectors per track */
  u16 cur_capacity0;        /* logical total sectors on drive */
  u16 cur_capacity1;        /*  (2 words, misaligned int)     */
  u8  multsect;             /* current multiple sector count */
  u8  multsect_valid;       /* when (bit0==1) multsect is ok */
  u32 lba_capacity;         /* total number of sectors */
  u16 dma_1word;            /* single-word dma info */
  u16 dma_mword;            /* multiple-word dma info */
  u16 eide_pio_modes;       /* bits 0:mode3 1:mode4 */
  u16 eide_dma_min;         /* min mword dma cycle time (ns) */
  u16 eide_dma_time;        /* recommended mword dma cycle time (ns) */
  u16 eide_pio;             /* min cycle time (ns), no IORDY  */
  u16 eide_pio_iordy;       /* min cycle time (ns), with IORDY */
  u16 words69_70[2];        /* reserved words 69-70 */
  u16 words71_74[4];        /* reserved words 71-74 */
  u16 queue_depth;          /*  */
  u16 sata_capability;      /*  SATA Capabilities word 76*/
  u16 sata_additional;      /*  Additional Capabilities */
  u16 sata_supported;       /* SATA Features supported  */
  u16 features_enabled;     /* SATA features enabled */
  u16 major_rev_num;        /*  Major rev number word 80 */
  u16 minor_rev_num;        /*  */
  u16 command_set_1;        /* bits 0:Smart 1:Security 2:Removable 3:PM */
  u16 command_set_2;        /* bits 14:Smart Enabled 13:0 zero */
  u16 cfsse;                /* command set-feature supported extensions */
  u16 cfs_enable_1;         /* command set-feature enabled */
  u16 cfs_enable_2;         /* command set-feature enabled */
  u16 csf_default;          /* command set-feature default */
  u16 dma_ultra;            /*  */
  u16 word89;               /* reserved (word 89) */
  u16 word90;               /* reserved (word 90) */
  u16 CurAPMvalues;         /* current APM values */
  u16 word92;               /* reserved (word 92) */
  u16 comreset;             /* should be cleared to 0 */
  u16 accoustic;            /*  accoustic management */
  u16 min_req_sz;           /* Stream minimum required size */
  u16 transfer_time_dma;    /* Streaming Transfer Time-DMA */
  u16 access_latency;       /* Streaming access latency-DMA & PIO WORD 97*/
  u32 perf_granularity;     /* Streaming performance granularity */
  u32 total_usr_sectors[2]; /* Total number of user addressable sectors */
  u16 transfer_time_pio;    /* Streaming Transfer time PIO */
  u16 reserved105;          /* Word 105 */
  u16 sector_sz;            /* Puysical Sector size / Logical sector size */
  u16 inter_seek_delay;     /* In microseconds */
  u16 words108_116[9];      /*  */
  u32 words_per_sector;     /* words per logical sectors */
  u16 supported_settings;   /* continued from words 82-84 */
  u16 command_set_3;        /* continued from words 85-87 */
  u16 words121_126[6];      /* reserved words 121-126 */
  u16 word127;              /* reserved (word 127) */
  u16 security_status;      /* device lock function
                                      * 15:9   reserved
                                      * 8   security level 1:max 0:high
                                      * 7:6   reserved
                                      * 5   enhanced erase
                                      * 4   expire
                                      * 3   frozen
                                      * 2   locked
                                      * 1   en/disabled
                                      * 0   capability
                                      */
  u16 csfo;                 /* current set features options
                                      * 15:4   reserved
                                      * 3   auto reassign
                                      * 2   reverting
                                      * 1   read-look-ahead
                                      * 0   write cache
                                      */
  u16 words130_155[26];     /* reserved vendor words 130-155 */
  u16 word156;
  u16 words157_159[3];     /* reserved vendor words 157-159 */
  u16 cfa;                 /* CFA Power mode 1 */
  u16 words161_175[15];    /* Reserved */
  u8  media_serial[60];    /* words 176-205 Current Media serial number */
  u16 sct_cmd_transport;   /* SCT Command Transport */
  u16 words207_208[2];     /* reserved */
  u16 block_align;         /* Alignement of logical blocks in larger physical blocks */
  u32 WRV_sec_count;       /* Write-Read-Verify sector count mode 3 only */
  u32 verf_sec_count;      /* Verify Sector count mode 2 only */
  u16 nv_cache_capability; /* NV Cache capabilities */
  u16 nv_cache_sz;         /* NV Cache size in logical blocks */
  u16 nv_cache_sz2;        /* NV Cache size in logical blocks */
  u16 rotation_rate;       /* Nominal media rotation rate */
  u16 reserved218;         /*  */
  u16 nv_cache_options;    /* NV Cache options */
  u16 words220_221[2];     /* reserved */
  u16 transport_major_rev; /*  */
  u16 transport_minor_rev; /*  */
  u16 words224_233[10];    /* Reserved */
  u16 min_dwnload_blocks;  /* Minimum number of 512byte units per
                             DOWNLOAD MICROCODE  command for mode 03h */
  u16 max_dwnload_blocks;  /* Maximum number of 512byte units per
                             DOWNLOAD MICROCODE  command for mode 03h */
  u16 words236_254[19];    /* Reserved */
  u16 integrity;           /* Cheksum, Signature */
} SATA_ident_t;
static u32      ahci_bus, ahci_slot, ahci_func, port, ahci_ports_base_addr;
static u32      drive_mapping[0xff];
static u32      ports[32];
static u32      port_total = 0;
static HBA_MEM *hba_mem_address;
static void     ahci_vdisk_read(char drive, u8 *buffer, u32 number, u32 lba);
static void     ahci_vdisk_write(char drive, u8 *buffer, u32 number, u32 lba);
static int      check_type(HBA_PORT *port) {
  u32 ssts = port->ssts;

  u8 ipm = (ssts >> 8) & 0x0F;
  u8 det = ssts & 0x0F;
  // https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf
  // 3.3.10
  if (det != HBA_PORT_DET_PRESENT) return AHCI_DEV_NULL;
  if (ipm != HBA_PORT_IPM_ACTIVE) return AHCI_DEV_NULL;

  switch (port->sig) {
  case SATA_SIG_ATAPI: return AHCI_DEV_SATAPI;
  case SATA_SIG_SEMB: return AHCI_DEV_SEMB;
  case SATA_SIG_PM: return AHCI_DEV_PM;
  default: return AHCI_DEV_SATA;
  }
}
void ahci_search_ports(HBA_MEM *abar) {
  // Search disk in implemented ports
  u32 pi = abar->pi;
  int i  = 0;
  while (i < 32) {
    if (pi & 1) {
      int dt = check_type(&abar->ports[i]);
      if (dt == AHCI_DEV_SATA) {
        logk("SATA drive found at port %d", i);
        port                = i;
        ports[port_total++] = i;
      } else if (dt == AHCI_DEV_SATAPI) {
        logk("SATAPI drive found at port %d", i);
      } else if (dt == AHCI_DEV_SEMB) {
        logk("SEMB drive found at port %d", i);
      } else if (dt == AHCI_DEV_PM) {
        logk("PM drive found at port %d", i);
      } else {
        logk("No drive found at port %d", i);
      }
    }

    pi >>= 1;
    i++;
  }
}
// Start command engine
void start_cmd(HBA_PORT *port) {
  // Wait until CR (bit15) is cleared
  while (port->cmd & HBA_PxCMD_CR)
    ;

  // Set FRE (bit4) and ST (bit0)
  port->cmd |= HBA_PxCMD_FRE;
  port->cmd |= HBA_PxCMD_ST;
}

// Stop command engine
void stop_cmd(HBA_PORT *port) {
  // Clear ST (bit0)
  port->cmd &= ~HBA_PxCMD_ST;

  // Clear FRE (bit4)
  port->cmd &= ~HBA_PxCMD_FRE;

  // Wait until FR (bit14), CR (bit15) are cleared
  while (1) {
    if (port->cmd & HBA_PxCMD_FR) continue;
    if (port->cmd & HBA_PxCMD_CR) continue;
    break;
  }
}

#define ATA_DEV_BUSY           0x80
#define ATA_DEV_DRQ            0x08
#define AHCI_CMD_READ_DMA_EXT  0x25
#define AHCI_CMD_WRITE_DMA_EXT 0x35
bool ahci_read(HBA_PORT *port, u32 startl, u32 starth, u32 count, u16 *buf) {
  port->is = (u32)-1; // Clear pending interrupt bits
  int spin = 0;       // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1) return false;

  HBA_CMD_HEADER *cmdheader  = (HBA_CMD_HEADER *)port->clb;
  cmdheader                 += slot;
  cmdheader->cfl             = sizeof(FIS_REG_H2D) / sizeof(u32); // Command FIS size
  cmdheader->w               = 0;                                 // Read from device
  cmdheader->c               = 1;
  cmdheader->p               = 1;
  cmdheader->prdtl           = (u16)((count - 1) >> 4) + 1; // PRDT entries count

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  // 8K bytes (16 sectors) per PRDT
  int i;
  for (i = 0; i < cmdheader->prdtl - 1; i++) {
    flush_cache(buf);
    cmdtbl->prdt_entry[i].dba  = (u32)buf;
    cmdtbl->prdt_entry[i].dbau = 0;
    cmdtbl->prdt_entry[i].dbc = 8 * 1024 - 1; // 8K bytes (this value should always be set to 1 less
                                              // than the actual value)
    cmdtbl->prdt_entry[i].i  = 1;
    buf                     += 4 * 1024; // 4K words
    count                   -= 16;       // 16 sectors
  }
  // Last entry
  cmdtbl->prdt_entry[i].dba = (u32)buf;
  cmdtbl->prdt_entry[i].dbc = (count << 9) - 1; // 512 bytes per sector
  cmdtbl->prdt_entry[i].i   = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c        = 1; // Command
  cmdfis->command  = AHCI_CMD_READ_DMA_EXT;

  cmdfis->lba0   = (u8)startl;
  cmdfis->lba1   = (u8)(startl >> 8);
  cmdfis->lba2   = (u8)(startl >> 16);
  cmdfis->device = 1 << 6; // LBA mode

  cmdfis->lba3 = (u8)(startl >> 24);
  cmdfis->lba4 = (u8)starth;
  cmdfis->lba5 = (u8)(starth >> 8);

  cmdfis->countl = count & 0xFF;
  cmdfis->counth = (count >> 8) & 0xFF;

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0) break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Read disk error");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Read disk error");
    return false;
  }

  flush_cache(buf);
  return true;
}

bool ahci_identify(HBA_PORT *port, void *buf) {
  port->is = (u32)-1; // Clear pending interrupt bits
  int spin = 0;       // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1) return false;

  HBA_CMD_HEADER *cmdheader  = (HBA_CMD_HEADER *)port->clb;
  cmdheader                 += slot;
  cmdheader->cfl             = sizeof(FIS_REG_H2D) / sizeof(u32); // Command FIS size
  cmdheader->w               = 0;                                 // Read from device
  cmdheader->prdtl           = 1;                                 // PRDT entries count
  cmdheader->c               = 1;
  HBA_CMD_TBL *cmdtbl        = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  cmdtbl->prdt_entry[0].dba  = (u32)buf;
  cmdtbl->prdt_entry[0].dbau = 0;
  cmdtbl->prdt_entry[0].dbc  = 0x200 - 1;
  cmdtbl->prdt_entry[0].i    = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c        = 1;    // Command
  cmdfis->command  = 0xec; // ATA IDENTIFY

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0) break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Read disk error");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Read disk error");
    return false;
  }

  return true;
}

bool ahci_write(HBA_PORT *port, u32 startl, u32 starth, u32 count, u16 *buf) {
  port->is = (u32)-1; // Clear pending interrupt bits
  int spin = 0;       // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1) return false;

  HBA_CMD_HEADER *cmdheader  = (HBA_CMD_HEADER *)port->clb;
  cmdheader                 += slot;
  cmdheader->cfl             = sizeof(FIS_REG_H2D) / sizeof(u32); // Command FIS size
  cmdheader->w               = 1;                                 // 写硬盘
  cmdheader->p               = 1;
  cmdheader->c               = 1;
  cmdheader->prdtl           = (u16)((count - 1) >> 4) + 1; // PRDT entries count

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  // 8K bytes (16 sectors) per PRDT
  int i;
  for (i = 0; i < cmdheader->prdtl - 1; i++) {
    flush_cache(buf);
    cmdtbl->prdt_entry[i].dba  = (u32)buf;
    cmdtbl->prdt_entry[i].dbau = 0;
    cmdtbl->prdt_entry[i].dbc = 8 * 1024 - 1; // 8K bytes (this value should always be set to 1 less
                                              // than the actual value)
    cmdtbl->prdt_entry[i].i  = 1;
    buf                     += 4 * 1024; // 4K words
    count                   -= 16;       // 16 sectors
  }
  // Last entry
  cmdtbl->prdt_entry[i].dba = (u32)buf;
  cmdtbl->prdt_entry[i].dbc = (count << 9) - 1; // 512 bytes per sector
  cmdtbl->prdt_entry[i].i   = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c        = 1; // Command
  cmdfis->command  = AHCI_CMD_WRITE_DMA_EXT;

  cmdfis->lba0   = (u8)startl;
  cmdfis->lba1   = (u8)(startl >> 8);
  cmdfis->lba2   = (u8)(startl >> 16);
  cmdfis->device = 1 << 6; // LBA mode

  cmdfis->lba3 = (u8)(startl >> 24);
  cmdfis->lba4 = (u8)starth;
  cmdfis->lba5 = (u8)(starth >> 8);

  cmdfis->countl = count & 0xFF;
  cmdfis->counth = (count >> 8) & 0xFF;

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0) break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Write disk error");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Write disk error");
    return false;
  }
  flush_cache(buf);
  return true;
}
// Find a free command list slot
int find_cmdslot(HBA_PORT *port) {
  // If not set in SACT and CI, the slot is free
  u32 slots    = (port->sact | port->ci);
  int cmdslots = (hba_mem_address->cap & 0x1f00) >> 8;
  for (int i = 0; i < cmdslots; i++) {
    if ((slots & 1) == 0) return i;
    slots >>= 1;
  }
  logk("Cannot find free command list entry");
  return -1;
}
void page_set_attr(unsigned start, unsigned end, unsigned attr, unsigned pde);
void port_rebase(HBA_PORT *port, int portno) {
  stop_cmd(port); // Stop command engine

  // Command list offset: 1K*portno
  // Command list entry size = 32
  // Command list entry maxim count = 32
  // Command list maxim size = 32*32 = 1K per port
  port->clb  = ahci_ports_base_addr + (portno << 10);
  port->clbu = 0;
  memset((void *)(port->clb), 0, 1024);

  // FIS offset: 32K+256*portno
  // FIS entry size = 256 bytes per port
  port->fb  = ahci_ports_base_addr + (32 << 10) + (portno << 8);
  port->fbu = 0;
  memset((void *)(port->fb), 0, 256);

  // Command table offset: 40K + 8K*portno
  // Command table size = 256*32 = 8K per port
  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)(port->clb);
  for (int i = 0; i < 32; i++) {
    cmdheader[i].prdtl = 8; // 8 prdt entries per command table
                            // 256 bytes per command table, 64+16+48+16*8
    // Command table offset: 40K + 8K*portno + cmdheader_index*256
    cmdheader[i].ctba  = ahci_ports_base_addr + (40 << 10) + (portno << 13) + (i << 8);
    cmdheader[i].ctbau = 0;
    memset((void *)cmdheader[i].ctba, 0, 256);
  }

  start_cmd(port); // Start command engine
}
static inline void cpuid(u32 leaf, u32 subleaf, u32 *regs) {
  asm volatile("cpuid"
               : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
               : "a"(leaf), "c"(subleaf));
}

// 获取缓存行大小
u32 get_cache_line_size() {
  u32 regs[4] = {0};
  cpuid(0x00000001, 0, regs);

  // EAX寄存器的第8-11位包含缓存行的字节数
  return (regs[1] >> 8) & 0xFF;
}

#define PAGE_SIZE 4096
int  cache_line_size = 0;
// 刷新缓存函数
void flush_cache(void *addr) {
  uintptr_t address = (uintptr_t)addr;

  // 计算需要刷新的页的起始地址
  uintptr_t page_start = address & ~(PAGE_SIZE - 1);

  // 遍历并刷新整页的所有缓存行
  for (uintptr_t cache_line  = page_start; cache_line < page_start + PAGE_SIZE;
       cache_line           += cache_line_size) {
    asm volatile("clflush (%0)" : : "r"(cache_line) : "memory");
  }
}
void ahci_init() {
  int i, j, k;
  int flag = 0;
  for (i = 0; i < 255; i++) {
    for (j = 0; j < 32; j++) {
      for (k = 0; k < 8; k++) {
        u32  p     = read_pci(i, j, k, 0x8);
        u16 *reg   = &p;        // reg[0] ---> P & R, reg[1] ---> Sub Class Class Code
        u8  *codes = &(reg[1]); // codes[0] --> Sub Class Code  codes[1] Class Code
        if (codes[1] == 0x1 && codes[0] == 0x6) {
          ahci_bus  = i;
          ahci_slot = j;
          ahci_func = k;
          flag      = 1;
          goto OK;
        }
      }
    }
  }
OK:
  if (!flag) {
    logk("Couldn't find AHCI Controller");
    return;
  }
  cache_line_size = get_cache_line_size();
  logk("cache line size = %d", cache_line_size);
  hba_mem_address = (HBA_MEM *)read_bar_n(ahci_bus, ahci_slot, ahci_func, 5);
  logk("HBA Address has been Mapped in %08x", hba_mem_address);
  // 设置允许中断产生
  u32 conf  = pci_read_command_status(ahci_bus, ahci_slot, ahci_func);
  conf     &= 0xffff0000;
  conf     |= 0x7;
  pci_write_command_status(ahci_bus, ahci_slot, ahci_func, conf);

  // 设置HBA中 GHC控制器的 AE（AHCI Enable）位，关闭AHCI控制器的IDE仿真模式
  hba_mem_address->ghc |= (1 << 31);
  logk("AHCI Enable = %08x", (hba_mem_address->ghc & (1 << 31)) >> 31);

  ahci_search_ports(hba_mem_address);

  ahci_ports_base_addr = page_malloc(1048576);

  cache = page_malloc(1048576);
  logk("AHCI port base address has been alloced in 0x%08x!", ahci_ports_base_addr);
  logk("The Useable Ports:");
  for (i = 0; i < port_total; i++) {
    logk("%d", ports[i]);
    port_rebase(&(hba_mem_address->ports[ports[i]]), ports[i]);
  }

  for (i = 0; i < port_total; i++) {
    SATA_ident_t buf;
    int          a = ahci_identify(&(hba_mem_address->ports[ports[i]]), &buf);
    if (!a) {
      logk("SATA Drive %d identify error.");
      continue;
    }
    logk("ports %d: total sector = %d", ports[i], buf.lba_capacity);
    vdisk vd;
    vd.flag  = 1;
    vd.Read  = ahci_vdisk_read;
    vd.Write = ahci_vdisk_write;
    vd.size  = buf.lba_capacity * 512;
    u8 drive = register_vdisk(vd);
    logk("drive: %c", drive);
    drive_mapping[drive] = ports[i];
  }
}
void io_delay(u32 delay_cycles) {
  volatile u32 i;

  for (i = 0; i < delay_cycles; ++i) {
    // 添加一些无用的操作来占用时间
    asm volatile("nop");
  }
}
static void ahci_vdisk_read(char drive, u8 *buffer, u32 number, u32 lba) {
  int i;
  for (i = 0; i < 5; i++)
    if (ahci_read(&(hba_mem_address->ports[drive_mapping[drive]]), lba, 0, number, cache)) {
      break;
    }
  if (i == 5) {
    printk("AHCI Read Error! Read %d %d\n", number, lba);
    while (true)
      ;
  }
  flush_cache(cache);
  flush_cache(cache + 0x1000);
  memcpy(buffer, cache, number * 512);
}
static void ahci_vdisk_write(char drive, u8 *buffer, u32 number, u32 lba) {
  memcpy(cache, buffer, number * 512);
  flush_cache(cache);
  flush_cache(cache + 0x1000);

  int i;
  for (i = 0; i < 5; i++)
    if (ahci_write(&(hba_mem_address->ports[drive_mapping[drive]]), lba, 0, number, cache)) {
      break;
    }
  if (i == 5) {
    printk("AHCI Write Error!\n");
    while (true)
      ;
  }
}