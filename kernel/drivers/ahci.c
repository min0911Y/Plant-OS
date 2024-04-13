// AHCI Controller Driver Implement

#include <dos.h>

static uint8_t *cache;
#define SATA_SIG_ATA 0x00000101   // SATA drive
#define SATA_SIG_ATAPI 0xEB140101 // SATAPI drive
#define SATA_SIG_SEMB 0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM 0x96690101    // Port multiplier
#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000
#define HBA_PxIS_TFES (1 << 30) /* TFES - Task File Error Status */

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM 3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3
typedef enum {
  FIS_TYPE_REG_H2D = 0x27,   // Register FIS - host to device
  FIS_TYPE_REG_D2H = 0x34,   // Register FIS - device to host
  FIS_TYPE_DMA_ACT = 0x39,   // DMA activate FIS - device to host
  FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
  FIS_TYPE_DATA = 0x46,      // Data FIS - bidirectional
  FIS_TYPE_BIST = 0x58,      // BIST activate FIS - bidirectional
  FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
  FIS_TYPE_DEV_BITS = 0xA1,  // Set device bits FIS - device to host
} FIS_TYPE;
typedef volatile struct tagHBA_PORT {
  uint32_t clb;       // 0x00, command list base address, 1K-byte aligned
  uint32_t clbu;      // 0x04, command list base address upper 32 bits
  uint32_t fb;        // 0x08, FIS base address, 256-byte aligned
  uint32_t fbu;       // 0x0C, FIS base address upper 32 bits
  uint32_t is;        // 0x10, interrupt status
  uint32_t ie;        // 0x14, interrupt enable
  uint32_t cmd;       // 0x18, command and status
  uint32_t rsv0;      // 0x1C, Reserved
  uint32_t tfd;       // 0x20, task file data
  uint32_t sig;       // 0x24, signature
  uint32_t ssts;      // 0x28, SATA status (SCR0:SStatus)
  uint32_t sctl;      // 0x2C, SATA control (SCR2:SControl)
  uint32_t serr;      // 0x30, SATA error (SCR1:SError)
  uint32_t sact;      // 0x34, SATA active (SCR3:SActive)
  uint32_t ci;        // 0x38, command issue
  uint32_t sntf;      // 0x3C, SATA notification (SCR4:SNotification)
  uint32_t fbs;       // 0x40, FIS-based switch control
  uint32_t rsv1[11];  // 0x44 ~ 0x6F, Reserved
  uint32_t vendor[4]; // 0x70 ~ 0x7F, vendor specific
} HBA_PORT;
typedef volatile struct tagHBA_MEM {
  // 0x00 - 0x2B, Generic Host Control
  uint32_t cap;     // 0x00, Host capability
  uint32_t ghc;     // 0x04, Global host control
  uint32_t is;      // 0x08, Interrupt status
  uint32_t pi;      // 0x0C, Port implemented
  uint32_t vs;      // 0x10, Version
  uint32_t ccc_ctl; // 0x14, Command completion coalescing control
  uint32_t ccc_pts; // 0x18, Command completion coalescing ports
  uint32_t em_loc;  // 0x1C, Enclosure management location
  uint32_t em_ctl;  // 0x20, Enclosure management control
  uint32_t cap2;    // 0x24, Host capabilities extended
  uint32_t bohc;    // 0x28, BIOS/OS handoff control and status

  // 0x2C - 0x9F, Reserved
  uint8_t rsv[0xA0 - 0x2C];

  // 0xA0 - 0xFF, Vendor specific registers
  uint8_t vendor[0x100 - 0xA0];

  // 0x100 - 0x10FF, Port control registers
  HBA_PORT ports[1]; // 1 ~ 32
} HBA_MEM;
typedef struct tagHBA_CMD_HEADER {
  // DW0
  uint8_t cfl : 5; // Command FIS length in DWORDS, 2 ~ 16
  uint8_t a : 1;   // ATAPI
  uint8_t w : 1;   // Write, 1: H2D, 0: D2H
  uint8_t p : 1;   // Prefetchable

  uint8_t r : 1;    // Reset
  uint8_t b : 1;    // BIST
  uint8_t c : 1;    // Clear busy upon R_OK
  uint8_t rsv0 : 1; // Reserved
  uint8_t pmp : 4;  // Port multiplier port

  uint16_t prdtl; // Physical region descriptor table length in entries

  // DW1
  volatile uint32_t prdbc; // Physical region descriptor byte count transferred

  // DW2, 3
  uint32_t ctba;  // Command table descriptor base address
  uint32_t ctbau; // Command table descriptor base address upper 32 bits

  // DW4 - 7
  uint32_t rsv1[4]; // Reserved
} HBA_CMD_HEADER;
typedef struct tagFIS_REG_H2D {
  // DWORD 0
  uint8_t fis_type; // FIS_TYPE_REG_H2D

  uint8_t pmport : 4; // Port multiplier
  uint8_t rsv0 : 3;   // Reserved
  uint8_t c : 1;      // 1: Command, 0: Control

  uint8_t command;  // Command register
  uint8_t featurel; // Feature register, 7:0

  // DWORD 1
  uint8_t lba0;   // LBA low register, 7:0
  uint8_t lba1;   // LBA mid register, 15:8
  uint8_t lba2;   // LBA high register, 23:16
  uint8_t device; // Device register

  // DWORD 2
  uint8_t lba3;     // LBA register, 31:24
  uint8_t lba4;     // LBA register, 39:32
  uint8_t lba5;     // LBA register, 47:40
  uint8_t featureh; // Feature register, 15:8

  // DWORD 3
  uint8_t countl;  // Count register, 7:0
  uint8_t counth;  // Count register, 15:8
  uint8_t icc;     // Isochronous command completion
  uint8_t control; // Control register

  // DWORD 4
  uint8_t rsv1[4]; // Reserved
} FIS_REG_H2D;
typedef struct tagFIS_REG_D2H {
  // DWORD 0
  uint8_t fis_type; // FIS_TYPE_REG_D2H

  uint8_t pmport : 4; // Port multiplier
  uint8_t rsv0 : 2;   // Reserved
  uint8_t i : 1;      // Interrupt bit
  uint8_t rsv1 : 1;   // Reserved

  uint8_t status; // Status register
  uint8_t error;  // Error register

  // DWORD 1
  uint8_t lba0;   // LBA low register, 7:0
  uint8_t lba1;   // LBA mid register, 15:8
  uint8_t lba2;   // LBA high register, 23:16
  uint8_t device; // Device register

  // DWORD 2
  uint8_t lba3; // LBA register, 31:24
  uint8_t lba4; // LBA register, 39:32
  uint8_t lba5; // LBA register, 47:40
  uint8_t rsv2; // Reserved

  // DWORD 3
  uint8_t countl;  // Count register, 7:0
  uint8_t counth;  // Count register, 15:8
  uint8_t rsv3[2]; // Reserved

  // DWORD 4
  uint8_t rsv4[4]; // Reserved
} FIS_REG_D2H;
typedef struct tagFIS_DATA {
  // DWORD 0
  uint8_t fis_type; // FIS_TYPE_DATA

  uint8_t pmport : 4; // Port multiplier
  uint8_t rsv0 : 4;   // Reserved

  uint8_t rsv1[2]; // Reserved

  // DWORD 1 ~ N
  uint32_t data[1]; // Payload
} FIS_DATA;
typedef struct tagFIS_PIO_SETUP {
  // DWORD 0
  uint8_t fis_type; // FIS_TYPE_PIO_SETUP

  uint8_t pmport : 4; // Port multiplier
  uint8_t rsv0 : 1;   // Reserved
  uint8_t d : 1;      // Data transfer direction, 1 - device to host
  uint8_t i : 1;      // Interrupt bit
  uint8_t rsv1 : 1;

  uint8_t status; // Status register
  uint8_t error;  // Error register

  // DWORD 1
  uint8_t lba0;   // LBA low register, 7:0
  uint8_t lba1;   // LBA mid register, 15:8
  uint8_t lba2;   // LBA high register, 23:16
  uint8_t device; // Device register

  // DWORD 2
  uint8_t lba3; // LBA register, 31:24
  uint8_t lba4; // LBA register, 39:32
  uint8_t lba5; // LBA register, 47:40
  uint8_t rsv2; // Reserved

  // DWORD 3
  uint8_t countl;   // Count register, 7:0
  uint8_t counth;   // Count register, 15:8
  uint8_t rsv3;     // Reserved
  uint8_t e_status; // New value of status register

  // DWORD 4
  uint16_t tc;     // Transfer count
  uint8_t rsv4[2]; // Reserved
} FIS_PIO_SETUP;
typedef struct tagFIS_DMA_SETUP {
  // DWORD 0
  uint8_t fis_type; // FIS_TYPE_DMA_SETUP

  uint8_t pmport : 4; // Port multiplier
  uint8_t rsv0 : 1;   // Reserved
  uint8_t d : 1;      // Data transfer direction, 1 - device to host
  uint8_t i : 1;      // Interrupt bit
  uint8_t a : 1;      // Auto-activate. Specifies if DMA Activate FIS is needed

  uint8_t rsved[2]; // Reserved

  // DWORD 1&2

  uint64_t DMAbufferID; // DMA Buffer Identifier. Used to Identify DMA buffer in
                        // host memory. SATA Spec says host specific and not in
                        // Spec. Trying AHCI spec might work.

  // DWORD 3
  uint32_t rsvd; // More reserved

  // DWORD 4
  uint32_t DMAbufOffset; // Byte offset into buffer. First 2 bits must be 0

  // DWORD 5
  uint32_t TransferCount; // Number of bytes to transfer. Bit 0 must be 0

  // DWORD 6
  uint32_t resvd; // Reserved

} FIS_DMA_SETUP;
typedef struct tagHBA_PRDT_ENTRY {
  uint32_t dba;  // Data base address
  uint32_t dbau; // Data base address upper 32 bits
  uint32_t rsv0; // Reserved

  // DW3
  uint32_t dbc : 22; // Byte count, 4M max
  uint32_t rsv1 : 9; // Reserved
  uint32_t i : 1;    // Interrupt on completion
} HBA_PRDT_ENTRY;
typedef struct tagHBA_CMD_TBL {
  // 0x00
  uint8_t cfis[64]; // Command FIS

  // 0x40
  uint8_t acmd[16]; // ATAPI command, 12 or 16 bytes

  // 0x50
  uint8_t rsv[48]; // Reserved

  // 0x80
  HBA_PRDT_ENTRY
  prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TBL;
typedef struct SATA_Ident {
  unsigned short config;       /* lots of obsolete bit flags */
  unsigned short cyls;         /* obsolete */
  unsigned short reserved2;    /* special config */
  unsigned short heads;        /* "physical" heads */
  unsigned short track_bytes;  /* unformatted bytes per track */
  unsigned short sector_bytes; /* unformatted bytes per sector */
  unsigned short sectors;      /* "physical" sectors per track */
  unsigned short vendor0;      /* vendor unique */
  unsigned short vendor1;      /* vendor unique */
  unsigned short vendor2;      /* vendor unique */
  unsigned char serial_no[20]; /* 0 = not_specified */
  unsigned short buf_type;
  unsigned short buf_size;    /* 512 byte increments; 0 = not_specified */
  unsigned short ecc_bytes;   /* for r/w long cmds; 0 = not_specified */
  unsigned char fw_rev[8];    /* 0 = not_specified */
  unsigned char model[40];    /* 0 = not_specified */
  unsigned short multi_count; /* Multiple Count */
  unsigned short dword_io;    /* 0=not_implemented; 1=implemented */
  unsigned short capability1; /* vendor unique */
  unsigned short
      capability2;       /* bits 0:DMA 1:LBA 2:IORDYsw 3:IORDYsup word: 50 */
  unsigned char vendor5; /* vendor unique */
  unsigned char tPIO;    /* 0=slow, 1=medium, 2=fast */
  unsigned char vendor6; /* vendor unique */
  unsigned char tDMA;    /* 0=slow, 1=medium, 2=fast */
  unsigned short field_valid;      /* bits 0:cur_ok 1:eide_ok */
  unsigned short cur_cyls;         /* logical cylinders */
  unsigned short cur_heads;        /* logical heads word 55*/
  unsigned short cur_sectors;      /* logical sectors per track */
  unsigned short cur_capacity0;    /* logical total sectors on drive */
  unsigned short cur_capacity1;    /*  (2 words, misaligned int)     */
  unsigned char multsect;          /* current multiple sector count */
  unsigned char multsect_valid;    /* when (bit0==1) multsect is ok */
  unsigned int lba_capacity;       /* total number of sectors */
  unsigned short dma_1word;        /* single-word dma info */
  unsigned short dma_mword;        /* multiple-word dma info */
  unsigned short eide_pio_modes;   /* bits 0:mode3 1:mode4 */
  unsigned short eide_dma_min;     /* min mword dma cycle time (ns) */
  unsigned short eide_dma_time;    /* recommended mword dma cycle time (ns) */
  unsigned short eide_pio;         /* min cycle time (ns), no IORDY  */
  unsigned short eide_pio_iordy;   /* min cycle time (ns), with IORDY */
  unsigned short words69_70[2];    /* reserved words 69-70 */
  unsigned short words71_74[4];    /* reserved words 71-74 */
  unsigned short queue_depth;      /*  */
  unsigned short sata_capability;  /*  SATA Capabilities word 76*/
  unsigned short sata_additional;  /*  Additional Capabilities */
  unsigned short sata_supported;   /* SATA Features supported  */
  unsigned short features_enabled; /* SATA features enabled */
  unsigned short major_rev_num;    /*  Major rev number word 80 */
  unsigned short minor_rev_num;    /*  */
  unsigned short command_set_1; /* bits 0:Smart 1:Security 2:Removable 3:PM */
  unsigned short command_set_2; /* bits 14:Smart Enabled 13:0 zero */
  unsigned short cfsse;         /* command set-feature supported extensions */
  unsigned short cfs_enable_1;  /* command set-feature enabled */
  unsigned short cfs_enable_2;  /* command set-feature enabled */
  unsigned short csf_default;   /* command set-feature default */
  unsigned short dma_ultra;     /*  */
  unsigned short word89;        /* reserved (word 89) */
  unsigned short word90;        /* reserved (word 90) */
  unsigned short CurAPMvalues;  /* current APM values */
  unsigned short word92;        /* reserved (word 92) */
  unsigned short comreset;      /* should be cleared to 0 */
  unsigned short accoustic;     /*  accoustic management */
  unsigned short min_req_sz;    /* Stream minimum required size */
  unsigned short transfer_time_dma; /* Streaming Transfer Time-DMA */
  unsigned short access_latency; /* Streaming access latency-DMA & PIO WORD 97*/
  unsigned int perf_granularity; /* Streaming performance granularity */
  unsigned int
      total_usr_sectors[2]; /* Total number of user addressable sectors */
  unsigned short transfer_time_pio; /* Streaming Transfer time PIO */
  unsigned short reserved105;       /* Word 105 */
  unsigned short sector_sz; /* Puysical Sector size / Logical sector size */
  unsigned short inter_seek_delay;   /* In microseconds */
  unsigned short words108_116[9];    /*  */
  unsigned int words_per_sector;     /* words per logical sectors */
  unsigned short supported_settings; /* continued from words 82-84 */
  unsigned short command_set_3;      /* continued from words 85-87 */
  unsigned short words121_126[6];    /* reserved words 121-126 */
  unsigned short word127;            /* reserved (word 127) */
  unsigned short security_status;    /* device lock function
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
  unsigned short csfo;               /* current set features options
                                      * 15:4   reserved
                                      * 3   auto reassign
                                      * 2   reverting
                                      * 1   read-look-ahead
                                      * 0   write cache
                                      */
  unsigned short words130_155[26];   /* reserved vendor words 130-155 */
  unsigned short word156;
  unsigned short words157_159[3];  /* reserved vendor words 157-159 */
  unsigned short cfa;              /* CFA Power mode 1 */
  unsigned short words161_175[15]; /* Reserved */
  unsigned char
      media_serial[60]; /* words 176-205 Current Media serial number */
  unsigned short sct_cmd_transport; /* SCT Command Transport */
  unsigned short words207_208[2];   /* reserved */
  unsigned short
      block_align; /* Alignement of logical blocks in larger physical blocks */
  unsigned int WRV_sec_count;  /* Write-Read-Verify sector count mode 3 only */
  unsigned int verf_sec_count; /* Verify Sector count mode 2 only */
  unsigned short nv_cache_capability; /* NV Cache capabilities */
  unsigned short nv_cache_sz;         /* NV Cache size in logical blocks */
  unsigned short nv_cache_sz2;        /* NV Cache size in logical blocks */
  unsigned short rotation_rate;       /* Nominal media rotation rate */
  unsigned short reserved218;         /*  */
  unsigned short nv_cache_options;    /* NV Cache options */
  unsigned short words220_221[2];     /* reserved */
  unsigned short transport_major_rev; /*  */
  unsigned short transport_minor_rev; /*  */
  unsigned short words224_233[10];    /* Reserved */
  unsigned short min_dwnload_blocks;  /* Minimum number of 512byte units per
                             DOWNLOAD MICROCODE  command for mode 03h */
  unsigned short max_dwnload_blocks;  /* Maximum number of 512byte units per
                             DOWNLOAD MICROCODE  command for mode 03h */
  unsigned short words236_254[19];    /* Reserved */
  unsigned short integrity;           /* Cheksum, Signature */
} SATA_ident_t;
static uint32_t ahci_bus, ahci_slot, ahci_func, port, ahci_ports_base_addr;
static uint32_t drive_mapping[0xff];
static uint32_t ports[32];
static uint32_t port_total = 0;
static HBA_MEM *hba_mem_address;
static void ahci_vdisk_read(char drive, unsigned char *buffer,
                            unsigned int number, unsigned int lba);
static void ahci_vdisk_write(char drive, unsigned char *buffer,
                             unsigned int number, unsigned int lba);
static int check_type(HBA_PORT *port) {
  uint32_t ssts = port->ssts;

  uint8_t ipm = (ssts >> 8) & 0x0F;
  uint8_t det = ssts & 0x0F;
  // https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf
  // 3.3.10
  if (det != HBA_PORT_DET_PRESENT)
    return AHCI_DEV_NULL;
  if (ipm != HBA_PORT_IPM_ACTIVE)
    return AHCI_DEV_NULL;

  switch (port->sig) {
  case SATA_SIG_ATAPI:
    return AHCI_DEV_SATAPI;
  case SATA_SIG_SEMB:
    return AHCI_DEV_SEMB;
  case SATA_SIG_PM:
    return AHCI_DEV_PM;
  default:
    return AHCI_DEV_SATA;
  }
}
void ahci_search_ports(HBA_MEM *abar) {
  // Search disk in implemented ports
  uint32_t pi = abar->pi;
  int i = 0;
  while (i < 32) {
    if (pi & 1) {
      int dt = check_type(&abar->ports[i]);
      if (dt == AHCI_DEV_SATA) {
        logk("SATA drive found at port %d\n", i);
        port = i;
        ports[port_total++] = i;
      } else if (dt == AHCI_DEV_SATAPI) {
        logk("SATAPI drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_SEMB) {
        logk("SEMB drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_PM) {
        logk("PM drive found at port %d\n", i);
      } else {
        logk("No drive found at port %d\n", i);
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
    if (port->cmd & HBA_PxCMD_FR)
      continue;
    if (port->cmd & HBA_PxCMD_CR)
      continue;
    break;
  }
}

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08
#define AHCI_CMD_READ_DMA_EXT 0x25
#define AHCI_CMD_WRITE_DMA_EXT 0x35
bool ahci_read(HBA_PORT *port, uint32_t startl, uint32_t starth, uint32_t count,
               uint16_t *buf) {
  port->is = (uint32_t)-1; // Clear pending interrupt bits
  int spin = 0;            // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1)
    return false;

  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)port->clb;
  cmdheader += slot;
  cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // Command FIS size
  cmdheader->w = 0;                                        // Read from device
  cmdheader->c = 1;
  cmdheader->p = 1;
  cmdheader->prdtl = (uint16_t)((count - 1) >> 4) + 1; // PRDT entries count

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0,
         sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  // 8K bytes (16 sectors) per PRDT
  int i;
  for (i = 0; i < cmdheader->prdtl - 1; i++) {
    cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
    cmdtbl->prdt_entry[i].dbau = 0;
    cmdtbl->prdt_entry[i].dbc =
        8 * 1024 - 1; // 8K bytes (this value should always be set to 1 less
                      // than the actual value)
    cmdtbl->prdt_entry[i].i = 1;
    buf += 4 * 1024; // 4K words
    count -= 16;     // 16 sectors
  }
  // Last entry
  cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
  cmdtbl->prdt_entry[i].dbc = (count << 9) - 1; // 512 bytes per sector
  cmdtbl->prdt_entry[i].i = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c = 1; // Command
  cmdfis->command = AHCI_CMD_READ_DMA_EXT;

  cmdfis->lba0 = (uint8_t)startl;
  cmdfis->lba1 = (uint8_t)(startl >> 8);
  cmdfis->lba2 = (uint8_t)(startl >> 16);
  cmdfis->device = 1 << 6; // LBA mode

  cmdfis->lba3 = (uint8_t)(startl >> 24);
  cmdfis->lba4 = (uint8_t)starth;
  cmdfis->lba5 = (uint8_t)(starth >> 8);

  cmdfis->countl = count & 0xFF;
  cmdfis->counth = (count >> 8) & 0xFF;

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung\n");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0)
      break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Read disk error\n");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Read disk error\n");
    return false;
  }

  return true;
}

bool ahci_identify(HBA_PORT *port, void *buf) {
  port->is = (uint32_t)-1; // Clear pending interrupt bits
  int spin = 0;            // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1)
    return false;

  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)port->clb;
  cmdheader += slot;
  cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // Command FIS size
  cmdheader->w = 0;                                        // Read from device
  cmdheader->prdtl = 1;                                    // PRDT entries count
  cmdheader->c = 1;
  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0,
         sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  cmdtbl->prdt_entry[0].dba = (uint32_t)buf;
  cmdtbl->prdt_entry[0].dbau = 0;
  cmdtbl->prdt_entry[0].dbc = 0x200 - 1;
  cmdtbl->prdt_entry[0].i = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c = 1;          // Command
  cmdfis->command = 0xec; // ATA IDENTIFY

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung\n");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0)
      break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Read disk error\n");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Read disk error\n");
    return false;
  }

  return true;
}

