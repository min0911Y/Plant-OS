#pragma once
#include <define.h>
#include <type.h>
struct InitializationBlock {
  // 链接器所迫 只能这么写了
  u16 mode;
  u8  reserved1numSendBuffers;
  u8  reserved2numRecvBuffers;
  u8  mac0, mac1, mac2, mac3, mac4, mac5;
  u16 reserved3;
  u64 logicalAddress;
  u32 recvBufferDescAddress;
  u32 sendBufferDescAddress;
} __PACKED__;

struct BufferDescriptor {
  u32 address;
  u32 flags;
  u32 flags2;
  u32 avail;
} __PACKED__;