#include <dos.h>
#include <irq.h>
int init_ok_flag = 0;
extern unsigned int PCI_ADDR_BASE;
char *shell_data;
unsigned shell_size;
int rtc_init(void);
void init_devfs(void);
void ahci_init(void);
static char find_system_drive(void) {
  static const char *const boot_files[] = {"init.bin", "psh.bin", "sys.cfg"};
  char path[300];

  for (int i = 0; i < 26; i++) {
    if (!vfs_check_mount('A' + i)) {
      continue;
    }
    bool complete = true;
    for (unsigned int j = 0; j < sizeof(boot_files) / sizeof(boot_files[0]);
         j++) {
      sprintf(path, "%c:/%s", 'A' + i, boot_files[j]);
      uint32_t size = vfs_filesize(path);
      if (size == (uint32_t)-1) {
        complete = false;
        break;
      }
    }
    if (complete) {
      return 'A' + i;
    }
  }

  return 0;
}
void idle() {
  kernel_lock_leave();
  for (;;) {
    asm volatile("sti; hlt" ::: "memory");
  }
}
void init() {
  irq_enable();
  logk("init task has been started!\n");

  PCI_ADDR_BASE = (unsigned int)page_malloc(1 * 1024 * 1024);
  init_PCI(PCI_ADDR_BASE);
  init_floppy();
  rtc_init();
  init_devfs();
  printk("init ide\n");
  ide_initialize(0x1F0, 0x3F6, 0x170, 0x376, 0x000);
  printk("init ahci\n");
  ahci_init();
  // init_palette();
  vfs_mount_all_disks();
  char system_drive = find_system_drive();
  if (system_drive == 0 || !vfs_check_mount(system_drive) ||
      !vfs_change_disk(system_drive)) {
    Panic_K("system disk not found");
  }
  task_set_default_drive(system_drive);
  current_task()->drive = system_drive;
  current_task()->drive_number = system_drive - 'A';
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
  if (strcmp("HIGHTEXTMODE", env_read("video_mode")) == 0) {
    running_mode = SwitchToHighTextMode() ? HIGHTEXTMODE : POWERINTDOS;
  } else {
    running_mode = POWERINTDOS;
  }

  FILE *fp = fopen("font.bin", "r");
  if (fp != NULL) {
    ascfont = fp->buffer;
  }
  fp = fopen("HZK16", "r");
  if (fp != NULL) {
    hzkfont = fp->buffer;
  }
  init_ok_flag = 1;
  extern struct tty *tty_default;
  tty_set(current_task(), tty_default);
  clear();
  shell_size = vfs_filesize("psh.bin");
  shell_data = shell_size == (uint32_t)-1 || shell_size == 0
                   ? NULL
                   : (char *)page_malloc(shell_size);
  if (shell_data == NULL || !vfs_readfile("psh.bin", shell_data)) {
    Panic_K("unable to load psh.bin");
  }
  os_execute_no_ret("init.bin", "init.bin");
  task_kill(current_task()->tid);
  for (;;)
    ;
}
