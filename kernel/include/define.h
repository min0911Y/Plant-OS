#pragma once
#include <ctypes.h>
#include <stdarg.h>
#include <stddef.h>
typedef u32    vram_t;
typedef vram_t color_t;

/* dos.h */
#define VERSION      "0.7b" // Version of the program
#define ADR_IDT      0x0026f800
#define LIMIT_IDT    0x000007ff
#define ADR_GDT      0x00270000
#define LIMIT_GDT    0x0000ffff
#define ADR_BOTPAK   0x00280000
#define LIMIT_BOTPAK 0x0007ffff
#define AR_DATA32_RW 0x4092
#define AR_DATA16_RW 0x0092
#define AR_CODE32_ER 0x409a
#define AR_CODE16_ER 0x009a
#define AR_INTGATE32 0x008e
#define PIT_CTRL     0x0043
#define PIT_CNT0     0x0040
#define AR_TSS32     0x0089
#define NULL_TID     11459810
#define Panic_Print(func, info, ...)                                                               \
  func("%s--PANIC: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define WARNING_Print(func, info, ...)                                                             \
  func("%s--WARNING: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define DEBUG_Print(func, info, ...)                                                               \
  func("%s--DEBUG: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__);
#define Panic_K(info, ...)   Panic_Print(logk, info, ##__VA_ARGS__)
#define WARNING_K(info, ...) WARNING_Print(logk, info, ##__VA_ARGS__)
#define DEBUG_K(info, ...)   DEBUG_Print(logk, info, ##__VA_ARGS__)
#define Panic_F(info, ...)   Panic_Print(printf, info, ##__VA_ARGS__)
#define WARNING_F(info, ...) WARNING_Print(printf, info, ##__VA_ARGS__)
#define DEBUG_F(info, ...)   DEBUG_Print(printf, info, ##__VA_ARGS__)
#define get_tid(task)        task->tid
#define POWERINTDOS          0
#define HIGHTEXTMODE         1
extern struct MOUSE_DEC mdec;
extern int              gmx, gmy;
extern u8              *font, *ascfont, *hzkfont;
extern struct TIMERCTL  timerctl;
extern u32              memsize;
extern u32              running_mode;
struct PAGE_INFO {
  u8 task_id;
  u8 count;
} __PACKED__;

#define FREE_MAX_NUM              4096
#define ERRNO_NOPE                0
#define ERRNO_NO_ENOGHT_MEMORY    1
#define ERRNO_NO_MORE_FREE_MEMBER 2
#define MEM_MAX(a, b)             ((a) > (b) ? (a) : (b))
typedef struct {
  u32 start;
  u32 end; // end和start都等于0说明这个free结构没有使用
} free_member;
typedef struct freeinfo freeinfo;
typedef struct freeinfo {
  free_member *f;
  freeinfo    *next;
} freeinfo;
typedef struct {
  freeinfo *freeinf;
  int       memerrno;
} memory;
struct SEGMENT_DESCRIPTOR {
  i16  limit_low, base_low;
  char base_mid, access_right;
  char limit_high, base_high;
};
struct GATE_DESCRIPTOR {
  i16  offset_low, selector;
  char dw_count, access_right;
  i16  offset_high;
};
#define MAX_TIMER 500
struct mtask;
typedef struct mtask mtask;
struct TIMER {
  struct TIMER *next;
  u32           timeout, flags;
  struct FIFO8 *fifo;
  u8            data;
  mtask        *waiter;
};
struct TIMERCTL {
  u32           count, next;
  struct TIMER *t0;
  struct TIMER  timers0[MAX_TIMER];
};
struct TSS32 {
  int backlink, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
  int eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
  int es, cs, ss, ds, fs, gs;
  int ldtr, iomap;
};
#define MAX_IPC_MESSAGE 5 // 一次最多存放5个IPC_MESSAGE
#define synchronous     1
#define asynchronous    2
typedef struct {
  void *data;
  u32   size;
  int   from_tid;
  int   flag1;
  int   flag2;
} IPCMessage;
// lock.c
typedef struct {
  mtask   *owner;
  unsigned value;
  mtask   *waiter;
} lock_t;
#define LOCK_UNLOCKED 0
#define LOCK_LOCKED   1
typedef struct { // IPC头（在TASK结构体中的头）
  int        now;
  IPCMessage messages[MAX_IPC_MESSAGE];
  lock_t     l;
} IPC_Header;
// struct THREAD {
//   struct TASK *father;
// };
#define _packed __PACKED__
enum {
  CR0_PE = 1 << 0, // Protection Enable 启用保护模式
  CR0_MP = 1 << 1, // Monitor Coprocessor
  CR0_EM = 1 << 2, // Emulation 启用模拟，表示没有 FPU
  CR0_TS = 1 << 3, // Task Switch 任务切换，延迟保存浮点环境
  CR0_ET = 1 << 3, // Extension Type 保留
  CR0_NE = 1 << 5, // Numeric Error 启用内部浮点错误报告
  CR0_WP = 1 << 16, // Write Protect 写保护（禁止超级用户写入只读页）帮助写时复制
  CR0_AM = 1 << 18, // Alignment Mask 对齐掩码
  CR0_NW = 1 << 29, // Not Write-Through 不是直写
  CR0_CD = 1 << 30, // Cache Disable 禁用内存缓冲
  CR0_PG = 1 << 31, // Paging 启用分页
};
typedef struct fpu_t {
  u16 control;
  u16 RESERVED1;
  u16 status;
  u16 RESERVED2;
  u16 tag;
  u16 RESERVED3;
  u32 fip0;
  u32 fop0;
  u32 fdp0;
  u32 fdp1;
  u8  regs[80];
} _packed fpu_t;
// struct TASK {
//   int sel, sleep, level;
//   struct TSS32 tss;
//   char name[32];
//   char running;
//   struct tty *TTY;
//   struct FIFO8 *keyfifo, *mousefifo; // 基本输入设备的缓冲区
//   int fifosleep;
//   int cs_base, ds_base;
//   void *alloc_addr;
//   memory *mm;
//   int alloc_size;
//   struct IPC_Header IPC_header;
//   struct TIMER *timer;
//   int esp_start; // 开始的esp
//   int eip_start; // 开始的eip
//   i16 cs_start;
//   i16 ss_start;
//   int is_child; // 是子线程吗
//   int app;
//   struct THREAD thread;
//   int drive_number;
//   char drive;
//   char *line;
//   void (*keyboard_press)(u8 data, u32 task);
//   void (*keyboard_release)(u8 data, u32 task);
//   int nl;
//   int lock; // 被锁住了？
//   char forever;
//   int DisableExpFlag;
//   u32 CatchEIP;
//   char flagOfexp;
//   int mx, my;
//   fpu_t *fpu;
//   struct vfs_t *nfs;
//   int fpu_flag;
//   struct FIFO8 *Pkeyfifo, *Ukeyfifo;
//   u32 fpu_use;
//   u32 *gdt_data;
//   u32 pde;
// } __PACKED__;
typedef struct {
  u32 eax, ebx, ecx, edx, esi, edi, ebp;
  u32 eip;
} stack_frame;
enum STATE {
  EMPTY,
  RUNNING,
  WAITING,
  SLEEPING,
  WILL_EMPTY,
  READY,
  DIED
};
typedef struct mtask {
  stack_frame  *esp;
  unsigned      pde;
  unsigned      user_mode;
  unsigned      top;
  unsigned      running; // 已经占用了多少时间片
  unsigned      timeout; // 需要占用多少时间片
  int           floor;
  enum STATE    state; // 此项为1（RUNNING） 即正常调度，为 2（WAITING） 3
                       // （SLEEPING）的时候不执行 ，0 EMPTY 空闲格子
  uint64_t      jiffies;
  struct vfs_t *nfs;
  uint64_t      tid, ptid;
  memory       *mm;
  u32           alloc_addr;
  u32          *alloc_size;
  u32           alloced;
  struct tty   *TTY;
  int           DisableExpFlag;
  u32           CatchEIP;
  char          flagOfexp;
  fpu_t         fpu;
  int           fpu_flag;
  char          drive_number;
  char          drive;
  struct FIFO8 *Pkeyfifo, *Ukeyfifo;
  struct FIFO8 *keyfifo, *mousefifo; // 基本输入设备的缓冲区
  char          urgent;
  void (*keyboard_press)(u8 data, u32 task);
  void (*keyboard_release)(u8 data, u32 task);
  char          fifosleep;
  int           mx, my;
  char         *line;
  struct TIMER *timer;
  IPC_Header    ipc_header;
  u32           waittid;
  int           ready; // 如果为waiting 则无视wating
  int           sigint_up;
  u8            train; // 轮询
  unsigned      status;
  unsigned      signal;
  unsigned      handler[30];
  unsigned      ret_to_app;
  unsigned      times;
  unsigned      signal_disable;
} _packed mtask;
typedef struct intr_frame_t {
  unsigned edi;
  unsigned esi;
  unsigned ebp;
  // 虽然 pushad 把 esp 也压入，但 esp 是不断变化的，所以会被 popad 忽略
  unsigned esp_dummy;

  unsigned ebx;
  unsigned edx;
  unsigned ecx;
  unsigned eax;

  unsigned gs;
  unsigned fs;
  unsigned es;
  unsigned ds;

  unsigned eip;
  unsigned cs;
  unsigned eflags;
  unsigned esp;
  unsigned ss;
} intr_frame_t;
#define vfs_now       current_task()->nfs
#define PG_P          1
#define PG_USU        4
#define PG_RWW        2
#define PG_PCD        16
#define PG_SHARED     1024
#define PDE_ADDRESS   0x400000
#define PTE_ADDRESS   (PDE_ADDRESS + 0x1000)
#define PAGE_END      (PTE_ADDRESS + 0x400000)
#define PAGE_MANNAGER PAGE_END
struct FIFO8 {
  u8 *buf;
  int p, q, size, free, flags;
};
struct ListCtl {
  struct List *start;
  struct List *end;
  int          all;
};
struct List {
  struct ListCtl *ctl;
  struct List    *prev;
  uintptr_t       val;
  struct List    *next;
};
typedef struct List List;

/* cmd.h */
#define CHAT_SERVER_IP   0x761ff8d7
#define CHAT_SERVER_PROT 25565
#define CHAT_CLIENT_PROT 21538

/* fs.h */

extern u32 Path_Addr;
struct FAT_CACHE {
  u32                  ADR_DISKIMG;
  struct FAT_FILEINFO *root_directory;
  struct List         *directory_list;
  struct List         *directory_clustno_list;
  struct List         *directory_max_list;
  int                 *fat;
  int                  FatMaxTerms;
  u32                  ClustnoBytes;
  u16                  RootMaxFiles;
  u32                  RootDictAddress;
  u32                  FileDataAddress;
  u32                  imgTotalSize;
  u16                  SectorBytes;
  u32                  Fat1Address, Fat2Address;
  u8                  *FatClustnoFlags;
  int                  type;
};
typedef struct {
  struct FAT_CACHE     dm;
  struct FAT_FILEINFO *dir;
} fat_cache;
#define get_dm(vfs)            ((fat_cache *)(vfs->cache))->dm
#define get_now_dir(vfs)       ((fat_cache *)(vfs->cache))->dir
#define get_clustno(high, low) (high << 16) | (low & 0xffff)
#define clustno_end(type)      0xfffffff & ((((1 << (type - 1)) - 1) << 1) + 1)
typedef enum {
  FLE,
  DIR,
  RDO,
  HID,
  SYS
} ftype;
typedef struct {
  char  name[255];
  ftype type;
  u32   size;
  u16   year, month, day;
  u16   hour, minute;
} vfs_file;
typedef struct vfs_t {
  List *path;
  void *cache;
  char  FSName[255];
  int   disk_number;
  u8    drive; // 大写（必须）
  vfs_file *(*FileInfo)(struct vfs_t *vfs, char *filename);
  List *(*ListFile)(struct vfs_t *vfs, char *dictpath);
  bool (*ReadFile)(struct vfs_t *vfs, char *path, char *buffer);
  bool (*WriteFile)(struct vfs_t *vfs, char *path, char *buffer, int size);
  bool (*DelFile)(struct vfs_t *vfs, char *path);
  bool (*DelDict)(struct vfs_t *vfs, char *path);
  bool (*CreateFile)(struct vfs_t *vfs, char *filename);
  bool (*CreateDict)(struct vfs_t *vfs, char *filename);
  bool (*RenameFile)(struct vfs_t *vfs, char *filename, char *filename_of_new);
  bool (*Attrib)(struct vfs_t *vfs, char *filename, ftype type);
  bool (*Format)(u8 disk_number);
  void (*InitFs)(struct vfs_t *vfs, u8 disk_number);
  void (*DeleteFs)(struct vfs_t *vfs);
  bool (*Check)(u8 disk_number);
  bool (*cd)(struct vfs_t *vfs, char *dictName);
  int (*FileSize)(struct vfs_t *vfs, char *filename);
  void (*CopyCache)(struct vfs_t *dest, struct vfs_t *src);
  int flag;
} vfs_t;
#define BS_jmpBoot       0
#define BS_OEMName       3
#define BPB_BytsPerSec   11
#define BPB_SecPerClus   13
#define BPB_RsvdSecCnt   14
#define BPB_NumFATs      16
#define BPB_RootEntCnt   17
#define BPB_TotSec16     19
#define BPB_Media        21
#define BPB_FATSz16      22
#define BPB_SecPerTrk    24
#define BPB_NumHeads     26
#define BPB_HiddSec      28
#define BPB_TotSec32     32
#define BPB_FATSz32      36
#define BPB_ExtFlags     40
#define BPB_FSVer        42
#define BPB_RootClus     44
#define BPB_FSInfo       48
#define BPB_BkBootSec    50
#define BPB_Reserved     52
#define BPB_Fat32ExtByts 28
#define BS_DrvNum        36
#define BS_Reserved1     37
#define BS_BootSig       38
#define BS_VolD          39
#define BS_VolLab        43
#define BS_FileSysType   54
#define EOF              -1
#define SEEK_SET         0
#define SEEK_CUR         1
#define SEEK_END         2
struct FAT_FILEINFO {
  u8   name[8], ext[3], type;
  char reserve;
  u8   create_time_tenth;
  u16  create_time, create_date, access_date, clustno_high;
  u16  update_time, update_date, clustno_low;
  u32  size;
};
#define rmfarptr2ptr(x) ((x).seg * 0x10 + (x).offset)
typedef struct FILE {
  u32   mode;
  u32   fileSize;
  u8   *buffer;
  u32   bufferSize;
  u32   p;
  char *name;
} FILE;
struct DLL_STRPICENV {
  int work[16384];
};
struct RGB {
  u8 b, g, r, t;
};
struct paw_info {
  u8   reserved[12]; // 12 bytes reserved(0xFF)
  char oem[3];       // PRA
  int  xsize;        // xsize
  int  ysize;        // ysize
};

/* interrupts.h */
#define PIC0_ICW1 0x0020
#define PIC0_OCW2 0x0020
#define PIC0_IMR  0x0021
#define PIC0_ICW2 0x0021
#define PIC0_ICW3 0x0021
#define PIC0_ICW4 0x0021
#define PIC1_ICW1 0x00a0
#define PIC1_OCW2 0x00a0
#define PIC1_IMR  0x00a1
#define PIC1_ICW2 0x00a1
#define PIC1_ICW3 0x00a1
#define PIC1_ICW4 0x00a1
typedef struct {
  u16 di, si, bp, sp, bx, dx, cx, ax;
  u16 gs, fs, es, ds, eflags;
} regs16_t;

/* io.h */
typedef enum {
  MODE_A = 'A',
  MODE_B = 'B',
  MODE_C = 'C',
  MODE_D = 'D',
  MODE_E = 'E',
  MODE_F = 'F',
  MODE_G = 'G',
  MODE_H = 'H',
  MODE_f = 'f',
  MODE_J = 'J',
  MODE_K = 'K',
  MODE_S = 'S',
  MODE_T = 'T',
  MODE_m = 'm'
} vt100_mode_t;
struct tty {
  int   is_using;                                     // 使用标志
  void *vram;                                         // 显存（也可以当做图层）
  int   x, y;                                         // 目前的 x y 坐标
  int   xsize, ysize;                                 // x 坐标大小 y 坐标大小
  int   Raw_y;                                        // 换行次数
  int   cur_moving;                                   // 光标需要移动吗
  u8    color;                                        // 颜色
  void (*putchar)(struct tty *res, int c);            // putchar函数
  void (*MoveCursor)(struct tty *res, int x, int y);  // MoveCursor函数
  void (*clear)(struct tty *res);                     // clear函数
  void (*screen_ne)(struct tty *res);                 // screen_ne函数
  void (*gotoxy)(struct tty *res, int x, int y);      // gotoxy函数
  void (*print)(struct tty *res, const char *string); // print函数
  void (*Draw_Box)(struct tty *res, int x, int y, int x1, int y1,
                   u8 color); // Draw_Box函数
  int (*fifo_status)(struct tty *res);
  int (*fifo_get)(struct tty *res);
  u32 reserved[4]; // 保留项

  //////////////实现VT100需要的//////////////////

  int          vt100;       // 是否检测到标志
  char         buffer[81];  // 缓冲区
  int          buf_p;       // 缓冲区指针
  int          done;        // 这个东西读取完毕没有？
  vt100_mode_t mode;        // 控制模式
  int          color_saved; // 保存的颜色
};
struct Input_StacK {
  char **Stack;
  u32    Stack_Size;
  u32    free;
  u32    Now;
  u32    times;
};
#define MAX_SHEETS 256
struct SHEET {
  vram_t        *buf;
  int            bxsize, bysize, vx0, vy0, col_inv, height, flags;
  struct SHTCTL *ctl;
  struct TASK   *task;
  void (*Close)(); // 为NULL表示没有关闭函数
  void *args;
};
struct SHTCTL {
  vram_t       *vram;
  u8           *map;
  int           xsize, ysize, top;
  struct SHEET *sheets[MAX_SHEETS];
  struct SHEET  sheets0[MAX_SHEETS];
};
#define COL_000000      0x00000000
#define COL_FF0000      0x00ff0000
#define COL_00FF00      0x0000ff00
#define COL_FFFF00      0x00ffff00
#define COL_0000FF      0x000000ff
#define COL_FF00FF      0x00ff00ff
#define COL_00FFFF      0x0000ffff
#define COL_C6C6C6      0x00c6c6c6
#define COL_848484      0x00848484
#define COL_840000      0x00840000
#define COL_008400      0x00008400
#define COL_848400      0x00848400
#define COL_000084      0x00000084
#define COL_840084      0x00840084
#define COL_008484      0x00008484
#define COL_FFFFFF      0x00ffffff
#define COL_TRANSPARENT 0x50ffffff

/* drivers.h */
struct ACPI_RSDP {
  char Signature[8];
  u8   Checksum;
  char OEMID[6];
  u8   Revision;
  u32  RsdtAddress;
  u32  Length;
  u32  XsdtAddress[2];
  u8   ExtendedChecksum;
  u8   Reserved[3];
};
struct ACPISDTHeader {
  char Signature[4];
  u32  Length;
  u8   Revision;
  u8   Checksum;
  char OEMID[6];
  char OEMTableID[8];
  u32  OEMRevision;
  u32  CreatorID;
  u32  CreatorRevision;
};
struct ACPI_RSDT {
  struct ACPISDTHeader header;
  u32                  Entry;
};
typedef struct {
  u8  AddressSpace;
  u8  BitWidth;
  u8  BitOffset;
  u8  AccessSize;
  u32 Address[2];
} GenericAddressStructure;
struct ACPI_FADT {
  struct ACPISDTHeader h;
  u32                  FirmwareCtrl;
  u32                  Dsdt;

  // field used in ACPI 1.0; no longer in use, for compatibility only
  u8 Reserved;

  u8  PreferredPowerManagementProfile;
  u16 SCI_Interrupt;
  u32 SMI_CommandPort;
  u8  AcpiEnable;
  u8  AcpiDisable;
  u8  S4BIOS_REQ;
  u8  PSTATE_Control;
  u32 PM1aEventBlock;
  u32 PM1bEventBlock;
  u32 PM1aControlBlock;
  u32 PM1bControlBlock;
  u32 PM2ControlBlock;
  u32 PMTimerBlock;
  u32 GPE0Block;
  u32 GPE1Block;
  u8  PM1EventLength;
  u8  PM1ControlLength;
  u8  PM2ControlLength;
  u8  PMTimerLength;
  u8  GPE0Length;
  u8  GPE1Length;
  u8  GPE1Base;
  u8  CStateControl;
  u16 WorstC2Latency;
  u16 WorstC3Latency;
  u16 FlushSize;
  u16 FlushStride;
  u8  DutyOffset;
  u8  DutyWidth;
  u8  DayAlarm;
  u8  MonthAlarm;
  u8  Century;

  // reserved in ACPI 1.0; used since ACPI 2.0+
  u16 BootArchitectureFlags;

  u8  Reserved2;
  u32 Flags;

  // 12 byte structure; see below for details
  GenericAddressStructure ResetReg;

  u8 ResetValue;
  u8 Reserved3[3];

  // 64bit pointers - Available on ACPI 2.0+
  u32 X_FirmwareControl[2];
  u32 X_Dsdt[2];

  GenericAddressStructure X_PM1aEventBlock;
  GenericAddressStructure X_PM1bEventBlock;
  GenericAddressStructure X_PM1aControlBlock;
  GenericAddressStructure X_PM1bControlBlock;
  GenericAddressStructure X_PM2ControlBlock;
  GenericAddressStructure X_PMTimerBlock;
  GenericAddressStructure X_GPE0Block;
  GenericAddressStructure X_GPE1Block;
} __PACKED__;
#define BCD_HEX(n)      ((n >> 4) * 10) + (n & 0xf)
#define HEX_BCD(n)      ((n / 10) << 4) + (n % 10)
#define CMOS_CUR_SEC    0x0
#define CMOS_ALA_SEC    0x1
#define CMOS_CUR_MIN    0x2
#define CMOS_ALA_MIN    0x3
#define CMOS_CUR_HOUR   0x4
#define CMOS_ALA_HOUR   0x5
#define CMOS_WEEK_DAY   0x6
#define CMOS_MON_DAY    0x7
#define CMOS_CUR_MON    0x8
#define CMOS_CUR_YEAR   0x9
#define CMOS_DEV_TYPE   0x12
#define CMOS_CUR_CEN    0x32
#define cmos_index      0x70
#define cmos_data       0x71
#define PORT_KEYDAT     0x0060
#define PORT_KEYSTA     0x0064
#define PORT_KEYCMD     0x0064
#define MOUSE_ROLL_NONE 0
#define MOUSE_ROLL_UP   1
#define MOUSE_ROLL_DOWN 2
struct MOUSE_DEC {
  u8   buf[4], phase;
  int  x, y, btn;
  int  sleep;
  char roll;
};
struct pci_config_space_public {
  u16 VendorID;
  u16 DeviceID;
  u16 Command;
  u16 Status;
  u8  RevisionID;
  u8  ProgIF;
  u8  SubClass;
  u8  BaseClass;
  u8  CacheLineSize;
  u8  LatencyTimer;
  u8  HeaderType;
  u8  BIST;
  u32 BaseAddr[6];
  u32 CardbusCIS;
  u16 SubVendorID;
  u16 SubSystemID;
  u32 ROMBaseAddr;
  u8  CapabilitiesPtr;
  u8  Reserved[3];
  u32 Reserved1;
  u8  InterruptLine;
  u8  InterruptPin;
  u8  MinGrant;
  u8  MaxLatency;
};
typedef struct {
  u16 offset;
  u16 seg;
} ReadModeFarPointer;
typedef struct {
  u16 attributes;
  u8  winA, winB;
  u16 granularity;
  u16 winsize;
  u16 segmentA, segmentB;
  /* In VBE Specification, this field should be
   * ReadModeFarPointer winPosFunc;
   * However, we overwrite this field in loader n*/
  u16 mode;
  u16 reserved2;
  u16 bytesPerLine;
  u16 width, height;
  u8  Wchar, Ychar, planes, bitsPerPixel, banks;
  u8  memory_model, bank_size, image_pages;
  u8  reserved0;
  u8  red_mask, red_position;
  u8  green_mask, green_position;
  u8  blue_mask, blue_position;
  u8  rsv_mask, rsv_position;
  u8  directcolor_attributes;
  u32 physbase; // your LFB (Linear Framebuffer) address ;)
  u32 offscreen;
  u16 offsize;

} __PACKED__ VESAModeInfo;
typedef struct {
  u8                 signature[4];
  u16                Version;
  ReadModeFarPointer oemString;
  u32                capabilities;
  ReadModeFarPointer videoModes;
  u16                totalMemory;
  u16                OEMVersion;
  ReadModeFarPointer vendor;
  ReadModeFarPointer product;
  ReadModeFarPointer revision;
  /* In VBE Specification, this field should be reserved.
   * However, we overwrite this field in loader */
  u16                modeCount;
  u8                 reserved0[220];
  u8                 oemUse[256];
  VESAModeInfo       modeList[0];
} __PACKED__ VESAControllerInfo;
struct VBEINFO {
  char res1[18];
  i16  xsize, ysize;
  char res2[18];
  int  vram;
};
#define VBEINFO_ADDRESS                    0x7e00
#define VGA_AC_INDEX                       0x3C0
#define VGA_AC_WRITE                       0x3C0
#define VGA_AC_READ                        0x3C1
#define VGA_MISC_WRITE                     0x3C2
#define VGA_SEQ_INDEX                      0x3C4
#define VGA_SEQ_DATA                       0x3C5
#define VGA_DAC_READ_INDEX                 0x3C7
#define VGA_DAC_WRITE_INDEX                0x3C8
#define VGA_DAC_DATA                       0x3C9
#define VGA_MISC_READ                      0x3CC
#define VGA_GC_INDEX                       0x3CE
#define VGA_GC_DATA                        0x3CF
/*			COLOR emulation		MONO emulation */
#define VGA_CRTC_INDEX                     0x3D4 /* 0x3B4 */
#define VGA_CRTC_DATA                      0x3D5 /* 0x3B5 */
#define VGA_INSTAT_READ                    0x3DA
#define VGA_NUM_SEQ_REGS                   5
#define VGA_NUM_CRTC_REGS                  25
#define VGA_NUM_GC_REGS                    9
#define VGA_NUM_AC_REGS                    21
#define VGA_NUM_REGS                       (1 + VGA_NUM_SEQ_REGS + VGA_NUM_CRTC_REGS + VGA_NUM_GC_REGS + VGA_NUM_AC_REGS)
#define _vmemwr(DS, DO, S, N)              memcpy((char *)((DS)*16 + (DO)), S, N)
#define SB16_IRQ                           5
#define SB16_FAKE_TID                      -3
#define SB16_PORT_MIXER                    0x224
#define SB16_PORT_DATA                     0x225
#define SB16_PORT_RESET                    0x226
#define SB16_PORT_READ                     0x22A
#define SB16_PORT_WRITE                    0x22C
#define SB16_PORT_READ_STATUS              0x22E
#define SB16_PORT_DSP_16BIT_INTHANDLER_IRQ 0x22F
#define COMMAND_DSP_WRITE                  0x40
#define COMMAND_DSP_SOSR                   0x41
#define COMMAND_DSP_TSON                   0xD1
#define COMMAND_DSP_TSOF                   0xD3
#define COMMAND_DSP_STOP8                  0xD0
#define COMMAND_DSP_RP8                    0xD4
#define COMMAND_DSP_STOP16                 0xD5
#define COMMAND_DSP_RP16                   0xD6
#define COMMAND_DSP_VERSION                0xE1
#define COMMAND_MIXER_MV                   0x22
#define COMMAND_SET_IRQ                    0x80
#define BUF_RDY_VAL                        128
#define MAX_DRIVERS                        256
#define DRIVER_USE                         1
#define DRIVER_FREE                        0
typedef struct driver *drv_t;
typedef int            drv_type_t;
struct driver {
  struct TASK *drv_task; // 驱动程序的任务
  drv_type_t   drv_type; // 驱动程序类型
  int          flags;    // 驱动程序的状态
};
struct driver_ctl {
  struct driver drivers[MAX_DRIVERS]; // 驱动程序数组
  int           driver_num;           // 驱动程序数量
};
struct arg_struct {
  int   func_num;
  void *arg; // 参数(base=0x00)
  int   tid;
};
struct InitializationBlock {
  // 链接器所迫 只能这么写了
  u16      mode;
  u8       reserved1numSendBuffers;
  u8       reserved2numRecvBuffers;
  u8       mac0, mac1, mac2, mac3, mac4, mac5;
  u16      reserved3;
  uint64_t logicalAddress;
  u32      recvBufferDescAddress;
  u32      sendBufferDescAddress;
} __PACKED__;

struct BufferDescriptor {
  u32 address;
  u32 flags;
  u32 flags2;
  u32 avail;
} __PACKED__;

struct IDEHardDiskInfomationBlock {
  char reserve1[2];
  u16  CylinesNum;
  char reserve2[2];
  u16  HeadersNum;
  u16  TrackBytes;
  u16  SectorBytes;
  u16  TrackSectors;
  char reserve3[6];
  char OEM[20];
  char reserve4[2];
  u16  BuffersBytes;
  u16  EECCheckSumLength;
  char Version[8];
  char ID[40];
};

/* net */
#define swap32(x)                                                                                  \
  ((((x)&0xff000000) >> 24) | (((x)&0x00ff0000) >> 8) | (((x)&0x0000ff00) << 8) |                  \
   (((x)&0x000000ff) << 24))
#define swap16(x) ((((x)&0xff00) >> 8) | (((x)&0x00ff) << 8))
// 以太网帧
struct EthernetFrame_head {
  u8  dest_mac[6];
  u8  src_mac[6];
  u16 type;
} __PACKED__;
// 以太网帧--尾部
struct EthernetFrame_tail {
  u32 CRC; // 这里可以填写为0，网卡会自动计算
};
// ARP
#define ARP_PROTOCOL  0x0806
#define MAX_ARP_TABLE 256
#define ARP_WAITTIME  1
struct ARPMessage {
  u16 hardwareType;
  u16 protocol;
  u8  hardwareAddressSize;
  u8  protocolAddressSize;
  u16 command;
  u8  src_mac[6];
  u32 src_ip;
  u8  dest_mac[6];
  u32 dest_ip;
} __PACKED__;
// IPV4
#define IP_PROTOCOL 0x0800
#define MTU         1500
#define IP_MF       13
#define IP_DF       14
#define IP_OFFSET   0
struct IPV4Message {
  u8  headerLength : 4;
  u8  version      : 4;
  u8  tos;
  u16 totalLength;
  u16 ident;
  u16 flagsAndOffset;
  u8  timeToLive;
  u8  protocol;
  u16 checkSum;
  u32 srcIP;
  u32 dstIP;
} __PACKED__;
// ICMP
#define ICMP_PROTOCOL 1
struct ICMPMessage {
  u8  type;
  u8  code;
  u16 checksum;
  u16 ID;
  u16 sequence;
} __PACKED__;
#define PING_WAITTIME 200
#define PING_ID       0x0038
#define PING_SEQ      0x2115
#define PING_DATA     0x38
#define PING_SIZE     28
// UDP
#define UDP_PROTOCOL  17
struct UDPMessage {
  u16 srcPort;
  u16 dstPort;
  u16 length;
  u16 checkSum;
} __PACKED__;
// DHCP
#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN  64
#define DHCP_FILE_LEN   128
struct DHCPMessage {
  u8   opcode;
  u8   htype;
  u8   hlen;
  u8   hops;
  u32  xid;
  u16  secs;
  u16  flags;
  u32  ciaddr;
  u32  yiaddr;
  u32  siaddr;
  u32  giaddr;
  u8   chaddr[DHCP_CHADDR_LEN];
  char bp_sname[DHCP_SNAME_LEN];
  char bp_file[DHCP_FILE_LEN];
  u32  magic_cookie;
  u8   bp_options[0];
} __PACKED__;
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2

#define DHCP_HARDWARE_TYPE_10_EHTHERNET 1

#define MESSAGE_TYPE_PAD                0
#define MESSAGE_TYPE_REQ_SUBNET_MASK    1
#define MESSAGE_TYPE_ROUTER             3
#define MESSAGE_TYPE_DNS                6
#define MESSAGE_TYPE_DOMAIN_NAME        15
#define MESSAGE_TYPE_REQ_IP             50
#define MESSAGE_TYPE_DHCP               53
#define MESSAGE_TYPE_PARAMETER_REQ_LIST 55
#define MESSAGE_TYPE_END                255

#define DHCP_OPTION_DISCOVER 1
#define DHCP_OPTION_OFFER    2
#define DHCP_OPTION_REQUEST  3
#define DHCP_OPTION_PACK     4

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_MAGIC_COOKIE 0x63825363
// DNS
#define DNS_Header_ID     0x2115

#define DNS_TYPE_A     1
#define DNS_TYPE_NS    2
#define DNS_TYPE_MD    3
#define DNS_TYPE_MF    4
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_SOA   6
#define DNS_TYPE_MB    7
#define DNS_TYPE_MG    8
#define DNS_TYPE_MR    9
#define DNS_TYPE_NULL  10
#define DNS_TYPE_WKS   11
#define DNS_TYPE_PTR   12
#define DNS_TYPE_HINFO 13
#define DNS_TYPE_MINFO 14
#define DNS_TYPE_MX    15
#define DNS_TYPE_TXT   16
#define DNS_TYPE_ANY   255

#define DNS_CLASS_INET   1
#define DNS_CLASS_CSNET  2
#define DNS_CLASS_CHAOS  3
#define DNS_CLASS_HESIOD 4
#define DNS_CLASS_ANY    255

#define DNS_PORT      53
#define DNS_SERVER_IP 0x08080808
struct DNS_Header {
  u16 ID;
  u8  RD     : 1;
  u8  AA     : 1;
  u8  Opcode : 4;
  u8  QR     : 1;
  u8  RCODE  : 4;
  u8  Z      : 3;
  u8  RA     : 1;
  u8  TC     : 1;
  u16 QDcount;
  u16 ANcount;
  u16 NScount;
  u16 ARcount;
  u8  reserved;
} __PACKED__;
struct DNS_Question {
  u16 type;
  u16 Class;
} __PACKED__;
struct DNS_Answer {
  u32 name : 24;
  u16 type;
  u16 Class;
  u32 TTL;
  u16 RDlength;
  u8  reserved;
  u8  RData[0];
} __PACKED__;
// TCP
#define TCP_PROTOCOL         6
#define TCP_CONNECT_WAITTIME 1000
#define MSS_Default          1460
#define TCP_SEG_WAITTIME     10
struct TCPPesudoHeader {
  u32 srcIP;
  u32 dstIP;
  u16 protocol;
  u16 totalLength;
} __PACKED__;
struct TCPMessage {
  u16 srcPort;
  u16 dstPort;
  u32 seqNum;
  u32 ackNum;
  u8  reserved     : 4;
  u8  headerLength : 4;
  u8  FIN          : 1;
  u8  SYN          : 1;
  u8  RST          : 1;
  u8  PSH          : 1;
  u8  ACK          : 1;
  u8  URG          : 1;
  u8  ECE          : 1;
  u8  CWR          : 1;
  u16 window;
  u16 checkSum;
  u16 pointer;
  u32 options[0];
} __PACKED__;
// Socket
#define MAX_SOCKET_NUM 256
struct Socket {
  // 函数格式
  int (*Connect)(struct Socket *socket);                   // TCP
  void (*Disconnect)(struct Socket *socket);               // TCP
  void (*Listen)(struct Socket *socket);                   // TCP
  void (*Send)(struct Socket *socket, u8 *data, u32 size); // TCP/UDP
  void (*Handler)(struct Socket *socket, void *base);      // TCP/UDP
  // TCP/UDP
  u32   remoteIP;
  u16   remotePort;
  u32   localIP;
  u16   localPort;
  u8    state;
  u8    protocol;
  // TCP
  u32   seqNum;
  u32   ackNum;
  u16   MSS;
  int   flag; // 1 有包 0 没包
  int   size;
  char *buf;
} __PACKED__;
// UDP state
#define SOCKET_ALLOC              -1
// UDP/TCP state
#define SOCKET_FREE               0
// TCP state
#define SOCKET_TCP_CLOSED         1
#define SOCKET_TCP_LISTEN         2
#define SOCKET_TCP_SYN_SENT       3
#define SOCKET_TCP_SYN_RECEIVED   4
#define SOCKET_TCP_ESTABLISHED    5
#define SOCKET_TCP_FIN_WAIT1      6
#define SOCKET_TCP_FIN_WAIT2      7
#define SOCKET_TCP_CLOSING        8
#define SOCKET_TCP_TIME_WAIT      9
#define SOCKET_TCP_CLOSE_WAIT     10
#define SOCKET_TCP_LAST_ACK       11
// Socket Server
#define SOCKET_SERVER_MAX_CONNECT 32
struct SocketServer {
  struct Socket *socket[SOCKET_SERVER_MAX_CONNECT];
  void (*Send)(struct SocketServer *server, u8 *data,
               u32 size); // TCP/UDP
};
// Http
typedef struct HTTPGetHeader {
  bool ok;
  char path[13];
} HTTPGetHeader;
// ntp
struct NTPMessage {
  u8       VN   : 3;
  u8       LI   : 2;
  u8       Mode : 3;
  u8       Startum;
  u8       Poll;
  u8       Precision;
  u32      Root_Delay;
  u32      Root_Difference;
  u32      Root_Identifier;
  uint64_t Reference_Timestamp;
  uint64_t Originate_Timestamp;
  uint64_t Receive_Timestamp;
  uint64_t Transmission_Timestamp;
} __PACKED__;
#define NTPServer1              0xA29FC87B
#define NTPServer2              0x727607A3
// ftp
#define FTP_PORT_MODE           1
#define FTP_PASV_MODE           2
#define FTP_SERVER_DATA_PORT    20
#define FTP_SERVER_COMMAND_PORT 21
struct FTP_Client {
  int (*Login)(struct FTP_Client *ftp_c_, u8 *user, u8 *pass);
  int (*TransModeChoose)(struct FTP_Client *ftp_c_, int mode);
  void (*Logout)(struct FTP_Client *ftp_c_);
  int (*Download)(struct FTP_Client *ftp_c_, u8 *path_pdos, u8 *path_ftp, int mode);
  int (*Upload)(struct FTP_Client *ftp_c_, u8 *path_pdos, u8 *path_ftp, int mode);
  int (*Delete)(struct FTP_Client *ftp_c_, u8 *path_ftp);
  u8 *(*Getlist)(struct FTP_Client *ftp_c_);
  struct Socket *socket_cmd;
  struct Socket *socket_dat;
  bool           is_using;
  bool           is_login;
  u8            *recv_buf_cmd;
  bool           recv_flag_cmd;
  u32            reply_code;
  u8            *recv_buf_dat;
  bool           recv_flag_dat;
  u32            recv_dat_size;
};
typedef struct {
  void (*Read)(char drive, u8 *buffer, u32 number, u32 lba);
  void (*Write)(char drive, u8 *buffer, u32 number, u32 lba);
  int  flag;
  u32  size; // 大小
  char DriveName[50];
} vdisk;
// signal
#define SIGINT     0
#define SIGKIL     1
#define SIGMASK(n) 1 << n
