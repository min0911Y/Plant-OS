#ifndef _DEFINE_H
#define _DEFINE_H
#include <arch.h>
#include <ctypes.h>
#include <stdarg.h>
#include <stddef.h>
typedef unsigned int vram_t;
typedef vram_t color_t;

/* dos.h */
#define VERSION "0.7b" // Version of the program
#define ADR_BOTPAK 0x00280000
#define LIMIT_BOTPAK 0x0007ffff
#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040
#define NULL_TID 11459810
#define Panic_Print(func, info, ...)                                           \
  func("%s--PANIC: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__,   \
       ##__VA_ARGS__);
#define WARNING_Print(func, info, ...)                                         \
  func("%s--WARNING: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__, \
       ##__VA_ARGS__);
#define DEBUG_Print(func, info, ...)                                           \
  func("%s--DEBUG: %s:%d Info:" info "\n", __FUNCTION__, __FILE__, __LINE__,   \
       ##__VA_ARGS__);
#define Panic_K(info, ...) Panic_Print(logk, info, ##__VA_ARGS__)
#define WARNING_K(info, ...) WARNING_Print(logk, info, ##__VA_ARGS__)
#define DEBUG_K(info, ...) DEBUG_Print(logk, info, ##__VA_ARGS__)
#define Panic_F(info, ...) Panic_Print(printf, info, ##__VA_ARGS__)
#define WARNING_F(info, ...) WARNING_Print(printf, info, ##__VA_ARGS__)
#define DEBUG_F(info, ...) DEBUG_Print(printf, info, ##__VA_ARGS__)
#define get_tid(task) task->tid
#define POWERINTDOS 0
#define HIGHTEXTMODE 1
extern struct MOUSE_DEC mdec;
extern int gmx, gmy;
extern unsigned char *font, *ascfont, *hzkfont;
extern struct TIMERCTL timerctl;
extern unsigned int memsize;
extern uint32_t running_mode;

#define MAX_TIMER 500
struct mtask;
typedef struct mtask mtask;
struct vfs_context;
struct TIMER {
  struct TIMER *next;
  unsigned int timeout, flags;
  struct FIFO8 *fifo;
  unsigned char data;
  mtask *waiter;
};
struct TIMERCTL {
  volatile unsigned int count, next;
  struct TIMER *t0;
  struct TIMER timers0[MAX_TIMER];
};
#define MAX_IPC_MESSAGE 16   // 每个任务的消息队列深度
#define IPC_MAX_MSG_SIZE 4096 // 单条消息负载的最大字节数
#define IPC_NAME_MAX 32       // 服务名最大长度（含结尾的 '\0'）
#define IPC_MAX_SERVICE 32    // 全局最多注册的服务名数量
// 旧接口保留的消息类型
#define synchronous 1
#define asynchronous 2
// ipc_send / ipc_recv 的标志位
#define IPC_NOWAIT 0x01     // 队列满/无消息时立刻返回，不阻塞
#define IPC_DELIVER_NOW 0x02 // 接收者因收信阻塞时直接切换给它
#define IPC_ANY_TID ((uint32_t)-1) // ipc_recv：接收任意发送者的消息
// IPC 返回值（0 或正数表示成功）
#define IPC_OK 0
#define IPC_ERR_INVAL -1    // 参数错误
#define IPC_ERR_NOTASK -2   // 目标任务不存在（或世代号不匹配）
#define IPC_ERR_FULL -3     // 目标队列已满
#define IPC_ERR_EMPTY -4    // 没有消息可读
#define IPC_ERR_TOOBIG -5   // 负载超过 IPC_MAX_MSG_SIZE
#define IPC_ERR_EXIST -6    // 服务名已被占用
#define IPC_ERR_NOMEM -7    // 内核内存不足
#define IPC_ERR_TIMEOUT -8  // 等待超时
#define IPC_ERR_NOTFOUND -9 // 服务名不存在
typedef struct {            // 一条消息
  void *data;               // 内核堆里的负载副本
  uint32_t size;            // 负载字节数
  uint32_t from_tid;        // 发送者 tid
  uint32_t from_generation; // 发送者的世代号（tid 会复用）
  uint32_t type;            // 用户自定义类型（RPC 用它区分请求/应答）
  uint32_t id;              // 用户自定义关联号（RPC 的调用序号）
  uint64_t seq;             // 入队序号，用于保证先进先出
  volatile int used;        // 该槽位是否有消息
} IPCMessage;
// 传给系统调用的消息描述符，apps/include/ipc.h 里有一份完全相同的定义
typedef struct {
  uint32_t peer_tid;        // 发送：目标 tid；接收：输出发送者 tid
  uint32_t peer_generation; // 发送：0 表示不校验；接收：输出发送者世代号
  uint32_t type;            // 消息类型
  uint32_t id;              // 关联号
  uint32_t size;            // 发送：负载长度；接收：入参为缓冲区容量，出参为实际长度
  uint32_t flags;           // IPC_NOWAIT ...
  uint32_t timeout_ms;      // 0 表示一直等待
  uint32_t from_filter;     // 接收：只收该 tid 的消息，IPC_ANY_TID 表示不过滤
  void *data;               // 负载缓冲区
} ipc_user_msg_t;
// 收到消息后回填给内核调用者的信息
typedef struct {
  uint32_t from_tid;
  uint32_t from_generation;
  uint32_t type;
  uint32_t id;
  uint32_t size;
} ipc_msg_info_t;
// lock.c
typedef struct {
  mtask *owner;
  unsigned value;
  mtask *waiter;
} lock_t;
#define LOCK_UNLOCKED 0
#define LOCK_LOCKED 1
typedef struct { // IPC头（在TASK结构体中的头）
  uint32_t count; // 队列中的消息数
  uint64_t seq;   // 下一条消息的入队序号
  IPCMessage messages[MAX_IPC_MESSAGE];
} IPC_Header;
// struct THREAD {
//   struct TASK *father;
// };
// struct TASK {
//   int sel, sleep, level;
//   char name[32];
//   char running;
//   struct tty *TTY;
//   struct FIFO8 *keyfifo, *mousefifo; // 基本输入设备的缓冲区
//   int fifosleep;
//   int cs_base, ds_base;
//   void *alloc_addr;
//   int alloc_size;
//   struct IPC_Header IPC_header;
//   struct TIMER *timer;
//   int esp_start; // 开始的esp
//   int eip_start; // 开始的eip
//   short cs_start;
//   short ss_start;
//   int is_child; // 是子线程吗
//   int app;
//   struct THREAD thread;
//   int drive_number;
//   char drive;
//   char *line;
//   void (*keyboard_press)(unsigned char data, uint32_t task);
//   void (*keyboard_release)(unsigned char data, uint32_t task);
//   int nl;
//   int lock; // 被锁住了？
//   char forever;
//   int mx, my;
//   struct vfs_t *nfs;
//   struct FIFO8 *Pkeyfifo, *Ukeyfifo;
//   uint32_t fpu_use;
//   uint32_t *gdt_data;
//   uint32_t pde;
// } __attribute__((packed));
enum STATE {
  EMPTY,
  RUNNING,
  WAITING,
  SLEEPING,
  WILL_EMPTY,
  READY,
  ALLOCATING,
  DIED
};
enum TASK_KIND { TASK_PROCESS, TASK_THREAD };
enum TASK_SCHED_FLAGS {
  TASK_SCHED_IDLE = 1u << 0,
  TASK_SCHED_PINNED = 1u << 1,
};
enum WAIT_REASON {
  WAIT_REASON_NONE,
  WAIT_REASON_GENERIC,
  WAIT_REASON_CHILD,
  WAIT_REASON_LOCK,
  WAIT_REASON_DISK,
  WAIT_REASON_TIMER,
  WAIT_REASON_TASK_GROUP_LOCK,
  WAIT_REASON_IPC,
  WAIT_REASON_SOCKET,
  WAIT_REASON_KEYBOARD
};
typedef struct mtask {
  arch_task_context_t *context;
  uintptr_t entry;
  unsigned pde;
  unsigned user_mode;
  unsigned top;
  unsigned weight;
  enum STATE state; // 此项为1（RUNNING） 即正常调度，为 2（WAITING） 3
                    // （SLEEPING）的时候不执行 ，0 EMPTY 空闲格子
  uint64_t vruntime;
  uint64_t runtime_ticks;
  uint16_t cpu;
  uint8_t on_cpu;
  uint8_t sched_flags;
  char name[32];
  struct vfs_context *fs_context;
  uint32_t tid;
  uint32_t ptid; /* parent process id; it does not own this task's lifetime */
  uint32_t tgid; /* process/thread-group leader tid */
  uint32_t generation;
  enum TASK_KIND kind;
  uint32_t alloc_addr;
  uint32_t *alloc_size;
  uint32_t alloced;
  struct tty *TTY;
  struct tty *tty_session;
  x86_fpu_state_t fpu_state;
  uint8_t fpu_initialized;
  struct FIFO8 *Pkeyfifo, *Ukeyfifo;
  struct FIFO8 *keyfifo, *mousefifo; // 基本输入设备的缓冲区
  char urgent;
  void (*keyboard_press)(unsigned char data, uint32_t task);
  void (*keyboard_release)(unsigned char data, uint32_t task);
  char fifosleep;
  int mx, my;
  volatile char *line;
  struct TIMER *timer;
  IPC_Header ipc_header;
  uint32_t ipc_wait_peer;    /* 阻塞发送时等待的目标 tid，TASK_ID_NONE 表示没有 */
  uint32_t ipc_deadline;     /* 带超时的 IPC 等待到期的时钟节拍 */
  uint32_t ipc_deadline_set; /* 上面的 ipc_deadline 是否有效 */
  uint32_t waittid;
  uint32_t wait_generation;
  enum WAIT_REASON wait_reason;
  uint32_t group_lock_owner;
  uint32_t group_lock_depth;
  int ready; // 如果为waiting 则无视wating
  int sigint_up;
  unsigned status;
  unsigned terminate_status;
  unsigned terminate_pending;
  unsigned signal;
  unsigned handler[30];
  unsigned ret_to_app;
  unsigned times;
  unsigned signal_disable;
} mtask;
#define PG_P 1
#define PG_USU 4
#define PG_RWW 2
#define PG_PCD 16
#define PG_SHARED 1024
#define PDE_ADDRESS 0x400000
#define PTE_ADDRESS (PDE_ADDRESS + 0x1000)
#define PAGE_END (PTE_ADDRESS + 0x400000)
#define PAGE_MANNAGER PAGE_END
struct FIFO8 {
  unsigned char *buf;
  int p, q, size, free, flags;
};
struct ListCtl {
  struct List *start;
  struct List *end;
  int all;
};
struct List {
  struct ListCtl *ctl;
  struct List *prev;
  uintptr_t val;
  struct List *next;
};
typedef struct List List;

/* cmd.h */
#define CHAT_SERVER_IP 0x761ff8d7
#define CHAT_SERVER_PROT 25565
#define CHAT_CLIENT_PROT 21538

/* fs.h */

extern uint32_t Path_Addr;
struct FAT_CACHE {
  unsigned int ADR_DISKIMG;
  struct FAT_FILEINFO *root_directory;
  struct List *directory_list;
  struct List *directory_clustno_list;
  struct List *directory_max_list;
  int *fat;
  int FatMaxTerms;
  unsigned int ClustnoBytes;
  unsigned short RootMaxFiles;
  unsigned int RootDictAddress;
  unsigned int FileDataAddress;
  unsigned int imgTotalSize;
  unsigned short SectorBytes;
  unsigned int Fat1Address, Fat2Address;
  unsigned char *FatClustnoFlags;
  int type;
};
#define get_clustno(high, low)                                                \
  (((uint32_t)(high) << 16) | ((uint32_t)(low) & 0xffffu))
typedef enum { FLE, DIR, RDO, HID, SYS } ftype;
typedef struct {
  char name[255];
  ftype type;
  unsigned int size;
  unsigned short year, month, day;
  unsigned short hour, minute;
} vfs_file;
typedef struct {
  uint32_t value[4];
} vfs_node_id_t;
typedef enum {
  VFS_NODE_FILE,
  VFS_NODE_DIRECTORY,
} vfs_node_type_t;
typedef struct {
  vfs_node_id_t id;
  vfs_node_type_t type;
  uint32_t size;
  ftype attributes;
  uint32_t modified_time;
} vfs_node_t;
typedef struct {
  char name[255];
  vfs_node_t node;
} vfs_dir_entry_t;
struct vfs_mount;
typedef struct vfs_mount vfs_t;
typedef struct vfs_filesystem {
  const char *name;
  bool (*check)(uint8_t disk_number);
  bool (*format)(uint8_t disk_number);
  int (*mount)(struct vfs_mount *mount);
  void (*unmount)(struct vfs_mount *mount);
  int (*root)(struct vfs_mount *mount, vfs_node_t *node);
  int (*normalize_name)(const char *name, size_t length, char *normalized,
                        size_t capacity);
  int (*lookup)(struct vfs_mount *mount, const vfs_node_t *directory,
                const char *name, vfs_node_t *node);
  int (*read)(struct vfs_mount *mount, const vfs_node_t *node,
              uint32_t offset, void *buffer, uint32_t length);
  int (*write)(struct vfs_mount *mount, vfs_node_t *node, uint32_t offset,
               const void *buffer, uint32_t length);
  int (*truncate)(struct vfs_mount *mount, vfs_node_t *node, uint32_t size);
  int (*create)(struct vfs_mount *mount, const vfs_node_t *directory,
                const char *name, vfs_node_type_t type, vfs_node_t *node);
  int (*remove)(struct vfs_mount *mount, const vfs_node_t *directory,
                const char *name, vfs_node_type_t type);
  int (*rename)(struct vfs_mount *mount, const vfs_node_t *source_directory,
                const char *source_name,
                const vfs_node_t *destination_directory,
                const char *destination_name);
  int (*iterate)(struct vfs_mount *mount, const vfs_node_t *directory,
                 uint32_t index, vfs_dir_entry_t *entry);
  int (*sync)(struct vfs_mount *mount);
} vfs_filesystem_t;
#define BS_jmpBoot 0
#define BS_OEMName 3
#define BPB_BytsPerSec 11
#define BPB_SecPerClus 13
#define BPB_RsvdSecCnt 14
#define BPB_NumFATs 16
#define BPB_RootEntCnt 17
#define BPB_TotSec16 19
#define BPB_Media 21
#define BPB_FATSz16 22
#define BPB_SecPerTrk 24
#define BPB_NumHeads 26
#define BPB_HiddSec 28
#define BPB_TotSec32 32
#define BPB_FATSz32 36
#define BPB_ExtFlags 40
#define BPB_FSVer 42
#define BPB_RootClus 44
#define BPB_FSInfo 48
#define BPB_BkBootSec 50
#define BPB_Reserved 52
#define BPB_Fat32ExtByts 28
#define BS_DrvNum 36
#define BS_Reserved1 37
#define BS_BootSig 38
#define BS_VolD 39
#define BS_VolLab 43
#define BS_FileSysType 54
#define EOF -1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
struct FAT_FILEINFO {
  unsigned char name[8], ext[3], type;
  char reserve;
  unsigned char create_time_tenth;
  unsigned short create_time, create_date, access_date, clustno_high;
  unsigned short update_time, update_date, clustno_low;
  unsigned int size;
};
#define rmfarptr2ptr(x) ((x).seg * 0x10 + (x).offset)
struct DLL_STRPICENV {
  int work[16384];
};
struct RGB {
  unsigned char b, g, r, t;
};
struct paw_info {
  unsigned char reserved[12]; // 12 bytes reserved(0xFF)
  char oem[3];                // PRA
  int xsize;                  // xsize
  int ysize;                  // ysize
};

/* interrupts.h */
#define PIC0_ICW1 0x0020
#define PIC0_OCW2 0x0020
#define PIC0_IMR 0x0021
#define PIC0_ICW2 0x0021
#define PIC0_ICW3 0x0021
#define PIC0_ICW4 0x0021
#define PIC1_ICW1 0x00a0
#define PIC1_OCW2 0x00a0
#define PIC1_IMR 0x00a1
#define PIC1_ICW2 0x00a1
#define PIC1_ICW3 0x00a1
#define PIC1_ICW4 0x00a1
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
  int using1;                              // 使用标志
  void *vram;                              // 显存（也可以当做图层）
  int x, y;                                // 目前的 x y 坐标
  int xsize, ysize;                        // x 坐标大小 y 坐标大小
  int Raw_y;                               // 换行次数
  int cur_moving;                          // 光标需要移动吗
  unsigned char color;                     // 颜色
  void (*putchar)(struct tty *res, int c); // putchar函数
  void (*MoveCursor)(struct tty *res, int x, int y);  // MoveCursor函数
  void (*clear)(struct tty *res);                     // clear函数
  void (*screen_ne)(struct tty *res);                 // screen_ne函数
  void (*gotoxy)(struct tty *res, int x, int y);      // gotoxy函数
  void (*print)(struct tty *res, const char *string); // print函数
  void (*Draw_Box)(struct tty *res, int x, int y, int x1, int y1,
                   unsigned char color); // Draw_Box函数
  int (*fifo_status)(struct tty *res);
  int (*fifo_get)(struct tty *res);
  unsigned int reserved[4]; // 保留项

  //////////////实现VT100需要的//////////////////

  int vt100;         // 是否检测到标志
  char buffer[81];   // 缓冲区
  int buf_p;         // 缓冲区指针
  int done;          // 这个东西读取完毕没有？
  vt100_mode_t mode; // 控制模式
  int color_saved;   // 保存的颜色
};
struct Input_StacK {
  char **Stack;
  unsigned int Stack_Size;
  unsigned int free;
  unsigned int Now;
  unsigned int times;
};
#define MAX_SHEETS 256
struct SHEET {
  vram_t *buf;
  int bxsize, bysize, vx0, vy0, col_inv, height, flags;
  struct SHTCTL *ctl;
  struct TASK *task;
  void (*Close)(); // 为NULL表示没有关闭函数
  void *args;
};
struct SHTCTL {
  vram_t *vram;
  unsigned char *map;
  int xsize, ysize, top;
  struct SHEET *sheets[MAX_SHEETS];
  struct SHEET sheets0[MAX_SHEETS];
};
#define COL_000000 0x00000000
#define COL_FF0000 0x00ff0000
#define COL_00FF00 0x0000ff00
#define COL_FFFF00 0x00ffff00
#define COL_0000FF 0x000000ff
#define COL_FF00FF 0x00ff00ff
#define COL_00FFFF 0x0000ffff
#define COL_C6C6C6 0x00c6c6c6
#define COL_848484 0x00848484
#define COL_840000 0x00840000
#define COL_008400 0x00008400
#define COL_848400 0x00848400
#define COL_000084 0x00000084
#define COL_840084 0x00840084
#define COL_008484 0x00008484
#define COL_FFFFFF 0x00ffffff
#define COL_TRANSPARENT 0x50ffffff

/* drivers.h */
struct ACPI_RSDP {
  char Signature[8];
  unsigned char Checksum;
  char OEMID[6];
  unsigned char Revision;
  unsigned int RsdtAddress;
  unsigned int Length;
  unsigned int XsdtAddress[2];
  unsigned char ExtendedChecksum;
  unsigned char Reserved[3];
};
struct ACPISDTHeader {
  char Signature[4];
  unsigned int Length;
  unsigned char Revision;
  unsigned char Checksum;
  char OEMID[6];
  char OEMTableID[8];
  unsigned int OEMRevision;
  unsigned int CreatorID;
  unsigned int CreatorRevision;
};
struct ACPI_RSDT {
  struct ACPISDTHeader header;
  unsigned int Entry;
};
typedef struct {
  unsigned char AddressSpace;
  unsigned char BitWidth;
  unsigned char BitOffset;
  unsigned char AccessSize;
  unsigned int Address[2];
} GenericAddressStructure;
struct ACPI_FADT {
  struct ACPISDTHeader h;
  unsigned int FirmwareCtrl;
  unsigned int Dsdt;

  // field used in ACPI 1.0; no longer in use, for compatibility only
  unsigned char Reserved;

  unsigned char PreferredPowerManagementProfile;
  unsigned short SCI_Interrupt;
  unsigned int SMI_CommandPort;
  unsigned char AcpiEnable;
  unsigned char AcpiDisable;
  unsigned char S4BIOS_REQ;
  unsigned char PSTATE_Control;
  unsigned int PM1aEventBlock;
  unsigned int PM1bEventBlock;
  unsigned int PM1aControlBlock;
  unsigned int PM1bControlBlock;
  unsigned int PM2ControlBlock;
  unsigned int PMTimerBlock;
  unsigned int GPE0Block;
  unsigned int GPE1Block;
  unsigned char PM1EventLength;
  unsigned char PM1ControlLength;
  unsigned char PM2ControlLength;
  unsigned char PMTimerLength;
  unsigned char GPE0Length;
  unsigned char GPE1Length;
  unsigned char GPE1Base;
  unsigned char CStateControl;
  unsigned short WorstC2Latency;
  unsigned short WorstC3Latency;
  unsigned short FlushSize;
  unsigned short FlushStride;
  unsigned char DutyOffset;
  unsigned char DutyWidth;
  unsigned char DayAlarm;
  unsigned char MonthAlarm;
  unsigned char Century;

  // reserved in ACPI 1.0; used since ACPI 2.0+
  unsigned short BootArchitectureFlags;

  unsigned char Reserved2;
  unsigned int Flags;

  // 12 byte structure; see below for details
  GenericAddressStructure ResetReg;

  unsigned char ResetValue;
  unsigned char Reserved3[3];

  // 64bit pointers - Available on ACPI 2.0+
  unsigned int X_FirmwareControl[2];
  unsigned int X_Dsdt[2];

  GenericAddressStructure X_PM1aEventBlock;
  GenericAddressStructure X_PM1bEventBlock;
  GenericAddressStructure X_PM1aControlBlock;
  GenericAddressStructure X_PM1bControlBlock;
  GenericAddressStructure X_PM2ControlBlock;
  GenericAddressStructure X_PMTimerBlock;
  GenericAddressStructure X_GPE0Block;
  GenericAddressStructure X_GPE1Block;
} __attribute__((packed));
struct ACPI_MADT {
  struct ACPISDTHeader h;
  uint32_t LocalApicAddress;
  uint32_t Flags;
  uint8_t Entries[0];
} __attribute__((packed));
#define BCD_HEX(n) ((n >> 4) * 10) + (n & 0xf)
#define HEX_BCD(n) ((n / 10) << 4) + (n % 10)
#define CMOS_CUR_SEC 0x0
#define CMOS_ALA_SEC 0x1
#define CMOS_CUR_MIN 0x2
#define CMOS_ALA_MIN 0x3
#define CMOS_CUR_HOUR 0x4
#define CMOS_ALA_HOUR 0x5
#define CMOS_WEEK_DAY 0x6
#define CMOS_MON_DAY 0x7
#define CMOS_CUR_MON 0x8
#define CMOS_CUR_YEAR 0x9
#define CMOS_DEV_TYPE 0x12
#define CMOS_CUR_CEN 0x32
#define cmos_index 0x70
#define cmos_data 0x71
#define PORT_KEYDAT 0x0060
#define PORT_KEYSTA 0x0064
#define PORT_KEYCMD 0x0064
#define MOUSE_ROLL_NONE 0
#define MOUSE_ROLL_UP 1
#define MOUSE_ROLL_DOWN 2
struct MOUSE_DEC {
  unsigned char buf[4], phase;
  int x, y, btn;
  int sleep;
  char roll;
};
struct pci_config_space_public {
  unsigned short VendorID;
  unsigned short DeviceID;
  unsigned short Command;
  unsigned short Status;
  unsigned char RevisionID;
  unsigned char ProgIF;
  unsigned char SubClass;
  unsigned char BaseClass;
  unsigned char CacheLineSize;
  unsigned char LatencyTimer;
  unsigned char HeaderType;
  unsigned char BIST;
  unsigned int BaseAddr[6];
  unsigned int CardbusCIS;
  unsigned short SubVendorID;
  unsigned short SubSystemID;
  unsigned int ROMBaseAddr;
  unsigned char CapabilitiesPtr;
  unsigned char Reserved[3];
  unsigned int Reserved1;
  unsigned char InterruptLine;
  unsigned char InterruptPin;
  unsigned char MinGrant;
  unsigned char MaxLatency;
};
typedef struct {
  unsigned short offset;
  unsigned short seg;
} ReadModeFarPointer;
typedef struct {
  unsigned short attributes;
  unsigned char winA, winB;
  unsigned short granularity;
  unsigned short winsize;
  unsigned short segmentA, segmentB;
  /* In VBE Specification, this field should be
   * ReadModeFarPointer winPosFunc;
   * However, we overwrite this field in loader n*/
  unsigned short mode;
  unsigned short reserved2;
  unsigned short bytesPerLine;
  unsigned short width, height;
  unsigned char Wchar, Ychar, planes, bitsPerPixel, banks;
  unsigned char memory_model, bank_size, image_pages;
  unsigned char reserved0;
  unsigned char red_mask, red_position;
  unsigned char green_mask, green_position;
  unsigned char blue_mask, blue_position;
  unsigned char rsv_mask, rsv_position;
  unsigned char directcolor_attributes;
  unsigned int physbase; // your LFB (Linear Framebuffer) address ;)
  unsigned int offscreen;
  unsigned short offsize;

} __attribute__((packed)) VESAModeInfo;
typedef struct {
  unsigned char signature[4];
  unsigned short Version;
  ReadModeFarPointer oemString;
  unsigned int capabilities;
  ReadModeFarPointer videoModes;
  unsigned short totalMemory;
  unsigned short OEMVersion;
  ReadModeFarPointer vendor;
  ReadModeFarPointer product;
  ReadModeFarPointer revision;
  /* In VBE Specification, this field should be reserved.
   * However, we overwrite this field in loader */
  unsigned short modeCount;
  unsigned char reserved0[220];
  unsigned char oemUse[256];
  VESAModeInfo modeList[0];
} __attribute__((packed)) VESAControllerInfo;
struct VBEINFO {
  char res1[18];
  short xsize, ysize;
  char res2[18];
  int vram;
};
#define VBEINFO_ADDRESS 0x7e00
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_WRITE 0x3C0
#define VGA_AC_READ 0x3C1
#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_DAC_READ_INDEX 0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA 0x3C9
#define VGA_MISC_READ 0x3CC
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
/*			COLOR emulation		MONO emulation */
#define VGA_CRTC_INDEX 0x3D4 /* 0x3B4 */
#define VGA_CRTC_DATA 0x3D5  /* 0x3B5 */
#define VGA_INSTAT_READ 0x3DA
#define VGA_NUM_SEQ_REGS 5
#define VGA_NUM_CRTC_REGS 25
#define VGA_NUM_GC_REGS 9
#define VGA_NUM_AC_REGS 21
#define VGA_NUM_REGS                                                           \
  (1 + VGA_NUM_SEQ_REGS + VGA_NUM_CRTC_REGS + VGA_NUM_GC_REGS + VGA_NUM_AC_REGS)
#define _vmemwr(DS, DO, S, N) memcpy((char *)((DS) * 16 + (DO)), S, N)
#define SB16_IRQ 5
#define SB16_FAKE_TID -3
#define SB16_PORT_MIXER 0x224
#define SB16_PORT_DATA 0x225
#define SB16_PORT_RESET 0x226
#define SB16_PORT_READ 0x22A
#define SB16_PORT_WRITE 0x22C
#define SB16_PORT_READ_STATUS 0x22E
#define SB16_PORT_DSP_16BIT_INTHANDLER_IRQ 0x22F
#define COMMAND_DSP_WRITE 0x40
#define COMMAND_DSP_SOSR 0x41
#define COMMAND_DSP_TSON 0xD1
#define COMMAND_DSP_TSOF 0xD3
#define COMMAND_DSP_STOP8 0xD0
#define COMMAND_DSP_RP8 0xD4
#define COMMAND_DSP_STOP16 0xD5
#define COMMAND_DSP_RP16 0xD6
#define COMMAND_DSP_VERSION 0xE1
#define COMMAND_MIXER_MV 0x22
#define COMMAND_SET_IRQ 0x80
#define BUF_RDY_VAL 128
#define MAX_DRIVERS 256
#define DRIVER_USE 1
#define DRIVER_FREE 0
typedef struct driver *drv_t;
typedef int drv_type_t;
struct driver {
  struct TASK *drv_task; // 驱动程序的任务
  drv_type_t drv_type;   // 驱动程序类型
  int flags;             // 驱动程序的状态
};
struct driver_ctl {
  struct driver drivers[MAX_DRIVERS]; // 驱动程序数组
  int driver_num;                     // 驱动程序数量
};
struct arg_struct {
  int func_num;
  void *arg; // 参数(base=0x00)
  int tid;
};
struct IDEHardDiskInfomationBlock {
  char reserve1[2];
  unsigned short CylinesNum;
  char reserve2[2];
  unsigned short HeadersNum;
  unsigned short TrackBytes;
  unsigned short SectorBytes;
  unsigned short TrackSectors;
  char reserve3[6];
  char OEM[20];
  char reserve4[2];
  unsigned short BuffersBytes;
  unsigned short EECCheckSumLength;
  char Version[8];
  char ID[40];
};

typedef enum {
  VDISK_TYPE_NONE,
  VDISK_TYPE_BLOCK,
  VDISK_TYPE_OPTICAL,
} vdisk_type_t;

typedef struct {
  void (*Read)(char drive, unsigned char *buffer, unsigned int number,
               unsigned int lba);
  void (*Write)(char drive, unsigned char *buffer, unsigned int number,
                unsigned int lba);
  vdisk_type_t flag;
  unsigned int size; // 大小
  unsigned int max_transfer_sectors;
  char DriveName[50];
} vdisk;
// signal
#define SIGINT 0
#define SIGKIL 1
#define SIGMASK(n) 1 << n
#endif
