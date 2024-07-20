#pragma once
#include "net.h"

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