#include <dos.h>
int init_ok_flag = 0;
extern unsigned int PCI_ADDR_BASE;
char *shell_data;
extern unsigned base_count;
float f_cpu_usage;
void idle() {
  while (1) {
    unsigned c = timerctl.count;
    unsigned count = 0;
    while (timerctl.count - c < 100) {
      count++;
    }
    f_cpu_usage = (float)(base_count - count) / (float)base_count;
    f_cpu_usage *= 100.0f;
  }
}
void init() {
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
  init_palette();
  vfs_mount_disk(current_task()->drive, current_task()->drive);
  vfs_change_disk(current_task()->drive);
  env_init();
  init_networkCTL();
  init_network();

  if (env_read("network") == NULL) {
    printk("WARNING: you haven't set the network value in env.cfg, system will "
           "set a default value(disable)\n");
    env_write("network", "disable");
    env_save();
  }
  if (env_read("video_mode" == NULL)) {
    env_write("video_mode", "TEXTMODE");
    env_save();
  }
  if (strcmp(env_read("network"), "enable") == 0) {
    logk("init card\n");
    init_card();
  }
  if (strcmp("HIGHTEXTMODE", env_read("video_mode")) == 0) {
    running_mode = HIGHTEXTMODE;
    SwitchToHighTextMode();
  } else {
    running_mode = POWERINTDOS;
  }

  FILE *fp = fopen("/other/font.bin", "r");
  ascfont = fp->buffer;
  fp = fopen("/other/hzk16", "r");
  hzkfont = fp->buffer;
  init_ok_flag = 1;
  extern struct tty *tty_default;
  tty_set(current_task(), tty_default);
  clear();
  shell_data = (char *)page_malloc(vfs_filesize("psh.bin"));
  vfs_readfile("psh.bin", shell_data);
  os_execute_no_ret("psh.bin", "psh.bin");

  page_free(current_task()->top - 64 * 1024, 64 * 1024);
  page_free(current_task()->nfs, sizeof(vfs_t));

  task_kill(current_task()->tid);
  for (;;)
    ;
}
