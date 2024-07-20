#pragma once
#include "net.h"

#define ARP_PROTOCOL  0x0806
#define MAX_ARP_TABLE 256
#define ARP_WAITTIME  1
struct ARPMessage {
  u16 hardwareType;
  u16 protocol;
  u8  hardwareAddressSize;
  u8  protocolAddressSize;
  u16 command;
  u8  src_mac[6];
  u32 src_ip;
  u8  dest_mac[6];
  u32 dest_ip;
} __PACKED__;
