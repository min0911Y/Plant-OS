#ifndef PLANT_IFADDRS_H
#define PLANT_IFADDRS_H

#include <net/if.h>

struct ifaddrs {
  struct ifaddrs *ifa_next;
  char *ifa_name;
  unsigned int ifa_flags;
  struct sockaddr *ifa_addr;
  struct sockaddr *ifa_netmask;
  union {
    struct sockaddr *ifu_broadaddr;
    struct sockaddr *ifu_dstaddr;
  } ifa_ifu;
  void *ifa_data;
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr ifa_ifu.ifu_dstaddr

#ifdef __cplusplus
extern "C" {
#endif

int getifaddrs(struct ifaddrs **result);
void freeifaddrs(struct ifaddrs *list);

#ifdef __cplusplus
}
#endif

#endif
