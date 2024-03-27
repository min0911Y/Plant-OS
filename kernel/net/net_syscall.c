#include <dos.h>
static struct Socket *wait[10] = {NULL, NULL, NULL, NULL, NULL,
                                  NULL, NULL, NULL, NULL, NULL};
static void *data[10];
static uint32_t size[10] = {0};
static void handler_udp(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 =
      (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct UDPMessage *udp =
      (struct UDPMessage *)(base + sizeof(struct EthernetFrame_head) +
                            sizeof(struct IPV4Message));
  void *dat = base + sizeof(struct EthernetFrame_head) +
              sizeof(struct IPV4Message) + sizeof(struct UDPMessage);
  uint32_t total_size = swap16(ipv4->totalLength) - sizeof(struct IPV4Message) -
                        sizeof(struct UDPMessage);
  for (uint32_t i = 0; i != 10; i++) {
    if (!wait[i]) {
      wait[i] = socket;
      data[i] = malloc(total_size);
      size[i] = total_size;
      uint32_t size0 = total_size;
      memcpy(data[i], dat, size0);
      break;
    }
  }
}
static void handler_tcp(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 =
      (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct TCPMessage *tcp =
      (struct TCPMessage *)(base + sizeof(struct EthernetFrame_head) +
                            sizeof(struct IPV4Message));
  void *dat = base + sizeof(struct EthernetFrame_head) +
              sizeof(struct IPV4Message) + (tcp->headerLength * 4);
  uint32_t total_size = swap16(ipv4->totalLength) - sizeof(struct IPV4Message) -
                        (tcp->headerLength * 4);
  if(socket->flag == 0) {
    memcpy(socket->buf,dat,total_size);
    socket->size = total_size;
    socket->flag = 1;
  }
}
enum { EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX };
void net_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx,
             int eax) {
  mtask *task = current_task();
  int cs_base = 0;
  int ds_base = 0;
  int alloc_addr = task->alloc_addr; // malloc地址
  uint32_t *reg = &eax + 1;          /* eax后面的地址*/
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
    socket->buf = malloc(4096);
    reg[EAX] = socket;
  } else if (eax == 0x02) {
    struct Socket *s = (struct Socket *)ebx;
    if (s->state == SOCKET_TCP_ESTABLISHED) {
      s->Disconnect(s);
    }
    free(s->buf);
    socket_free((struct Socket *)ebx);
  } else if (eax == 0x03) {
    struct Socket *socket = (struct Socket *)ebx;
    socket->Send(socket, (uint8_t *)(ds_base + ecx), edx);
  } else if (eax == 0x04) {
    int t = -1;
    struct Socket *s = (struct Socket *)ebx;
    while (1) {
      if (s->state != SOCKET_TCP_ESTABLISHED && s->protocol == TCP_PROTOCOL) {
        reg[EAX] = 0;
        return;
      }
     // printk("%d\n",s->flag);
      if(s->flag) {
        break;
      }
    }
    uint32_t sz;
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
    extern uint32_t ip;
    reg[EAX] = ip;
  } else if (eax == 0x07) {
    reg[EAX] = ping(ebx);
  } else if (eax == 0x08) {
    struct Socket *socket = (struct Socket *)ebx;
    socket->Listen(socket);
  }
}