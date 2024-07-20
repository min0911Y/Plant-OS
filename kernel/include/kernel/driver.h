#pragma once
#include "config.h"
#include <define.h>
#include <type.h>

#define DRIVER_USE  1
#define DRIVER_FREE 0
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