bool ahci_write(HBA_PORT *port, uint32_t startl, uint32_t starth,
                uint32_t count, uint16_t *buf) {
  port->is = (uint32_t)-1; // Clear pending interrupt bits
  int spin = 0;            // Spin lock timeout counter
  int slot = find_cmdslot(port);
  if (slot == -1)
    return false;

  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)port->clb;
  cmdheader += slot;
  cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // Command FIS size
  cmdheader->w = 1;                                        // 写硬盘
  cmdheader->p = 1;
  cmdheader->c = 1;
  cmdheader->prdtl = (uint16_t)((count - 1) >> 4) + 1; // PRDT entries count

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba);
  memset(cmdtbl, 0,
         sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) * sizeof(HBA_PRDT_ENTRY));

  // 8K bytes (16 sectors) per PRDT
  int i;
  for (i = 0; i < cmdheader->prdtl - 1; i++) {
    cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
    cmdtbl->prdt_entry[i].dbau = 0;
    cmdtbl->prdt_entry[i].dbc =
        8 * 1024 - 1; // 8K bytes (this value should always be set to 1 less
                      // than the actual value)
    cmdtbl->prdt_entry[i].i = 1;
    buf += 4 * 1024; // 4K words
    count -= 16;     // 16 sectors
  }
  // Last entry
  cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
  cmdtbl->prdt_entry[i].dbc = (count << 9) - 1; // 512 bytes per sector
  cmdtbl->prdt_entry[i].i = 1;

  // Setup command
  FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;
  cmdfis->c = 1; // Command
  cmdfis->command = AHCI_CMD_WRITE_DMA_EXT;

  cmdfis->lba0 = (uint8_t)startl;
  cmdfis->lba1 = (uint8_t)(startl >> 8);
  cmdfis->lba2 = (uint8_t)(startl >> 16);
  cmdfis->device = 1 << 6; // LBA mode

  cmdfis->lba3 = (uint8_t)(startl >> 24);
  cmdfis->lba4 = (uint8_t)starth;
  cmdfis->lba5 = (uint8_t)(starth >> 8);

  cmdfis->countl = count & 0xFF;
  cmdfis->counth = (count >> 8) & 0xFF;

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }
  if (spin == 1000000) {
    logk("Port is hung\n");
    return false;
  }

  port->ci = 1 << slot; // Issue command

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((port->ci & (1 << slot)) == 0)
      break;
    if (port->is & HBA_PxIS_TFES) // Task file error
    {
      logk("Write disk error\n");
      return false;
    }
  }

  // Check again
  if (port->is & HBA_PxIS_TFES) {
    logk("Write disk error\n");
    return false;
  }

  return true;
}
// Find a free command list slot
int find_cmdslot(HBA_PORT *port) {
  // If not set in SACT and CI, the slot is free
  uint32_t slots = (port->sact | port->ci);
  int cmdslots = (hba_mem_address->cap & 0x1f00) >> 8;
  for (int i = 0; i < cmdslots; i++) {
    if ((slots & 1) == 0)
      return i;
    slots >>= 1;
  }
  logk("Cannot find free command list entry\n");
  return -1;
}
void page_set_attr(unsigned start, unsigned end, unsigned attr, unsigned pde);
void port_rebase(HBA_PORT *port, int portno) {
  stop_cmd(port); // Stop command engine

  // Command list offset: 1K*portno
  // Command list entry size = 32
  // Command list entry maxim count = 32
  // Command list maxim size = 32*32 = 1K per port
  port->clb = ahci_ports_base_addr + (portno << 10);
  port->clbu = 0;
  memset((void *)(port->clb), 0, 1024);

  // FIS offset: 32K+256*portno
  // FIS entry size = 256 bytes per port
  port->fb = ahci_ports_base_addr + (32 << 10) + (portno << 8);
  port->fbu = 0;
  memset((void *)(port->fb), 0, 256);

  // Command table offset: 40K + 8K*portno
  // Command table size = 256*32 = 8K per port
  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)(port->clb);
  for (int i = 0; i < 32; i++) {
    cmdheader[i].prdtl = 8; // 8 prdt entries per command table
                            // 256 bytes per command table, 64+16+48+16*8
    // Command table offset: 40K + 8K*portno + cmdheader_index*256
    cmdheader[i].ctba =
        ahci_ports_base_addr + (40 << 10) + (portno << 13) + (i << 8);
    cmdheader[i].ctbau = 0;
    memset((void *)cmdheader[i].ctba, 0, 256);
  }

  start_cmd(port); // Start command engine
}

