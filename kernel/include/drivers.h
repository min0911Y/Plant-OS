#ifndef _DRIVERS_H
#define _DRIVERS_H
#include <define.h>
#include <module.h>
// acpi.c
char checksum(unsigned char *addr, unsigned int length);
unsigned int *acpi_find_rsdp(void);
void *acpi_find_table(char *Signature);
void init_acpi(void);
int acpi_shutdown(void);
uint64_t nanoTime(void);
int acpi_have_madt(void);
uint32_t acpi_lapic_address(void);
uint32_t acpi_cpu_count(void);
uint32_t acpi_bsp_lapic_id(void);
uint32_t acpi_cpu_lapic_id(uint32_t index);
uint32_t acpi_ioapic_count(void);
uint32_t acpi_ioapic_address(uint32_t index);
uint32_t acpi_ioapic_gsi_base(uint32_t index);
uint32_t acpi_irq_to_gsi(uint32_t irq);
int acpi_irq_trigger_mode(uint32_t irq);
int acpi_irq_polarity(uint32_t irq);
int acpi_isa_irq_overridden(uint32_t irq);
// beep.c
void beep(int point, int notes, int dup);
// cmos.c
unsigned char read_cmos(unsigned char p);
void write_cmos(unsigned char p, unsigned char data);
unsigned int get_hour_hex();
unsigned int get_min_hex();
unsigned int get_sec_hex();
unsigned int get_day_of_month();
unsigned int get_day_of_week();
unsigned int get_mon_hex();
unsigned int get_year();
void write_cmos_time(unsigned int year, unsigned char mon, unsigned char day,
                     unsigned char hour, unsigned char min);
// com.c
int init_serial(void);
int serial_received();
char read_serial();
int is_transmit_empty();
void write_serial(char a);
// dma.c
void dma_xfer(unsigned char channel, unsigned long address, unsigned int length,
              unsigned char read);
// floppy.c
void init_floppy();
void flint(int *esp);
int fdc_rw(int block, unsigned char *blockbuff, int read,
           unsigned long nosectors);
int fdc_rw_ths(int track, int head, int sector, unsigned char *blockbuff,
               int read, unsigned long nosectors);
int read_block(int block, unsigned char *blockbuff, unsigned long nosectors);
int write_block(int block, unsigned char *blockbuff, unsigned long nosectors);
int write_floppy_for_ths(int track, int head, int sec, unsigned char *blockbuff,
                         unsigned long nosec);
void block2hts(int block, int *track, int *head, int *sector);
void hts2block(int track, int head, int sector, int *block);
// harddisk.c
void drivers_idehdd_read(unsigned int LBA, unsigned int number,
                         unsigned short *buffer);
void drivers_idehdd_write(unsigned int LBA, unsigned int number,
                          unsigned short *buffer);
struct IDEHardDiskInfomationBlock *drivers_idehdd_info();
// keyboard.c
void wait_KBC_sendready(void);
void init_keyboard(void);
int getch();
int input_char_inSM();
void inthandler21(int *esp);
int kbhit();
// mouse.c
void enable_mouse(struct MOUSE_DEC *mdec);
void mouse_sleep(struct MOUSE_DEC *mdec);
void mouse_ready(struct MOUSE_DEC *mdec);
int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat);
void mouseinput();
void inthandler2c(int *esp);
// pci.c
uint32_t read_pci(uint8_t bus, uint8_t device, uint8_t function,
                  uint8_t registeroffset);
void write_pci(uint8_t bus, uint8_t device, uint8_t function,
               uint8_t registeroffset, uint32_t value);
uint32_t pci_read_command_status(uint8_t bus, uint8_t slot, uint8_t func);
void pci_write_command_status(uint8_t bus, uint8_t slot, uint8_t func,
                              uint32_t value);
uint8_t pci_get_drive_irq(uint8_t bus, uint8_t slot, uint8_t func);
uint32_t pci_get_port_base(uint8_t bus, uint8_t slot, uint8_t func);
void PCI_GET_DEVICE(uint16_t vendor_id, uint16_t device_id, uint8_t *bus,
                    uint8_t *slot, uint8_t *func);
