#pragma once
#include <define.h>
#include <type.h>
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