void ahci_init() {
  int i, j, k;
  int flag = 0;
  for (i = 0; i < 255; i++) {
    for (j = 0; j < 32; j++) {
      for (k = 0; k < 8; k++) {
        uint32_t p = read_pci(i, j, k, 0x8);
        uint16_t *reg =
            &p; // reg[0] ---> P & R, reg[1] ---> Sub Class Class Code
        uint8_t *codes =
            &(reg[1]); // codes[0] --> Sub Class Code  codes[1] Class Code
        if (codes[1] == 0x1 && codes[0] == 0x6) {
          ahci_bus = i;
          ahci_slot = j;
          ahci_func = k;
          flag = 1;
          goto OK;
        }
      }
    }
  }
OK:
  if (!flag) {
    logk("Couldn't find AHCI Controller\n");
    return;
  }
  hba_mem_address = (HBA_MEM *)read_bar_n(ahci_bus, ahci_slot, ahci_func, 5);
  logk("HBA Address has been Mapped in %08x ", hba_mem_address);
  // 设置允许中断产生
  uint32_t conf = pci_read_command_status(ahci_bus, ahci_slot, ahci_func);
  conf &= 0xffff0000;
  conf |= 0x7;
  pci_write_command_status(ahci_bus, ahci_slot, ahci_func, conf);

  // 设置HBA中 GHC控制器的 AE（AHCI Enable）位，关闭AHCI控制器的IDE仿真模式
  hba_mem_address->ghc |= (1 << 31);
  logk("AHCI Enable = %08x\n", (hba_mem_address->ghc & (1 << 31)) >> 31);

  ahci_search_ports(hba_mem_address);

  ahci_ports_base_addr = page_malloc(1048576);

  cache = page_malloc_one_no_mark();
  page_set_physics_attr(cache,cache,PG_P | PG_PCD | PG_RWW | (1 << 3) | (1 << 8));
  logk("AHCI port base address has been alloced in 0x%08x!\n",
       ahci_ports_base_addr);
  logk("The Useable Ports:");
  for (i = 0; i < port_total; i++) {
    logk("%d ", ports[i]);
    port_rebase(&(hba_mem_address->ports[ports[i]]), ports[i]);
  }
  logk("\n");

  for (i = 0; i < port_total; i++) {
    SATA_ident_t buf;
    int a = ahci_identify(&(hba_mem_address->ports[ports[i]]), &buf);
    if (!a) {
      logk("SATA Drive %d identify error.\n");
      continue;
    }
    logk("ports %d: total sector = %d\n", ports[i], buf.lba_capacity);
    vdisk vd;
    vd.flag = 1;
    vd.Read = ahci_vdisk_read;
    vd.Write = ahci_vdisk_write;
    vd.size = buf.lba_capacity * 512;
    uint8_t drive = register_vdisk(vd);
    logk("drive: %c\n", drive);
    drive_mapping[drive] = ports[i];
  }
}

static void ahci_vdisk_read(char drive, unsigned char *buffer,
                            unsigned int number, unsigned int lba) {
  memset(cache,0,0x1000);
  int i;
  for (i = 0; i < 5; i++)
    if (ahci_read(&(hba_mem_address->ports[drive_mapping[drive]]), lba, 0,
                  number,
                  cache)) {
      break;
    }
  if (i == 5) {
      printk("AHCI Read Error! Read %d %d\n",number,lba);
      for (;;)
        ;
  }
  memcpy(buffer,cache,number*512);
}
static void ahci_vdisk_write(char drive, unsigned char *buffer,
                             unsigned int number, unsigned int lba) {
  memset(cache,0,0x1000);
  memcpy(cache, buffer, number * 512);
  int i;
  for (i = 0; i < 5; i++)
    if (ahci_write(&(hba_mem_address->ports[drive_mapping[drive]]), lba, 0,
                   number,
                   cache)) {
      break;
    }
  if (i == 5) {
      printk("AHCI Write Error!\n");
      for (;;)
        ;
  }
}