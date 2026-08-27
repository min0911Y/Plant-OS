#include <arch.h>
#include <arch/x86/control.h>
#include <dos.h>
#include <irq.h>
extern struct ide_device {
  unsigned char Reserved;      // 0 (Empty) or 1 (This Drive really exists).
  unsigned char Channel;       // 0 (Primary Channel) or 1 (Secondary Channel).
  unsigned char Drive;         // 0 (Master Drive) or 1 (Slave Drive).
  unsigned short Type;         // 0: ATA, 1:ATAPI.
  unsigned short Signature;    // Drive Signature
  unsigned short Capabilities; // Features.
  unsigned int CommandSets;    // Command Sets Supported.
  unsigned int Size;           // Size in Sectors.
  unsigned char Model[41];     // Model in string.
} ide_devices[4];
// struct TASK *shell_task;
// struct TASK *sr1, *sr2;
// struct TASK normal;
memory *public_heap;
void *heap;
unsigned int memsize;
unsigned int PCI_ADDR_BASE;
struct MOUSE_DEC mdec;
extern unsigned char *IVT;
void disable_sb16(void);
void init_mount_disk(void);
int getReadyDisk();
void socket_init();
void init_devfs();
void init_vfs();
unsigned base_count;

#ifdef KERNEL_DISABLE_MEMTEST
#define KERNEL_MEMSIZE_BYTES ((unsigned int)KERNEL_MEMSIZE_MB * 1024U * 1024U)
#endif

#ifndef KERNEL_DISABLE_BOOT_BENCH
static unsigned bench_loops_one_tick(void) {
  unsigned loops = 0;
  unsigned c = timerctl.count;
  while (timerctl.count == c) {
  }

  c = timerctl.count;
  while (timerctl.count == c) {
    loops++;
  }
  return loops;
}

static unsigned bench_loops_avg(unsigned rounds) {
  unsigned long long total = 0;
  if (rounds == 0) {
    return 0;
  }

  for (unsigned i = 0; i < rounds; i++) {
    total += bench_loops_one_tick();
  }
  return (unsigned)(total / rounds);
}

static void bench_timer_modes(void) {
  const unsigned rounds = 8;
  unsigned tsc_base = 0;
  unsigned pit_base = 0;

  printk("benching timer modes...\n");
  logk("sysinit: timer bench start rounds=%d\n", rounds);

  if (apic_timer_tsc_deadline_available()) {
    if (apic_timer_use_tsc_deadline()) {
      tsc_base = bench_loops_avg(rounds);
      printk("bench tsc-deadline base=%08x\n", tsc_base);
      logk("sysinit: timer bench tsc_deadline=%08x\n", tsc_base);
    }

    apic_timer_use_irq0();
    pit_base = bench_loops_avg(rounds);
    printk("bench pit base=%08x\n", pit_base);
    logk("sysinit: timer bench pit=%08x\n", pit_base);

    if (tsc_base != 0 && pit_base != 0) {
      int diff_pct = (int)(((int64_t)tsc_base - (int64_t)pit_base) * 100 /
                           (int64_t)pit_base);
      printk("bench diff=%d%% (%s vs pit)\n", diff_pct,
             diff_pct >= 0 ? "tsc" : "tsc slower");
      logk("sysinit: timer bench diff_pct=%d\n", diff_pct);
    }

    if (apic_timer_use_tsc_deadline()) {
      base_count = tsc_base;
    } else {
      base_count = pit_base;
    }
  } else {
    pit_base = bench_loops_avg(rounds);
    base_count = pit_base;
    printk("bench pit base=%08x\n", pit_base);
    logk("sysinit: timer bench pit_only=%08x\n", pit_base);
  }

  logk("sysinit: timer bench done base_count=%08x mode=%s\n", base_count,
       apic_timer_uses_tsc_deadline() ? "tsc-deadline" : "pit");
}
#else
static unsigned calibrate_base_count_fallback(void) {
  unsigned loops = 0;
  unsigned c = timerctl.count;

  while (timerctl.count == c) {
  }

  c = timerctl.count;
  while (timerctl.count - c < 100) {
    loops++;
  }

  return loops / 100 ? loops / 100 : 1;
}
#endif

