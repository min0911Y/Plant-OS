#pragma once
#include <define.h>
#include <kernel.h>
#include <type.h>

// 以太网帧
void ether_frame_provider_send(u64 dest_mac, u16 type, u8 *buffer, u32 size);
// ARP
u8  *ARP_Packet(u64 dest_mac, u32 dest_ip, u64 src_mac, u32 src_ip, u16 command);
u64  IPParseMAC(u32 dstIP);
void arp_handler(void *base);
// IPV4
void IPV4ProviderSend(u8 protocol, u64 dest_mac, u32 dest_ip, u32 src_ip, u8 *data, u32 size);
u16  CheckSum(u16 *dat, u32 size);
u32  IP2UINT32_T(u8 *ip);
// ICMP
u8  *ICMP_Packet(u8 type, u8 code, u16 ID, u16 sequence, u8 *data, u32 size);
void ICMPProviderSend(u32 destip, u32 srcip, u8 type, u8 code, u16 ID, u16 sequence, u8 *data,
                      u32 size);
int  ping(u32 dstIP);
void cmd_ping(char *cmdline);
void icmp_handler(void *base);
// UDP
u8  *UDP_Packet(u16 dest_port, u16 src_port, u8 *data, u32 size);
void udp_provider_send(u32 destip, u32 srcip, u16 dest_port, u16 src_port, u8 *data, u32 size);
void udp_handler(void *base);
// DCHP
int  dhcp_discovery(u8 *mac);
void dhcp_handler(void *base);
// DNS
u32  dns_parse_ip(u8 *name);
void dns_handler(void *base);
// TCP
void tcp_provider_send(u32 dstIP, u32 srcIP, u16 dstPort, u16 srcPort, u32 Sequence, u32 ackNum,
                       bool URG, bool ACK, bool PSH, bool RST, bool SYN, bool FIN, bool ECE,
                       bool CWR, u8 *data, u32 size);
void tcp_handler(void *base);
// Socket
struct Socket *socket_alloc(u8 protocol);
void           socket_free(struct Socket *socket);
void Socket_Init(struct Socket *socket, u32 remoteIP, u16 remotePort, u32 localIP, u16 localPort);
void Socket_Bind(struct Socket *socket, void (*Handler)(struct Socket *socket, void *base));
struct Socket       *Socket_Find(u32 dstIP, u16 dstPort, u32 srcIP, u16 srcPort, u8 protocol);
struct SocketServer *SocketServer_Alloc(void (*Handler)(struct Socket *socket, void *base),
                                        u32 srcIP, u16 srcPort, u8 protocol);
void                 SocketServer_Free(struct SocketServer *server, u8 protocol);
u32                  SocketServer_Status(struct SocketServer *server, u8 state);
// HTTP
HTTPGetHeader        http_check(u8 *data, u32 size);
int                  __get_week(int year, int month, int day);
int                  get_week();
void                 GetNowDate(char *result);
// net_syscall
void net_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax);
// NTP
u32  ntp_get_server_time(u32 NTPServerIP);
u32  ntp_time_stamp(u32 year, u32 month, u32 day, u32 hour, u32 min, u32 sec);
u32  UTCTimeStamp(u32 year, u32 month, u32 day, u32 hour, u32 min, u32 sec);
void UnNTPTimeStamp(u32 timestamp, u32 *year, u32 *month, u32 *day, u32 *hour, u32 *min, u32 *sec);
void UnUTCTimeStamp(u32 timestamp, u32 *year, u32 *month, u32 *day, u32 *hour, u32 *min, u32 *sec);
// FTP
struct FTP_Client *ftp_client_find(struct Socket *s);
struct FTP_Client *FTP_Client_Alloc(u32 remoteIP, u32 localIP, u16 localPort);
