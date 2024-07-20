#pragma once
#include "config.h"
#include "cpu.h"
#include "mm.h"
#include <define.h>
#include <type.h>
typedef struct mtask mtask;

typedef struct {
  mtask   *owner;
  unsigned value;
  mtask   *waiter;
} lock_t;
#define LOCK_UNLOCKED 0
#define LOCK_LOCKED   1

#define synchronous  1
#define asynchronous 2
typedef struct {
  void *data;
  u32   size;
  int   from_tid;
  int   flag1;
  int   flag2;
} IPCMessage;

// IPC头（在TASK结构体中的头）
typedef struct {
  int        now;
  IPCMessage messages[MAX_IPC_MESSAGE];
  lock_t     l;
} IPC_Header;

typedef void (*cb_keyboard_t)(u8 data, u32 task);

enum STATE {
  EMPTY,
  RUNNING,
  WAITING,
  SLEEPING,
  WILL_EMPTY,
  READY,
  DIED
};

struct __PACKED__ mtask {
  stack_frame  *esp;
  unsigned      pde;
  unsigned      user_mode;
  unsigned      top;
  unsigned      running; // 已经占用了多少时间片
  unsigned      timeout; // 需要占用多少时间片
  int           floor;
  enum STATE    state; // 此项为1（RUNNING） 即正常调度，为 2（WAITING） 3
                       // （SLEEPING）的时候不执行 ，0 EMPTY 空闲格子
  u64           jiffies;
  struct vfs_t *nfs;
  u64           tid, ptid;
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
  cb_keyboard_t keyboard_press;
  cb_keyboard_t keyboard_release;
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
  u64           time_sec;
  u64           time_ns;
  u64           val_old;
};

#define NULL_TID      11459810
#define get_tid(task) task->tid
extern u8 *font, *ascfont, *hzkfont;
