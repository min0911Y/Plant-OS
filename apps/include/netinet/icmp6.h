#ifndef PLANT_NETINET_ICMP6_H
#define PLANT_NETINET_ICMP6_H

#include <netinet/in.h>

#define IPPROTO_ICMPV6 58

struct icmp6_hdr {
  uint8_t icmp6_type;
  uint8_t icmp6_code;
  uint16_t icmp6_cksum;
  union {
    uint32_t icmp6_un_data32[1];
    uint16_t icmp6_un_data16[2];
    uint8_t icmp6_un_data8[4];
  } icmp6_dataun;
};

#define icmp6_data32 icmp6_dataun.icmp6_un_data32
#define icmp6_data16 icmp6_dataun.icmp6_un_data16
#define icmp6_data8 icmp6_dataun.icmp6_un_data8
#define icmp6_id icmp6_data16[0]
#define icmp6_seq icmp6_data16[1]

#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY 129

#endif
