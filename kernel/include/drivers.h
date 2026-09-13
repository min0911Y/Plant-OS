#ifndef _DRIVERS_H
#define _DRIVERS_H
#include <define.h>
#include <mouse.h>
#include <module.h>
// acpi.c
char checksum(unsigned char *addr, unsigned int length);
unsigned int *acpi_find_rsdp(void);
void *acpi_find_table(char *Signature);
void init_acpi(void);
bool hpet_available(void);
uint64_t monotonic_time_ns(void);
void usleep(uint64_t nanoseconds);
int acpi_have_madt(void);
uint32_t acpi_lapic_address(void);
uint32_t acpi_cpu_count(void);
uint32_t acpi_bsp_lapic_id(void);
uint32_t acpi_cpu_lapic_id(uint32_t index);
int acpi_cpu_usable(uint32_t index);
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
// rtc.c
int rtc_init(void);
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
// keyboard.c
bool ps2_wait_input_empty(void);
bool init_keyboard(void);
int getch();
int input_char_inSM();
int kbhit();
// mouse.c
bool enable_mouse(void);
void mouseinput();
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
void apic_send_fixed_ipi(uint32_t apic_id, uint8_t vector);
void apic_init_secondary(void);
void apic_timer_init_secondary(void);
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
int register_vdisk_at(char drive, vdisk vd);
int logout_vdisk(char drive);
int rw_vdisk(char drive, unsigned int lba, unsigned char *buffer,
             unsigned int number, int read);
bool have_vdisk(char drive);
bool disk_sync(char drive);
bool disk_writable(char drive);
vdisk_type_t vdisk_type(char drive);
char first_vdisk(void);
char next_vdisk(char drive);
// timer.c
void sleep(unsigned long long s);
// ide.c
void ide_initialize(void);
// ahci.c
void ahci_init(void);
#endif
