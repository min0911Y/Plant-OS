#include <dos.h>
// Socket
static struct Socket sockets[MAX_SOCKET_NUM];
static void socket_udp_send(struct Socket *socket, uint8_t *data,
                            uint32_t size) {
  udp_provider_send(socket->remoteIP, socket->localIP, socket->remotePort,
                    socket->localPort, data, size);
}
void socket_init() {
  for (int i = 0; i < MAX_SOCKET_NUM; i++) {
    sockets->state = SOCKET_FREE;
  }
}

void usleep(uint64_t ns);
static void socket_tcp_send(struct Socket *socket, uint8_t *data,
                            uint32_t size) {
  while(socket->state != SOCKET_TCP_ESTABLISHED);
  uint32_t s = size;
  tcp_provider_send(socket->remoteIP, socket->localIP, socket->remotePort,
                    socket->localPort, socket->seqNum, socket->ackNum, 0, 1, 0,
                    0, 0, 0, 0, 0, data, s);
  socket->seqNum += s;
  //io_delay();
}
static int socket_tcp_connect(struct Socket *socket) {
  // printk("send first shake.\n");
  // printk("socket->state: SOCKET_TCP_CLOSED -> SOCKET_TCP_SYN_SENT\n");
  socket->ackNum = 0;
  socket->seqNum = 0;
  socket->state = SOCKET_TCP_SYN_SENT;
  tcp_provider_send(socket->remoteIP, socket->localIP, socket->remotePort,
                    socket->localPort, socket->seqNum, socket->ackNum, 0, 0, 0,
                    0, 1, 0, 0, 0, 0, 0);
  socket->seqNum++;
  extern struct TIMERCTL timerctl;
  uint32_t time = timerctl.count;
  while (socket->state != SOCKET_TCP_ESTABLISHED) {
    if (timerctl.count - time > TCP_CONNECT_WAITTIME) {
      return -1;
    }
  }
  return 0;
}
static void socket_tcp_disconnect(struct Socket *socket) {
  // printk("send first wave.\n");
  // printk("socket->state: SOCKET_TCP_ESTABLISHED -> SOCKET_TCP_FIN_WAIT1\n");
  socket->state = SOCKET_TCP_FIN_WAIT1;
  tcp_provider_send(socket->remoteIP, socket->localIP, socket->remotePort,
                    socket->localPort, socket->seqNum, socket->ackNum, 0, 1, 0,
                    0, 0, 1, 0, 0, 0, 0);
  socket->seqNum++;
  while (socket->state != SOCKET_TCP_CLOSED)
    ;
}
static void socket_tcp_listen(struct Socket *socket) {
  // printk("Listening...\n");
  // printk("socket->state: SOCKET_TCP_CLOSED -> SOCKET_TCP_LISTEN\n");
  socket->state = SOCKET_TCP_LISTEN;
  while (socket->state != SOCKET_TCP_ESTABLISHED)
    ;
}
static void SocketServer_Send(struct SocketServer *server, uint8_t *data,
                              uint32_t size) {
  for (int i = 0; i != SOCKET_SERVER_MAX_CONNECT; i++) {
    if (server->socket[i]->state == SOCKET_TCP_ESTABLISHED) {
      server->socket[i]->Send(server->socket[i], data, size);
    }
  }
}
struct Socket *socket_alloc(uint8_t protocol) {
  for (int i = 0; i != MAX_SOCKET_NUM; i++) {
    if (sockets[i].state == SOCKET_FREE) {
      if (protocol == UDP_PROTOCOL) { // UDP
        sockets[i].state = SOCKET_ALLOC;
        sockets[i].protocol = UDP_PROTOCOL;
        sockets[i].Send = socket_udp_send;
        sockets[i].Handler = NULL;
        return (struct Socket *)&sockets[i];
      } else if (protocol == TCP_PROTOCOL) { // TCP
        sockets[i].state = SOCKET_TCP_CLOSED;
        sockets[i].protocol = TCP_PROTOCOL;
        sockets[i].Send = socket_tcp_send;
        sockets[i].Connect = socket_tcp_connect;
        sockets[i].Disconnect = socket_tcp_disconnect;
        sockets[i].Listen = socket_tcp_listen;
        sockets[i].Handler = NULL;
        sockets[i].MSS = MSS_Default;
        return (struct Socket *)&sockets[i];
      }
    }
  }
  return -1;
}
void socket_free(struct Socket *socket) {
  for (int i = 0; i != MAX_SOCKET_NUM; i++) {
    if (&sockets[i] == socket) {
      sockets[i].state = SOCKET_FREE;
      return;
    }
  }
}
void Socket_Init(struct Socket *socket, uint32_t remoteIP, uint16_t remotePort,
                 uint32_t localIP, uint16_t localPort) {
  socket->remoteIP = remoteIP;
  socket->remotePort = remotePort;
  socket->localIP = localIP;
  socket->localPort = localPort;
}
void Socket_Bind(struct Socket *socket,
                 void (*Handler)(struct Socket *socket, void *base)) {
  socket->Handler = Handler;
}
struct Socket *Socket_Find(uint32_t dstIP, uint16_t dstPort, uint32_t srcIP,
                           uint16_t srcPort, uint8_t protocol) {
  for (int i = 0; i != MAX_SOCKET_NUM; i++) {
    if (srcIP == sockets[i].localIP && dstIP == sockets[i].remoteIP &&
        srcPort == sockets[i].localPort && dstPort == sockets[i].remotePort &&
        protocol == sockets[i].protocol && sockets[i].state != SOCKET_FREE) {
      return (struct Socket *)&sockets[i];
    } else if (srcIP == sockets[i].localIP && srcPort == sockets[i].localPort &&
               protocol == sockets[i].protocol &&
               sockets[i].state == SOCKET_TCP_LISTEN) {
      return (struct Socket *)&sockets[i];
    }
  }
  return -1;
}

