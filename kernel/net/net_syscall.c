#include <dos.h>
static void handler_udp(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct UDPMessage  *udp =
      (struct UDPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  void *dat = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
              sizeof(struct UDPMessage);
  u32 total_size =
      swap16(ipv4->totalLength) - sizeof(struct IPV4Message) - sizeof(struct UDPMessage);
  if (socket->flag == 0) {
    memcpy(socket->buf, dat, total_size);
    socket->size = total_size;
    socket->flag = 1;
  }
}
static void handler_tcp(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct TCPMessage  *tcp =
      (struct TCPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  void *dat = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
              (tcp->headerLength * 4);
  u32 total_size = swap16(ipv4->totalLength) - sizeof(struct IPV4Message) - (tcp->headerLength * 4);
  if (socket->flag == 0) {
    memcpy(socket->buf, dat, total_size);
    socket->size = total_size;
    socket->flag = 1;
  }
}
enum {
  EDI,
  ESI,
  EBP,
  ESP,
  EBX,
  EDX,
  ECX,
  EAX
};
void net_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax) {
  mtask *task       = current_task();
  int    cs_base    = 0;
  int    ds_base    = 0;
  int    alloc_addr = task->alloc_addr; // malloc地址
  u32   *reg        = &eax + 1;         /* eax后面的地址*/
                                        /*强行改写通过PUSHAD保存的值*/
  /* reg[0] : EDI,   reg[1] : ESI,   reg[2] : EBP,   reg[3] : ESP */
  /* reg[4] : EBX,   reg[5] : EDX,   reg[6] : ECX,   reg[7] : EAX */
  if (eax == 0x01) { // Socket
    struct Socket *socket = socket_alloc(ebx);
    if (ebx == UDP_PROTOCOL) {
      Socket_Bind(socket, handler_udp);
    } else if (ebx == TCP_PROTOCOL) {
      Socket_Bind(socket, handler_tcp);
    }
    socket->flag = 0;
    socket->buf  = malloc(4096);
    reg[EAX]     = (u32)socket;
  } else if (eax == 0x02) {
    struct Socket *s = (struct Socket *)ebx;
    if (s->state == SOCKET_TCP_ESTABLISHED) { s->Disconnect(s); }
    free(s->buf);
    socket_free((struct Socket *)ebx);
  } else if (eax == 0x03) {
    struct Socket *socket = (struct Socket *)ebx;
    socket->Send(socket, (u8 *)(ds_base + ecx), edx);
  } else if (eax == 0x04) {
    int            t = -1;
    struct Socket *s = (struct Socket *)ebx;
    while (1) {
      if (s->state != SOCKET_TCP_ESTABLISHED && s->protocol == TCP_PROTOCOL) {
        reg[EAX] = 0;
        return;
      }
      if (s->flag) { break; }
    }
    u32 sz;
    if (edx > s->size) {
      sz = s->size;
    } else {
      sz = edx;
    }
    reg[EAX] = sz;
    memcpy((void *)(ds_base + ecx), s->buf, sz);
    s->flag = 0;
  } else if (eax == 0x05) {
    struct Socket *socket = (struct Socket *)ebx;
    Socket_Init(socket, ecx, edx, esi, edi);
  } else if (eax == 0x06) {
    extern u32 ip;
    reg[EAX] = ip;
  } else if (eax == 0x07) {
    reg[EAX] = ping(ebx);
  } else if (eax == 0x08) {
    struct Socket *socket = (struct Socket *)ebx;
    socket->Listen(socket);
  } else if (eax == 0x09) {
    struct Socket *socket = (struct Socket *)ebx;
    if (socket->protocol == TCP_PROTOCOL) reg[EAX] = socket->Connect(socket);
  }
}