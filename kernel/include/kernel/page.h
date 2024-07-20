#pragma once
#include <define.h>
#include <type.h>
#define PG_P          1
#define PG_USU        4
#define PG_RWW        2
#define PG_PCD        16
#define PG_SHARED     1024
#define PDE_ADDRESS   0x400000
#define PTE_ADDRESS   (PDE_ADDRESS + 0x1000)
#define PAGE_END      (PTE_ADDRESS + 0x400000)
#define PAGE_MANNAGER PAGE_END
struct PAGE_INFO {
  u8 task_id;
  u8 count;
} __PACKED__;