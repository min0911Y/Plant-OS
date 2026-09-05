#ifndef PLOS_LWIPOPTS_H
#define PLOS_LWIPOPTS_H

/* Plant OS runs lwIP directly on the kernel's single CPU.  Packet input and
 * timer processing are serialized by interrupt state, so the callback API is
 * both smaller and a better fit than lwIP's threaded socket layer. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_TCPIP_CORE_LOCKING 0

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_CALLBACK_API 1
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_AUTOIP 0
#define LWIP_ACD 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_IGMP 0

#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_ALTCP 0
#define LWIP_EVENT_API 0

#define LWIP_TIMERS 1
#define LWIP_TIMERS_CUSTOM 0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 0
/* Use lwIP's native lo interface.  NO_SYS polling happens in
 * net_stack_tick(), outside raw-API output callbacks. */
#define LWIP_NETIF_LOOPBACK 1

#define MEM_ALIGNMENT __SIZEOF_POINTER__
#define MEM_SIZE (64 * 1024)
#define mem_free lwip_mem_free
#define atoi lwip_port_atoi
#define MEMP_MEM_MALLOC 0
#define MEMP_OVERFLOW_CHECK 0
#define MEMP_SANITY_CHECK 0
#define MEMP_NUM_PBUF 32
#define MEMP_NUM_RAW_PCB 4
#define MEMP_NUM_UDP_PCB 16
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG 64
#define MEMP_NUM_REASSDATA 4
#define MEMP_NUM_SYS_TIMEOUT 16

#define PBUF_POOL_SIZE 32
#define PBUF_POOL_BUFSIZE 1536
#define PBUF_LINK_HLEN 14

#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_LISTEN_BACKLOG 1
#define TCP_OVERSIZE 0

#define IP_REASSEMBLY 1
#define IP_REASS_MAX_PBUFS 16
#define IP_FRAG 1
#define IP_DEFAULT_TTL 64
#define TCP_TTL 64
#define UDP_TTL 64

#define ARP_TABLE_SIZE 10
#define ARP_QUEUEING 1
#define ARP_QUEUE_LEN 3

#define LWIP_DHCP_PROVIDE_DNS_SERVERS 1
#define LWIP_STATS 0
#define LWIP_DEBUG 0

#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_ICMP 1

#endif
