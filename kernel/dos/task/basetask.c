#include <dos.h>
#include <irq.h>
#include <pci.h>
#include <usb.h>
int init_ok_flag = 0;
static bool system_files_present(vfs_context_t *context) {
  static const char *const files[] = {"/init.bin", "/psh.bin", "/sys.cfg",
                                      "/lib/ld.so", "/lib/libp.so"};
  for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
    vfs_stat_t status;
    if (vfs_stat(context, files[i], &status) < 0 ||
        status.type != VFS_NODE_FILE)
      return false;
  }
  return true;
}

static char find_system_drive(void) {
  for (char drive = 'A'; drive <= 'Z'; drive++) {
    if (!vfs_check_mount(drive))
      continue;
    vfs_context_t *context = vfs_context_create(drive);
    if (!context)
      continue;
    bool complete = system_files_present(context);
    vfs_context_release(context);
    if (complete)
      return drive;
  }
  return 0;
}
void idle() {
  kernel_lock_leave();
  for (;;) {
    arch_cpu_idle();
  }
}
void init() {
  smp_request_secondary_release();
  irq_enable();
  logk("init task has been started!\n");

  if (!pci_initialize()) {
    Panic_K("unable to enumerate PCI devices");
  }
  init_floppy();
  rtc_init();
  init_devfs();
  printk("init ide\n");
  ide_initialize();
  printk("init ahci\n");
  ahci_init();
  const boot_module_t *initramfs = &arch_boot_info()->initramfs;
  char system_drive;
  if (initramfs->size != 0) {
    if (!vfs_mount_disk(BOOT_INITRAMFS_DRIVE, BOOT_INITRAMFS_DRIVE)) {
      Panic_K("unable to mount initramfs");
    }
    /* Keep the RAM-backed system volume authoritative, but expose attached
     * block devices as ordinary drive-letter volumes as well. */
    vfs_mount_all_disks();
    system_drive = BOOT_INITRAMFS_DRIVE;
  } else {
    vfs_mount_all_disks();
    system_drive = find_system_drive();
  }
  if (system_drive == 0 || !vfs_check_mount(system_drive)) {
    Panic_K("system disk not found");
  }
  vfs_context_t *system_context = vfs_context_create(system_drive);
  if (system_context == NULL || !system_files_present(system_context)) {
    Panic_K("system runtime unavailable");
  }
  vfs_context_release(current_task()->fs_context);
  current_task()->fs_context = system_context;
  task_set_default_drive(system_drive);
  env_init();
  net_stack_initialize();

  if (env_read("network") == NULL) {
    printk("WARNING: you haven't set the network value in env.cfg, system will "
           "set a default value(disable)\n");
    env_write("network", "disable");
    env_save();
  }
  if (env_read("video_mode") == NULL) {
    env_write("video_mode", "TEXTMODE");
    env_save();
  }
  if (strcmp(env_read("network"), "enable") == 0) {
    logk("network: start lwIP\n");
    if (!net_stack_start()) {
      WARNING_K("network: Ethernet unavailable; loopback remains available");
    }
  }
  xhci_initialize();
#if defined(KERNEL_ARCH_I386)
  if (strcmp("HIGHTEXTMODE", env_read("video_mode")) == 0) {
    running_mode = SwitchToHighTextMode() ? HIGHTEXTMODE : POWERINTDOS;
  } else {
    running_mode = POWERINTDOS;
  }
#endif

  vfs_stat_t font_status;
  FILE *fp = fopen("font.bin", "rb");
  if (fp != NULL &&
      vfs_stat(current_task()->fs_context, "font.bin", &font_status) == 0) {
    ascfont = malloc(font_status.size);
    if (ascfont == NULL ||
        fread(ascfont, 1, font_status.size, fp) != font_status.size) {
      Panic_K("unable to load font.bin");
    }
    fclose(fp);
  }
  fp = fopen("HZK16", "rb");
  if (fp != NULL &&
      vfs_stat(current_task()->fs_context, "HZK16", &font_status) == 0) {
    hzkfont = malloc(font_status.size);
    if (hzkfont == NULL ||
        fread(hzkfont, 1, font_status.size, fp) != font_status.size) {
      Panic_K("unable to load HZK16");
    }
    fclose(fp);
  }
  init_ok_flag = 1;
  extern struct tty *tty_default;
  tty_set(current_task(), tty_default);
#ifndef KERNEL_USB_DEBUG
  clear();
#endif
  os_execute_no_ret("init.bin", "init.bin");
  task_kill(current_task()->tid);
  for (;;)
    ;
}
