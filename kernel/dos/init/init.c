#include <dos.h>
extern struct ide_device {
  unsigned char  Reserved;     // 0 (Empty) or 1 (This Drive really exists).
  unsigned char  Channel;      // 0 (Primary Channel) or 1 (Secondary Channel).
  unsigned char  Drive;        // 0 (Master Drive) or 1 (Slave Drive).
  unsigned short Type;         // 0: ATA, 1:ATAPI.
  unsigned short Signature;    // Drive Signature
  unsigned short Capabilities; // Features.
  unsigned int   CommandSets;  // Command Sets Supported.
  unsigned int   Size;         // Size in Sectors.
  unsigned char  Model[41];    // Model in string.
} ide_devices[4];
// struct TASK *shell_task;
// struct TASK *sr1, *sr2;
// struct TASK normal;
memory               *public_heap;
void                 *heap;
unsigned int          memsize;
unsigned int          PCI_ADDR_BASE;
struct MOUSE_DEC      mdec;
extern unsigned char *IVT;
void                  init_page(void);
void                  disable_sb16(void);
void                  init_mount_disk(void);
int                   getReadyDisk();
void                  socket_init();
void                  init_devfs();
void                  init_vfs();
void                  do_init_seg_register();
unsigned              base_count;
void                  sysinit(void) {
  struct FIFO8 keyfifo, mousefifo;
  struct FIFO8 keyfifo_sr1, keyfifo_sr2;
  struct FIFO8 mousefifo_sr1, mousefifo_sr2;
  char         keybuf[32];
  char         mousebuf[128];
  char         keybuf_sr1[32];
  char         mousebuf_sr1[128];
  char         keybuf_sr2[32];
  char         mousebuf_sr2[128];

  do_init_seg_register();

  struct SEGMENT_DESCRIPTOR *gdt = (struct SEGMENT_DESCRIPTOR *)ADR_GDT;
  init_page(); // 初始化分页
  init_gdtidt();

  IVT = page_malloc(0x400);
  memcpy(IVT, 0x0, 0x400);
  init_pic();

  io_sti();

  irq_mask_clear(0);  // pit (timer)
  irq_mask_clear(1);  // keyboard
  irq_mask_clear(12); // mouse
  set_cr0(get_cr0() | CR0_EM | CR0_TS | CR0_NE);

  fifo8_init(&keyfifo, 32, (unsigned char *)keybuf);
  fifo8_init(&mousefifo, 128, (unsigned char *)mousebuf);
  fifo8_init(&keyfifo_sr1, 32, (unsigned char *)keybuf_sr1);
  fifo8_init(&mousefifo_sr1, 128, (unsigned char *)mousebuf_sr1);
  fifo8_init(&keyfifo_sr2, 32, (unsigned char *)keybuf_sr2);
  fifo8_init(&mousefifo_sr2, 128, (unsigned char *)mousebuf_sr2);
  init_keyboard();
  enable_mouse(&mdec);
  mouse_sleep(&mdec);

  heap        = page_malloc(128 * 1024 * 1024);
  public_heap = memory_init(heap, 128 * 1024 * 1024);
  init_tty();
  clear();
  printk("Welcome to Plant OS Kernel!!!!!!\n");
  memsize = memtest(0x00400000, 0xbfffffff);

  if (memsize / (1024 * 1024) < 256) {
    while (1) {
      beep(3, 7, 5);
      sleep(100);
    }
  }

  clear();
  printk("PIT\n");
  init_pit();
  printk("VDISK\n");
  init_vdisk();
  printk("VFS\n");
  init_vfs();
  printk("FAT\n");
  Register_fat_fileSys();
  printk("ISO9660\n");
  init_iso9660();
  printk("PFS\n");
  reg_pfs();
  printk("pf set up to %08x\n", memsize);
  pf_set(memsize);
  printk("acpi\n");
  init_acpi();
  printk("sb16\n");
  disable_sb16();
  printk("input stack\n");
  Input_Stack_Init();
  printk("socket\n");
  socket_init();
  // init_driver();
  printk("mount disk\n");
  init_mount_disk();
  printk("set drives\n");
  SetDrive((unsigned char *)"DISK_DRIVE");
  SetDrive((unsigned char *)"NETCARD_DRIVE");

  printk("Hello Plant OS Kernel\n");
  printk("benching cpu.....\n");
  base_count = 0;
  unsigned c = timerctl.count;
  while (timerctl.count - c < 100) {
    base_count++;
  }
  printk("base count is %08x\n", base_count);
  into_mtask();
  for (;;)
    ;
}
