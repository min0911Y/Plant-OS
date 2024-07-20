#pragma once
#include "net.h"

struct NTPMessage {
  u8  VN   : 3;
  u8  LI   : 2;
  u8  Mode : 3;
  u8  Startum;
  u8  Poll;
  u8  Precision;
  u32 Root_Delay;
  u32 Root_Difference;
  u32 Root_Identifier;
  u64 Reference_Timestamp;
  u64 Originate_Timestamp;
  u64 Receive_Timestamp;
  u64 Transmission_Timestamp;
} __PACKED__;
#define NTPServer1 0xA29FC87B
#define NTPServer2 0x727607A3
