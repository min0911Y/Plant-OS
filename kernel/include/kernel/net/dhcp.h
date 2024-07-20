#pragma once
#include "net.h"

#define DHCP_CHADDR_LEN 16
#define DHCP_SNAME_LEN  64
#define DHCP_FILE_LEN   128
struct DHCPMessage {
  u8   opcode;
  u8   htype;
  u8   hlen;
  u8   hops;
  u32  xid;
  u16  secs;
  u16  flags;
  u32  ciaddr;
  u32  yiaddr;
  u32  siaddr;
  u32  giaddr;
  u8   chaddr[DHCP_CHADDR_LEN];
  char bp_sname[DHCP_SNAME_LEN];
  char bp_file[DHCP_FILE_LEN];
  u32  magic_cookie;
  u8   bp_options[0];
} __PACKED__;
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2

#define DHCP_HARDWARE_TYPE_10_EHTHERNET 1

#define MESSAGE_TYPE_PAD                0
#define MESSAGE_TYPE_REQ_SUBNET_MASK    1
#define MESSAGE_TYPE_ROUTER             3
#define MESSAGE_TYPE_DNS                6
#define MESSAGE_TYPE_DOMAIN_NAME        15
#define MESSAGE_TYPE_REQ_IP             50
#define MESSAGE_TYPE_DHCP               53
#define MESSAGE_TYPE_PARAMETER_REQ_LIST 55
#define MESSAGE_TYPE_END                255

#define DHCP_OPTION_DISCOVER 1
#define DHCP_OPTION_OFFER    2
#define DHCP_OPTION_REQUEST  3
#define DHCP_OPTION_PACK     4

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_MAGIC_COOKIE 0x63825363