struct SocketServer *
SocketServer_Alloc(void (*Handler)(struct Socket *socket, void *base),
                   uint32_t srcIP, uint16_t srcPort, uint8_t protocol) {
  struct SocketServer *server =
      (struct SocketServer *)malloc(sizeof(struct SocketServer));
  for (int i = 0; i < SOCKET_SERVER_MAX_CONNECT; i++) {
    if (protocol == TCP_PROTOCOL) {
      server->socket[i] = socket_alloc(TCP_PROTOCOL);
      Socket_Init(server->socket[i], 0, 0, srcIP, srcPort);
      Socket_Bind(server->socket[i], Handler);
      server->socket[i]->state = SOCKET_TCP_LISTEN;
    } else if (protocol == UDP_PROTOCOL) {
      server->socket[i] = socket_alloc(UDP_PROTOCOL);
      Socket_Init(server->socket[i], 0, 0, srcIP, srcPort);
      Socket_Bind(server->socket[i], Handler);
    }
  }
  server->Send = SocketServer_Send;
  return server;
}

void SocketServer_Free(struct SocketServer *server, uint8_t protocol) {
  for (int i = 0; i < SOCKET_SERVER_MAX_CONNECT; i++) {
    if (server->socket[i] != NULL &&
        server->socket[i]->state == SOCKET_TCP_ESTABLISHED) {
      if (protocol == TCP_PROTOCOL) {
        server->socket[i]->Disconnect(server->socket[i]);
      }
      socket_free(server->socket[i]);
    }
  }
  free(server);
}

uint32_t SocketServer_Status(struct SocketServer *server, uint8_t state) {
  uint32_t count = 0;
  for (int i = 0; i < SOCKET_SERVER_MAX_CONNECT; i++) {
    if (server->socket[i]->state == state) {
      count++;
    }
  }
  return count;
}
