#pragma once
#include <define.h>
#include <type.h>
struct input_stack {
  char **stack;
  u32    stack_size;
  u32    free;
  u32    now;
  u32    times;
};
