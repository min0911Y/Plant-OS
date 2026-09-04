#include <arch.h>
#include <dos.h>
#include <irq.h>
#include <platform.h>
// struct TASK *shell_task;
// struct TASK *sr1, *sr2;
// struct TASK normal;
uintptr_t memsize;
mouse_decoder_t mdec;

#ifdef KERNEL_DISABLE_MEMTEST
#define KERNEL_MEMSIZE_BYTES ((unsigned int)KERNEL_MEMSIZE_MB * 1024U * 1024U)
#endif

#ifdef KERNEL_ENABLE_BOOT_BENCH
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
    if (!apic_timer_use_tsc_deadline()) {
      WARNING_K("unable to restore TSC-deadline timer after benchmark");
    }
  } else {
    pit_base = bench_loops_avg(rounds);
    printk("bench pit base=%08x\n", pit_base);
    logk("sysinit: timer bench pit_only=%08x\n", pit_base);
  }

  logk("sysinit: timer bench done mode=%s\n",
       apic_timer_uses_tsc_deadline() ? "tsc-deadline" : "pit");
}
#endif

void sysinit(void) {
  const boot_info_t *boot_info = arch_boot_info();
  boot_module_t initramfs = boot_info->initramfs;
  bool has_initramfs = initramfs.size != 0;
  struct FIFO8 keyfifo, mousefifo;
  struct FIFO8 keyfifo_sr1, keyfifo_sr2;
  struct FIFO8 mousefifo_sr1, mousefifo_sr2;
  char keybuf[32];
  char mousebuf[128];
  char keybuf_sr1[32];
  char mousebuf_sr1[128];
  char keybuf_sr2[32];
  char mousebuf_sr2[128];

  init_page(boot_info); // 初始化分页与 WP
  if (has_initramfs &&
      !page_reserve_physical_range(initramfs.address, initramfs.size)) {
    Panic_K("unable to reserve initramfs memory");
    return;
  }
  arch_interrupt_init();
  platform_early_initialize();

  irq_enable();
#ifdef KERNEL_PERF
  perf_start(PERF_SESSION_BOOT);
#endif

  if (!apic_timer_uses_tsc_deadline()) {
    irq_mask_clear(0);  // pit (timer)
  }
  arch_fpu_init_cpu();

  fifo8_init(&keyfifo, 32, (unsigned char *)keybuf);
  fifo8_init(&mousefifo, 128, (unsigned char *)mousebuf);
  fifo8_init(&keyfifo_sr1, 32, (unsigned char *)keybuf_sr1);
  fifo8_init(&mousefifo_sr1, 128, (unsigned char *)mousebuf_sr1);
  fifo8_init(&keyfifo_sr2, 32, (unsigned char *)keybuf_sr2);
  fifo8_init(&mousefifo_sr2, 128, (unsigned char *)mousebuf_sr2);
  if (!init_keyboard()) {
    WARNING_K("PS/2 keyboard initialization timed out");
  }
  if (!enable_mouse(&mdec)) {
    WARNING_K("PS/2 mouse initialization timed out");
  }
  logk("sysinit: enable_mouse done\n");
  mouse_sleep(&mdec);
  logk("sysinit: mouse_sleep done\n");
  irq_mask_clear(1);  // keyboard
  irq_mask_clear(12); // mouse

  logk("sysinit: kernel heap start\n");
  if (!kernel_heap_initialize()) {
    Panic_K("unable to initialize kernel heap");
    return;
  }
  logk("sysinit: kernel heap done\n");
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
  memsize = arch_memory_detect(boot_info);
  logk("sysinit: memtest done memsize=%08x\n", memsize);
#else
  memsize = KERNEL_MEMSIZE_BYTES;
  printk("memtest disabled, assume %u MiB RAM\n", KERNEL_MEMSIZE_MB);
  logk("sysinit: memtest disabled memsize=%08x memsize_mb=%u\n", memsize,
       KERNEL_MEMSIZE_MB);
#endif

  if (has_initramfs &&
      (initramfs.address >= memsize ||
       initramfs.size > memsize - initramfs.address)) {
    Panic_K("initramfs lies outside detected memory");
    return;
  }

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
  if (has_initramfs && !boot_initramfs_register(&initramfs)) {
    Panic_K("unable to register initramfs");
    return;
  }
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
         smp_cpu_lapic_id(0),
         interrupt_controller_uses_apic() ? "apic" : "pic");
  printk("sb16\n");
  logk("sysinit: disable_sb16 start\n");
  disable_sb16();
  logk("sysinit: disable_sb16 done\n");
  printk("input stack\n");
  logk("sysinit: Input_Stack_Init start\n");
  Input_Stack_Init();
  logk("sysinit: Input_Stack_Init done\n");
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
  logk("sysinit: SetDrive done\n");

  printk("Hello Plant OS Kernel\n");
#ifdef KERNEL_ENABLE_BOOT_BENCH
  bench_timer_modes();
#endif
  smp_start_aps();
  printk("smp online=%d/%d\n", smp_online_cpu_count(), smp_cpu_count());
  logk("sysinit: into_mtask start\n");
  if (into_mtask() != 0) {
    Panic_K("unable to start multitasking");
    return;
  }
  for (;;)
    ;
}
