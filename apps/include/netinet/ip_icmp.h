#ifndef PLANT_NETINET_IP_ICMP_H
#define PLANT_NETINET_IP_ICMP_H

#include <ctypes.h>

struct icmp {
  uint8_t icmp_type;
  uint8_t icmp_code;
  uint16_t icmp_cksum;
  uint16_t icmp_id;
  uint16_t icmp_seq;
  uint8_t icmp_data[];
};

#define ICMP_MINLEN 8
#define ICMP_ECHOREPLY 0
#define ICMP_ECHO 8

#endif
