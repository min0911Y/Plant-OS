#ifndef PLANT_NETINET_IP_H
#define PLANT_NETINET_IP_H

#include <socket.h>

struct ip {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  unsigned int ip_hl : 4;
  unsigned int ip_v : 4;
#else
  unsigned int ip_v : 4;
  unsigned int ip_hl : 4;
#endif
  uint8_t ip_tos;
  uint16_t ip_len;
  uint16_t ip_id;
  uint16_t ip_off;
  uint8_t ip_ttl;
  uint8_t ip_p;
  uint16_t ip_sum;
  struct in_addr ip_src;
  struct in_addr ip_dst;
};

#define IP_RF 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff

#endif
