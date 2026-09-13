#ifndef PLANT_NET_IF_H
#define PLANT_NET_IF_H

#include <socket.h>

#define IF_NAMESIZE 16
#define IFNAMSIZ IF_NAMESIZE
#define IFHWADDRLEN 6

#define IFF_UP 0x0001
#define IFF_BROADCAST 0x0002
#define IFF_DEBUG 0x0004
#define IFF_LOOPBACK 0x0008
#define IFF_POINTOPOINT 0x0010
#define IFF_NOTRAILERS 0x0020
#define IFF_RUNNING 0x0040
#define IFF_NOARP 0x0080
#define IFF_PROMISC 0x0100
#define IFF_ALLMULTI 0x0200
#define IFF_MULTICAST 0x1000

struct ifreq {
  char ifr_name[IFNAMSIZ];
  union {
    struct sockaddr ifru_addr;
    struct sockaddr ifru_broadaddr;
    struct sockaddr ifru_dstaddr;
    struct sockaddr ifru_netmask;
    struct sockaddr ifru_hwaddr;
    short ifru_flags;
    int ifru_mtu;
    int ifru_index;
  } ifr_ifru;
};

#define ifr_addr ifr_ifru.ifru_addr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_dstaddr ifr_ifru.ifru_dstaddr
#define ifr_netmask ifr_ifru.ifru_netmask
#define ifr_hwaddr ifr_ifru.ifru_hwaddr
#define ifr_flags ifr_ifru.ifru_flags
#define ifr_mtu ifr_ifru.ifru_mtu
#define ifr_index ifr_ifru.ifru_index

struct ifconf {
  int ifc_len;
  union {
    char *ifcu_buf;
    struct ifreq *ifcu_req;
  } ifc_ifcu;
};

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

#ifdef __cplusplus
extern "C" {
#endif

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);

#ifdef __cplusplus
}
#endif

#endif
