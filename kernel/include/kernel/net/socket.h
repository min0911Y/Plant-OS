#pragma once
#include "net.h"

// TCP
#define TCP_PROTOCOL         6
#define TCP_CONNECT_WAITTIME 1000
#define MSS_Default          1460
#define TCP_SEG_WAITTIME     10
struct TCPPesudoHeader {
  u32 srcIP;
  u32 dstIP;
  u16 protocol;
  u16 totalLength;
} __PACKED__;
struct TCPMessage {
  u16 srcPort;
  u16 dstPort;
  u32 seqNum;
  u32 ackNum;
  u8  reserved     : 4;
  u8  headerLength : 4;
  u8  FIN          : 1;
  u8  SYN          : 1;
  u8  RST          : 1;
  u8  PSH          : 1;
  u8  ACK          : 1;
  u8  URG          : 1;
  u8  ECE          : 1;
  u8  CWR          : 1;
  u16 window;
  u16 checkSum;
  u16 pointer;
  u32 options[0];
} __PACKED__;
// Socket
#define MAX_SOCKET_NUM 256
struct Socket {
  // 函数格式
  int (*Connect)(struct Socket *socket);                   // TCP
  void (*Disconnect)(struct Socket *socket);               // TCP
  void (*Listen)(struct Socket *socket);                   // TCP
  void (*Send)(struct Socket *socket, u8 *data, u32 size); // TCP/UDP
  void (*Handler)(struct Socket *socket, void *base);      // TCP/UDP
  // TCP/UDP
  u32   remoteIP;
  u16   remotePort;
  u32   localIP;
  u16   localPort;
  u8    state;
  u8    protocol;
  // TCP
  u32   seqNum;
  u32   ackNum;
  u16   MSS;
  int   flag; // 1 有包 0 没包
  int   size;
  char *buf;
} __PACKED__;
// UDP state
#define SOCKET_ALLOC              -1
// UDP/TCP state
#define SOCKET_FREE               0
// TCP state
#define SOCKET_TCP_CLOSED         1
#define SOCKET_TCP_LISTEN         2
#define SOCKET_TCP_SYN_SENT       3
#define SOCKET_TCP_SYN_RECEIVED   4
#define SOCKET_TCP_ESTABLISHED    5
#define SOCKET_TCP_FIN_WAIT1      6
#define SOCKET_TCP_FIN_WAIT2      7
#define SOCKET_TCP_CLOSING        8
#define SOCKET_TCP_TIME_WAIT      9
#define SOCKET_TCP_CLOSE_WAIT     10
#define SOCKET_TCP_LAST_ACK       11
// Socket Server
#define SOCKET_SERVER_MAX_CONNECT 32
struct SocketServer {
  struct Socket *socket[SOCKET_SERVER_MAX_CONNECT];
  void (*Send)(struct SocketServer *server, u8 *data,
               u32 size); // TCP/UDP
};

// ICMP
#define ICMP_PROTOCOL 1
struct ICMPMessage {
  u8  type;
  u8  code;
  u16 checksum;
  u16 ID;
  u16 sequence;
} __PACKED__;
#define PING_WAITTIME 200
#define PING_ID       0x0038
#define PING_SEQ      0x2115
#define PING_DATA     0x38
#define PING_SIZE     28
// UDP
#define UDP_PROTOCOL  17
struct UDPMessage {
  u16 srcPort;
  u16 dstPort;
  u16 length;
  u16 checkSum;
} __PACKED__;
