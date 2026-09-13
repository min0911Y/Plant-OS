#ifndef PLANT_NETINET_IN_H
#define PLANT_NETINET_IN_H

#include <socket.h>

/* BSD assigns AF_INET6 28; Plant OS currently exposes IPv4 sockets only, but
 * keeping the public layout lets applications detect that at runtime. */
#define AF_INET6 28
#define IPPROTO_IPV6 41

struct in6_addr {
  uint8_t s6_addr[16];
};

struct sockaddr_in6 {
  sa_family_t sin6_family;
  uint16_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t sin6_scope_id;
};

struct ipv6_mreq {
  struct in6_addr ipv6mr_multiaddr;
  uint32_t ipv6mr_interface;
};

#define INET6_ADDRSTRLEN 46
#define IN6ADDR_ANY_INIT {{0}}
#define IN6ADDR_LOOPBACK_INIT \
  {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}

static const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
static const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* IPv4 protocol and socket option constants used by the Unix JDK sources. */
#define IPPROTO_IP 0
#define IP_TOS 1
#define IP_TTL 2
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36
#define IP_MULTICAST_ALL 49

#define IP_UNBLOCK_SOURCE 37
#define IP_BLOCK_SOURCE 38
#define IP_ADD_SOURCE_MEMBERSHIP 39
#define IP_DROP_SOURCE_MEMBERSHIP 40

struct ip_mreq {
  struct in_addr imr_multiaddr;
  struct in_addr imr_interface;
};

struct ip_mreqn {
  struct in_addr imr_multiaddr;
  struct in_addr imr_address;
  int imr_ifindex;
};

struct ip_mreq_source {
  struct in_addr imr_multiaddr;
  struct in_addr imr_interface;
  struct in_addr imr_sourceaddr;
};

struct group_source_req {
  uint32_t gsr_interface;
  struct sockaddr_storage gsr_group;
  struct sockaddr_storage gsr_source;
};

#define IPV6_V6ONLY 27
#define IPV6_UNICAST_HOPS 4
#define IPV6_MULTICAST_IF 9
#define IPV6_MULTICAST_HOPS 10
#define IPV6_MULTICAST_LOOP 11
#define IPV6_JOIN_GROUP 12
#define IPV6_LEAVE_GROUP 13
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#define IPV6_CHECKSUM 7
#define IPV6_TCLASS 61
#define SOL_RAW 255

#define MCAST_JOIN_GROUP 41
#define MCAST_LEAVE_GROUP 42
#define MCAST_BLOCK_SOURCE 43
#define MCAST_UNBLOCK_SOURCE 44
#define MCAST_JOIN_SOURCE_GROUP 45
#define MCAST_LEAVE_SOURCE_GROUP 46

#endif
