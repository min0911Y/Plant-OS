#ifndef _DRIVERS_H
#define _DRIVERS_H
#include <kernel.h>
// acpi.c
char checksum(u8 *addr, u32 length);
u32 *acpi_find_rsdp(void);
u32  acpi_find_table(char *Signature);
void init_acpi(void);
int  acpi_shutdown(void);

typedef struct {
  u64 sec;
  u64 nsec;
} time_ns_t;

void gettime_ns(time_ns_t *ptr);
void usleep(u64 time_us);

// beep.c
void beep(int point, int notes, int dup);

// cmos.c
u8   read_cmos(u8 p);
void write_cmos(u8 p, u8 data);
u32  get_hour_hex();
u32  get_min_hex();
u32  get_sec_hex();
u32  get_day_of_month();
u32  get_day_of_week();
u32  get_mon_hex();
u32  get_year();
void write_cmos_time(u32 year, u8 mon, u8 day, u8 hour, u8 min);

// com.c
int  init_serial(void);
int  serial_received();
char read_serial();
int  is_transmit_empty();
void write_serial(char a);

// dma.c
void dma_xfer(u8 channel, u32 address, u32 length, u8 read);

// floppy.c
void init_floppy();
void flint(int *esp);
int  fdc_rw(int block, u8 *blockbuff, int read, u32 nosectors);
int  fdc_rw_ths(int track, int head, int sector, u8 *blockbuff, int read, u32 nosectors);
int  read_block(int block, u8 *blockbuff, u32 nosectors);
int  write_block(int block, u8 *blockbuff, u32 nosectors);
int  write_floppy_for_ths(int track, int head, int sec, u8 *blockbuff, u32 nosec);
void block2hts(int block, int *track, int *head, int *sector);
void hts2block(int track, int head, int sector, int *block);

// harddisk.c
void                               drivers_idehdd_read(u32 LBA, u32 number, u16 *buffer);
void                               drivers_idehdd_write(u32 LBA, u32 number, u16 *buffer);
struct IDEHardDiskInfomationBlock *drivers_idehdd_info();

// keyboard.c
void wait_KBC_sendready(void);
void init_keyboard(void);
int  getch();
int  input_char_inSM();
void inthandler21(int *esp);
int  kbhit();

// mouse.c
void enable_mouse(struct MOUSE_DEC *mdec);
void mouse_sleep(struct MOUSE_DEC *mdec);
void mouse_ready(struct MOUSE_DEC *mdec);
int  mouse_decode(struct MOUSE_DEC *mdec, u8 dat);
void mouseinput();
void inthandler2c(int *esp);

// pci.c
u32  read_pci(u8 bus, u8 device, u8 function, u8 registeroffset);
void write_pci(u8 bus, u8 device, u8 function, u8 registeroffset, u32 value);
u32  pci_read_command_status(u8 bus, u8 slot, u8 func);
void pci_write_command_status(u8 bus, u8 slot, u8 func, u32 value);
u8   pci_get_drive_irq(u8 bus, u8 slot, u8 func);
u32  pci_get_port_base(u8 bus, u8 slot, u8 func);
void PCI_GET_DEVICE(u16 vendor_id, u16 device_id, u8 *bus, u8 *slot, u8 *func);
void pci_config(u32 Bus, u32 f, u32 equipment, u32 adder);
u32  read_bar_n(u8 bus, u8 device, u8 function, u8 bar_n);
void init_PCI(u32 adder_Base);
void PCI_ClassCode_Print(struct pci_config_space_public *pci_config_space_puclic);

// pic.c
void init_pic(void);
void send_eoi(int irq);
void irq_mask_clear(u8 irq);
void irq_mask_set(u8 irq);

// pcnet.c
void into_32bitsRW();
void into_16bitsRW();
void reset_card();
void Activate();
void init_pcnet_card();
void PcnetSend(u8 *buffer, int size);
void PCNET_IRQ(int *esp);

// vbe.c
int           SwitchVBEMode(int mode);
int           check_vbe_mode(int mode, struct VBEINFO *vinfo);
void          SwitchToText8025_BIOS();
void          SwitchTo320X200X256_BIOS();
void         *GetSVGACardMemAddress();
char         *GetSVGACharOEMString();
VESAModeInfo *GetVESAModeInfo(int mode);
void          get_all_mode();
unsigned      set_mode(int width, int height, int bpp);

// vga.c
void     write_regs(u8 *regs);
void     SwitchTo320X200X256();
void     SwitchToText8025();
void     Set_Font(char *file);
void     pokew(int setmentaddr, int offset, i16 value);
void     pokeb(int setmentaddr, int offset, char value);
void     set_palette(int start, int end, u8 *rgb);
void     init_palette(void);
unsigned get_fb_seg(void);

// driver.c
void  init_driver();
drv_t driver_malloc(char *drv_file, drv_type_t drv_type);
void  driver_free(drv_t driver);
void  driver_call(drv_t driver, int func, void *arg);
void  driver_set_handler(drv_t driver, int func_addr, int handler_num);
drv_t driver_find(drv_type_t type);

// vdisk.c
int  init_vdisk();
int  register_vdisk(vdisk vd);
int  logout_vdisk(char drive);
int  rw_vdisk(char drive, u32 lba, u8 *buffer, u32 number, int read);
bool have_vdisk(char drive);
// rtl8139.c
bool rtl8139_find_card();
void init_rtl8139_card();
// timer.c
void sleep(u64 s);
// ide.c
void ide_read_sectors(u8 drive, u8 numsects, u32 lba, u16 es, u32 edi);
void ide_write_sectors(u8 drive, u8 numsects, u32 lba, u16 es, u32 edi);
void ide_initialize(u32 BAR0, u32 BAR1, u32 BAR2, u32 BAR3, u32 BAR4);
// network.c
void init_networkCTL();
void init_network();
void init_card();
#endif
