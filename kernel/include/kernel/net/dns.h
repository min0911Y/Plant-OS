#pragma once
#include "net.h"

#define DNS_Header_ID 0x2115

#define DNS_TYPE_A     1
#define DNS_TYPE_NS    2
#define DNS_TYPE_MD    3
#define DNS_TYPE_MF    4
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_SOA   6
#define DNS_TYPE_MB    7
#define DNS_TYPE_MG    8
#define DNS_TYPE_MR    9
#define DNS_TYPE_NULL  10
#define DNS_TYPE_WKS   11
#define DNS_TYPE_PTR   12
#define DNS_TYPE_HINFO 13
#define DNS_TYPE_MINFO 14
#define DNS_TYPE_MX    15
#define DNS_TYPE_TXT   16
#define DNS_TYPE_ANY   255

#define DNS_CLASS_INET   1
#define DNS_CLASS_CSNET  2
#define DNS_CLASS_CHAOS  3
#define DNS_CLASS_HESIOD 4
#define DNS_CLASS_ANY    255

#define DNS_PORT      53
#define DNS_SERVER_IP 0x08080808
struct DNS_Header {
  u16 ID;
  u8  RD     : 1;
  u8  AA     : 1;
  u8  Opcode : 4;
  u8  QR     : 1;
  u8  RCODE  : 4;
  u8  Z      : 3;
  u8  RA     : 1;
  u8  TC     : 1;
  u16 QDcount;
  u16 ANcount;
  u16 NScount;
  u16 ARcount;
  u8  reserved;
} __PACKED__;
struct DNS_Question {
  u16 type;
  u16 Class;
} __PACKED__;
struct DNS_Answer {
  u32 name : 24;
  u16 type;
  u16 Class;
  u32 TTL;
  u16 RDlength;
  u8  reserved;
  u8  RData[0];
} __PACKED__;
