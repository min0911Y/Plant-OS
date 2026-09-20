#ifndef _DEFINE_H
#define _DEFINE_H
#include <arch.h>
#include <ctypes.h>
#include <mouse.h>
#include <rpc.h>
#include <stdarg.h>
#include <stddef.h>
#include <user_thread.h>
#include <user_signal.h>
typedef unsigned int vram_t;
typedef vram_t color_t;

/* dos.h */
#define VERSION "0.7b" // Version of the program
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
extern int gmx, gmy;
extern unsigned char *font, *ascfont, *hzkfont;
extern struct TIMERCTL timerctl;
extern uintptr_t memsize;
extern uintptr_t physical_memory_limit;
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
  mtask *waiter_tail;
} lock_t;
#define LOCK_UNLOCKED 0
#define LOCK_LOCKED 1
typedef struct { // IPC头（在TASK结构体中的头）
  uint32_t count; // 队列中的消息数
  uint64_t seq;   // 下一条消息的入队序号
  IPCMessage messages[MAX_IPC_MESSAGE];
  struct rpc_pending *rpc;
} IPC_Header;
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
  WAIT_REASON_KEYBOARD,
  WAIT_REASON_INPUT,
  WAIT_REASON_USB,
  WAIT_REASON_TTY,
  WAIT_REASON_FUTEX,
  WAIT_REASON_IO,
  WAIT_REASON_SIGNAL
};
enum { TASK_KERNEL_STACK_SIZE = 64u * 1024u };
typedef struct mtask {
  arch_task_context_t *context;
  uintptr_t entry;
  arch_address_space_t address_space;
  unsigned user_mode;
  bool return_cwd;
  bool joinable;
  uintptr_t thread_pointer;
  thread_region_t user_tls, user_stack;
  uintptr_t top;
  unsigned weight;
  enum STATE state; // 此项为1（RUNNING） 即正常调度，为 2（WAITING） 3
                    // （SLEEPING）的时候不执行 ，0 EMPTY 空闲格子
  /* Intrusive runnable links; only run_next is reused after retirement. */
  struct mtask *run_next, **run_previous;
  uint64_t vruntime;
  uint64_t runtime_ns;
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
  uintptr_t alloc_addr;
  uintptr_t vm_hint;
  size_t *alloc_size;
  uint32_t alloced;
  struct tty *TTY;
  struct tty *tty_session;
  arch_fpu_state_t fpu_state;
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
  lock_t *waiting_lock;
  struct mtask *lock_next;
  struct mtask *lock_previous;
  uint32_t group_lock_owner;
  uint32_t group_lock_depth;
  int ready; // 如果为waiting 则无视wating
  int sigint_up;
  unsigned status;
  unsigned terminate_status;
  unsigned terminate_pending;
  task_signal_state_t signals;
  struct sigaction signal_actions[NSIG]; /* Used only by the group leader. */
  uintptr_t ret_to_app;
} mtask;
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

struct FAT_CACHE {
  uintptr_t ADR_DISKIMG;
  struct FAT_FILEINFO *root_directory;
  struct List *directory_list;
  struct List *directory_clustno_list;
  struct List *directory_max_list;
  int *fat;
  int FatMaxTerms;
  unsigned int ClustnoBytes;
  unsigned int RootMaxFiles;
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
  struct {
    rpc_endpoint_t server, busy;
    uint32_t opcode, handle;
    bool disconnected;
  } remote;
  uint32_t input_sequence;
  bool native_ansi;

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

typedef enum {
  VDISK_TYPE_NONE,
  VDISK_TYPE_BLOCK,
  VDISK_TYPE_OPTICAL,
} vdisk_type_t;

typedef struct {
  bool (*Read)(char drive, unsigned char *buffer, unsigned int number,
               unsigned int lba);
  bool (*Write)(char drive, unsigned char *buffer, unsigned int number,
                unsigned int lba);
  vdisk_type_t flag;
  uint64_t size; // Capacity in bytes.
  bool (*Sync)(char drive);
  bool owns_serialization;
  unsigned int max_transfer_sectors;
  char DriveName[50];
} vdisk;
#endif
