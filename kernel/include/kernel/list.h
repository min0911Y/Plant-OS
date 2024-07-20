#pragma once
#include <define.h>
#include <type.h>

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