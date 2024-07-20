#pragma once
#include <define.h>
#include <type.h>
typedef struct {
  void (*Read)(char drive, u8 *buffer, u32 number, u32 lba);
  void (*Write)(char drive, u8 *buffer, u32 number, u32 lba);
  int  flag;
  u32  size; // 大小
  char DriveName[50];
} vdisk;