void sysinit(void) {
  struct FIFO8 keyfifo, mousefifo;
  struct FIFO8 keyfifo_sr1, keyfifo_sr2;
  struct FIFO8 mousefifo_sr1, mousefifo_sr2;
  char keybuf[32];
  char mousebuf[128];
  char keybuf_sr1[32];
  char mousebuf_sr1[128];
  char keybuf_sr2[32];
  char mousebuf_sr2[128];

  init_page(); // 初始化分页与 WP
  arch_interrupt_init();
  init_pic();
  init_pit();
  init_acpi();
  apic_init();

  IVT = page_malloc(0x400);
  memcpy(IVT, 0x0, 0x400);

  irq_enable();
#ifdef KERNEL_PERF
  perf_boot_start();
#endif

  if (!apic_timer_uses_tsc_deadline()) {
    irq_mask_clear(0);  // pit (timer)
  }
  irq_mask_clear(1);  // keyboard
  irq_mask_clear(12); // mouse
  x86_cr0_write(x86_cr0_read() | X86_CR0_EM | X86_CR0_TS | X86_CR0_NE);

  fifo8_init(&keyfifo, 32, (unsigned char *)keybuf);
  fifo8_init(&mousefifo, 128, (unsigned char *)mousebuf);
  fifo8_init(&keyfifo_sr1, 32, (unsigned char *)keybuf_sr1);
  fifo8_init(&mousefifo_sr1, 128, (unsigned char *)mousebuf_sr1);
  fifo8_init(&keyfifo_sr2, 32, (unsigned char *)keybuf_sr2);
  fifo8_init(&mousefifo_sr2, 128, (unsigned char *)mousebuf_sr2);
  init_keyboard();
  enable_mouse(&mdec);
  logk("sysinit: enable_mouse done\n");
  mouse_sleep(&mdec);
  logk("sysinit: mouse_sleep done\n");

  logk("sysinit: page_malloc public heap start\n");
  heap = page_malloc(128 * 1024 * 1024);
  logk("sysinit: page_malloc public heap done heap=%08x\n",
       (uint32_t)(uintptr_t)heap);
  if (heap == NULL) {
    logk("sysinit: public heap allocation failed\n");
    for (;;)
      ;
  }
  logk("sysinit: memory_init public heap start\n");
  public_heap = memory_init((uintptr_t)heap, 128 * 1024 * 1024);
  logk("sysinit: memory_init public heap done public_heap=%08x\n",
       (uint32_t)(uintptr_t)public_heap);
  logk("sysinit: init_tty start\n");
  if (!init_tty()) {
    Panic_K("unable to initialize TTY");
    return;
  }
  logk("sysinit: init_tty done\n");
  clear();
  logk("sysinit: clear done\n");
  printk("Welcome to Plant OS Kernel!!!!!!\n");
#ifndef KERNEL_DISABLE_MEMTEST
  logk("sysinit: memtest start\n");
  memsize = memtest(0x00400000, 0xbfffffff);
  logk("sysinit: memtest done memsize=%08x\n", memsize);
#else
  memsize = KERNEL_MEMSIZE_BYTES;
  printk("memtest disabled, assume %u MiB RAM\n", KERNEL_MEMSIZE_MB);
  logk("sysinit: memtest disabled memsize=%08x memsize_mb=%u\n", memsize,
       KERNEL_MEMSIZE_MB);
#endif

  if (memsize / (1024 * 1024) < 256) {
    while (1) {
      beep(3, 7, 5);
      sleep(100);
    }
  }

  clear();
  logk("sysinit: PIT banner\n");
  printk("PIT\n");
  printk("VDISK\n");
  logk("sysinit: init_vdisk start\n");
  init_vdisk();
  logk("sysinit: init_vdisk done\n");
  printk("VFS\n");
  logk("sysinit: init_vfs start\n");
  init_vfs();
  logk("sysinit: init_vfs done\n");
  printk("FAT\n");
  logk("sysinit: Register_fat_fileSys start\n");
  Register_fat_fileSys();
  logk("sysinit: Register_fat_fileSys done\n");
  printk("ISO9660\n");
  logk("sysinit: init_iso9660 start\n");
  init_iso9660();
  logk("sysinit: init_iso9660 done\n");
  printk("PFS\n");
  logk("sysinit: reg_pfs start\n");
  reg_pfs();
  logk("sysinit: reg_pfs done\n");
  printk("pf set up to %08x\n",memsize);
  logk("sysinit: pf_set start memsize=%08x\n", memsize);
  pf_set(memsize);
  logk("sysinit: pf_set done\n");
  printk("acpi\n");
  printk("smp cpus=%d bsp apic=%d ctl=%s\n", smp_cpu_count(),
         smp_bsp_lapic_id(),
         interrupt_controller_uses_apic() ? "apic" : "pic");
  printk("sb16\n");
  logk("sysinit: disable_sb16 start\n");
  disable_sb16();
  logk("sysinit: disable_sb16 done\n");
  printk("input stack\n");
  logk("sysinit: Input_Stack_Init start\n");
  Input_Stack_Init();
  logk("sysinit: Input_Stack_Init done\n");
  printk("socket\n");
  logk("sysinit: socket_init start\n");
  socket_init();
  logk("sysinit: socket_init done\n");
  printk("module\n");
  logk("sysinit: module_init_system start\n");
  module_init_system();
  logk("sysinit: module_init_system done\n");
  printk("mount disk\n");
  logk("sysinit: init_mount_disk start\n");
  init_mount_disk();
  logk("sysinit: init_mount_disk done\n");
  printk("set drives\n");
  logk("sysinit: SetDrive start\n");
  SetDrive((unsigned char *)"DISK_DRIVE");
  SetDrive((unsigned char *)"NETCARD_DRIVE");
  logk("sysinit: SetDrive done\n");

  printk("Hello Plant OS Kernel\n");
#ifndef KERNEL_DISABLE_BOOT_BENCH
  bench_timer_modes();
#else
  base_count = calibrate_base_count_fallback();
  printk("boot timer benchmark disabled\n");
  printk("base count fallback %08x (%s)\n", base_count,
         apic_timer_uses_tsc_deadline() ? "tsc-deadline" : "pit");
  logk("sysinit: timer bench disabled base_count=%08x mode=%s\n", base_count,
       apic_timer_uses_tsc_deadline() ? "tsc-deadline" : "pit");
#endif
  printk("base count is %08x (%s)\n", base_count,
         apic_timer_uses_tsc_deadline() ? "tsc-deadline" : "pit");
  logk("sysinit: into_mtask start\n");
  if (into_mtask() != 0) {
    Panic_K("unable to start multitasking");
    return;
  }
  for (;;)
    ;
}
