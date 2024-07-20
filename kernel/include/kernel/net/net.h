#pragma once
#include <define.h>
#include <type.h>

#define swap32(x)                                                                                  \
  ((((x)&0xff000000) >> 24) | (((x)&0x00ff0000) >> 8) | (((x)&0x0000ff00) << 8) |                  \
   (((x)&0x000000ff) << 24))
#define swap16(x) ((((x)&0xff00) >> 8) | (((x)&0x00ff) << 8))

// 以太网帧
struct EthernetFrame_head {
  u8  dest_mac[6];
  u8  src_mac[6];
  u16 type;
} __PACKED__;
// 以太网帧--尾部
struct EthernetFrame_tail {
  u32 CRC; // 这里可以填写为0，网卡会自动计算
};