void pci_config(unsigned int Bus, unsigned int f, unsigned int equipment,
                       unsigned int adder);
uint32_t read_bar_n(uint8_t bus, uint8_t device, uint8_t function,
                    uint8_t bar_n);
void init_PCI(unsigned int adder_Base);
void PCI_ClassCode_Print(
    struct pci_config_space_public *pci_config_space_puclic);
// pic.c
void init_pic(void);
void pic_disable(void);
void send_eoi(int irq);
bool irq_is_valid(unsigned irq);
void irq_mask_clear(unsigned irq);
void irq_mask_set(unsigned irq);
void irq_configure(unsigned irq, int trigger_mode, int polarity);
int interrupt_controller_uses_apic(void);
void apic_init(void);
int apic_ready(void);
int apic_x2apic_enabled(void);
int apic_timer_uses_tsc_deadline(void);
int apic_timer_tsc_deadline_available(void);
uint32_t apic_current_id(void);
void apic_send_eoi(void);
void apic_timer_on_interrupt(void);
int apic_timer_use_tsc_deadline(void);
void apic_timer_use_irq0(void);
void apic_route_irq(unsigned irq, unsigned char vector);
void apic_configure_irq(unsigned irq, int trigger_mode, int polarity);
void apic_mask_irq(unsigned irq);
void apic_unmask_irq(unsigned irq);
void apic_mask_all_irqs(void);
void apic_send_init_ipi(uint32_t apic_id);
void apic_send_startup_ipi(uint32_t apic_id, uint32_t vector);
uint8_t *smp_trampoline_base(void);
uint32_t smp_trampoline_phys(void);
uint32_t smp_cpu_count(void);
uint32_t smp_bsp_lapic_id(void);
uint32_t smp_cpu_lapic_id(uint32_t index);
int smp_cpu_started(uint32_t index);
// vbe.c
int SwitchVBEMode(int mode);
int check_vbe_mode(int mode, struct VBEINFO *vinfo);
void SwitchToText8025_BIOS();
void SwitchTo320X200X256_BIOS();
void *GetSVGACardMemAddress();
char *GetSVGACharOEMString();
VESAModeInfo *GetVESAModeInfo(int mode);
void get_all_mode();
unsigned set_mode(int width, int height, int bpp);
// vga.c
void write_regs(unsigned char *regs);
void SwitchTo320X200X256();
void SwitchToText8025();
void Set_Font(char *file);
void pokew(int setmentaddr, int offset, short value);
void pokeb(int setmentaddr, int offset, char value);
void set_palette(int start, int end, unsigned char *rgb);
void init_palette(void);
unsigned get_fb_seg(void);
// driver.c
void init_driver();
drv_t driver_malloc(char *drv_file, drv_type_t drv_type);
void driver_free(drv_t driver);
void driver_call(drv_t driver, int func, void *arg);
void driver_set_handler(drv_t driver, int func_addr, int handler_num);
drv_t driver_find(drv_type_t type);
// module.c
void module_init_system(void);
int module_load(const char *path);
int module_unload(const char *name);
int module_list(module_handle_t *out, int max_count);
uintptr_t module_resolve_symbol(const char *name);
bool module_register_kernel_symbol(const char *name, uintptr_t addr);
// vdisk.c
int init_vdisk();
int register_vdisk(vdisk vd);
int logout_vdisk(char drive);
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read);
bool have_vdisk(char drive);
char first_vdisk(void);
char next_vdisk(char drive);
// timer.c
void sleep(unsigned long long s);
// ide.c
void ide_read_sectors(unsigned char drive, unsigned char numsects,
                      unsigned int lba, unsigned short es, void *buffer);
void ide_write_sectors(unsigned char drive, unsigned char numsects,
                       unsigned int lba, unsigned short es, void *buffer);
void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2,
                    unsigned int BAR3, unsigned int BAR4);
#endif
