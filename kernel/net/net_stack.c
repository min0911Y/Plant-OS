#include <dos.h>
#include <irq.h>
#include <net_link.h>

#include <lwip/def.h>
#include <lwip/dhcp.h>
#include <lwip/err.h>
#include <lwip/etharp.h>
#include <lwip/inet_chksum.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/raw.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <lwip/udp.h>
#include <lwip/prot/icmp.h>
#include <lwip/prot/ip.h>
#include <netif/ethernet.h>

#define NET_PROTOCOL_TCP 6
#define NET_PROTOCOL_UDP 17
#define NET_SOCKET_CAPACITY 16
#define NET_RX_ITEM_CAPACITY 64
#define NET_HANDLE_INDEX_BITS 5
#define NET_HANDLE_INDEX_MASK ((1u << NET_HANDLE_INDEX_BITS) - 1u)
#define NET_HANDLE_GENERATION_MAX 0x03ffffffu
#define NET_CONNECT_TIMEOUT_TICKS 1000u
#define NET_PING_TIMEOUT_TICKS 200u
#define NET_PING_DATA_BYTES 32
#define NET_FRAME_BYTES 1514

typedef enum {
  NET_SOCKET_FREE,
  NET_SOCKET_UDP,
  NET_SOCKET_TCP_READY,
  NET_SOCKET_TCP_CONNECTING,
  NET_SOCKET_TCP_CONNECTED,
  NET_SOCKET_TCP_LISTENING,
  NET_SOCKET_TCP_FAILED,
} net_socket_state_t;

typedef struct net_rx_item {
  struct net_rx_item *next;
  struct pbuf *packet;
} net_rx_item_t;

typedef struct {
  uint32_t owner_group;
  uint32_t generation;
  uint32_t remote_ip;
  uint32_t local_ip;
  uint16_t remote_port;
  uint16_t local_port;
  net_socket_state_t state;
  uint8_t protocol;
  bool configured;
  bool bound;
  bool peer_closed;
  union {
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
  } pcb;
  net_rx_item_t *receive_head;
  net_rx_item_t *receive_tail;
} net_socket_t;

typedef struct {
  bool active;
  bool reply;
  uint16_t id;
  uint16_t sequence;
  struct raw_pcb *pcb;
} net_ping_t;

static struct netif net_interface;
static uint8_t net_mac[6];
static uint8_t net_transmit_frame[NET_FRAME_BYTES];
static bool net_started;
static net_socket_t net_sockets[NET_SOCKET_CAPACITY];
static net_rx_item_t net_rx_items[NET_RX_ITEM_CAPACITY];
static net_rx_item_t *net_rx_free;
static net_ping_t net_ping;

static void net_ip_from_u32(ip_addr_t *address, uint32_t value) {
  IP_ADDR4(address, value >> 24, value >> 16, value >> 8, value);
}

static void net_yield(void) {
  irq_state_t state = irq_save();
  task_next();
  irq_restore(state);
}

static void net_rx_pool_init(void) {
  net_rx_free = NULL;
  for (unsigned i = 0; i < NET_RX_ITEM_CAPACITY; i++) {
    net_rx_items[i].packet = NULL;
    net_rx_items[i].next = net_rx_free;
    net_rx_free = &net_rx_items[i];
  }
}

static bool net_socket_queue_packet(net_socket_t *socket, struct pbuf *packet) {
  if (net_rx_free == NULL) {
    return false;
  }

  net_rx_item_t *item = net_rx_free;
  net_rx_free = item->next;
  item->packet = packet;
  item->next = NULL;
  if (socket->receive_tail != NULL) {
    socket->receive_tail->next = item;
  } else {
    socket->receive_head = item;
  }
  socket->receive_tail = item;
  return true;
}

static void net_socket_clear_packets(net_socket_t *socket) {
  net_rx_item_t *item = socket->receive_head;
  while (item != NULL) {
    net_rx_item_t *next = item->next;
    pbuf_free(item->packet);
    item->packet = NULL;
    item->next = net_rx_free;
    net_rx_free = item;
    item = next;
  }
  socket->receive_head = NULL;
  socket->receive_tail = NULL;
}

static int net_socket_handle(unsigned index, uint32_t generation) {
  return (int)((generation << NET_HANDLE_INDEX_BITS) | (index + 1));
}

static net_socket_t *net_socket_find(uint32_t owner_group, int handle) {
  uint32_t value = (uint32_t)handle;
  uint32_t slot = value & NET_HANDLE_INDEX_MASK;
  uint32_t generation = value >> NET_HANDLE_INDEX_BITS;
  if (handle <= 0 || slot == 0 || slot > NET_SOCKET_CAPACITY) {
    return NULL;
  }

  net_socket_t *socket = &net_sockets[slot - 1];
  if (socket->state == NET_SOCKET_FREE || socket->owner_group != owner_group ||
      socket->generation != generation) {
    return NULL;
  }
  return socket;
}

static void net_tcp_clear_callbacks(struct tcp_pcb *pcb) {
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
}

static void net_socket_dispose(net_socket_t *socket) {
  if (socket->protocol == NET_PROTOCOL_UDP && socket->pcb.udp != NULL) {
    udp_recv(socket->pcb.udp, NULL, NULL);
    udp_remove(socket->pcb.udp);
  } else if (socket->protocol == NET_PROTOCOL_TCP && socket->pcb.tcp != NULL) {
    if (socket->state == NET_SOCKET_TCP_LISTENING) {
      tcp_arg(socket->pcb.tcp, NULL);
      tcp_accept(socket->pcb.tcp, NULL);
      if (tcp_close(socket->pcb.tcp) != ERR_OK) {
        tcp_abort(socket->pcb.tcp);
      }
    } else {
      net_tcp_clear_callbacks(socket->pcb.tcp);
      tcp_abort(socket->pcb.tcp);
    }
  }

  net_socket_clear_packets(socket);
  socket->pcb.tcp = NULL;
  socket->configured = false;
  socket->bound = false;
  socket->peer_closed = false;
  socket->protocol = 0;
  socket->owner_group = 0;
  socket->state = NET_SOCKET_FREE;
}

static void net_tcp_attach_callbacks(net_socket_t *socket,
                                     struct tcp_pcb *pcb);

static void net_udp_receive(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
                            const ip_addr_t *address, u16_t port) {
  (void)pcb;
  (void)address;
  (void)port;
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL || !net_socket_queue_packet(socket, packet)) {
    pbuf_free(packet);
  }
}

static err_t net_tcp_receive(void *arg, struct tcp_pcb *pcb,
                             struct pbuf *packet, err_t error) {
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL) {
    if (packet != NULL) {
      pbuf_free(packet);
    }
    return ERR_OK;
  }
  if (packet == NULL) {
    socket->peer_closed = true;
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    socket->state = NET_SOCKET_TCP_FAILED;
    return ERR_OK;
  }
  if (!net_socket_queue_packet(socket, packet)) {
    return ERR_MEM;
  }
  (void)pcb;
  return ERR_OK;
}

static void net_tcp_error(void *arg, err_t error) {
  (void)error;
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL) {
    return;
  }
  socket->pcb.tcp = NULL;
  socket->state = NET_SOCKET_TCP_FAILED;
}

static err_t net_tcp_connected(void *arg, struct tcp_pcb *pcb, err_t error) {
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL || error != ERR_OK) {
    if (socket != NULL) {
      socket->state = NET_SOCKET_TCP_FAILED;
    }
    return ERR_OK;
  }
  socket->pcb.tcp = pcb;
  socket->state = NET_SOCKET_TCP_CONNECTED;
  return ERR_OK;
}

static err_t net_tcp_accept(void *arg, struct tcp_pcb *new_pcb, err_t error) {
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL || error != ERR_OK || new_pcb == NULL ||
      socket->state != NET_SOCKET_TCP_LISTENING) {
    if (new_pcb != NULL) {
      tcp_abort(new_pcb);
    }
    return ERR_ABRT;
  }

  struct tcp_pcb *listener = socket->pcb.tcp;
  tcp_arg(listener, NULL);
  tcp_accept(listener, NULL);
  tcp_abort(listener);
  socket->pcb.tcp = new_pcb;
  socket->peer_closed = false;
  socket->state = NET_SOCKET_TCP_CONNECTED;
  net_tcp_attach_callbacks(socket, new_pcb);
  tcp_backlog_accepted(new_pcb);
  return ERR_OK;
}

static void net_tcp_attach_callbacks(net_socket_t *socket,
                                     struct tcp_pcb *pcb) {
  tcp_arg(pcb, socket);
  tcp_recv(pcb, net_tcp_receive);
  tcp_err(pcb, net_tcp_error);
}

static bool net_tcp_bind(net_socket_t *socket) {
  if (socket->bound) {
    return true;
  }
  ip_addr_t local;
  net_ip_from_u32(&local, socket->local_ip);
  if (tcp_bind(socket->pcb.tcp, &local, socket->local_port) != ERR_OK) {
    return false;
  }
  socket->bound = true;
  return true;
}

static err_t net_link_output(struct netif *interface, struct pbuf *packet) {
  (void)interface;
  if (packet->tot_len > sizeof(net_transmit_frame) ||
      pbuf_copy_partial(packet, net_transmit_frame, packet->tot_len, 0) !=
          packet->tot_len) {
    return ERR_BUF;
  }
  return net_link_transmit(net_transmit_frame, packet->tot_len) == 0 ? ERR_OK
                                                                       : ERR_IF;
}

static err_t net_interface_init(struct netif *interface) {
  interface->name[0] = 'e';
  interface->name[1] = 'n';
  interface->output = etharp_output;
  interface->linkoutput = net_link_output;
  interface->hwaddr_len = sizeof(net_mac);
  memcpy(interface->hwaddr, net_mac, sizeof(net_mac));
  interface->mtu = 1500;
  interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  return ERR_OK;
}

static void net_interface_status(struct netif *interface) {
  if (!netif_is_up(interface) || ip4_addr_isany_val(*netif_ip4_addr(interface))) {
    return;
  }
  uint32_t address = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(interface)));
  logk("network: DHCP %d.%d.%d.%d\n", (uint8_t)(address >> 24),
       (uint8_t)(address >> 16), (uint8_t)(address >> 8), (uint8_t)address);
}

static void net_receive_frame(const uint8_t *frame, uint16_t length) {
  if (!net_started || frame == NULL || length < 14 || length > NET_FRAME_BYTES) {
    return;
  }

  irq_state_t state = irq_save();
  struct pbuf *packet = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
  if (packet != NULL && pbuf_take(packet, frame, length) == ERR_OK) {
    if (net_interface.input(packet, &net_interface) != ERR_OK) {
      pbuf_free(packet);
    }
  } else if (packet != NULL) {
    pbuf_free(packet);
  }
  irq_restore(state);
}

bool net_stack_start(void) {
  if (net_started) {
    return true;
  }

  irq_state_t state = irq_save();
  lwip_init();
  net_rx_pool_init();

  const char *driver = NULL;
  if (!net_link_start(net_receive_frame, net_mac, &driver)) {
    logk("network: link driver initialization failed\n");
    irq_restore(state);
    return false;
  }

  ip4_addr_t zero;
  IP4_ADDR(&zero, 0, 0, 0, 0);
  if (netif_add(&net_interface, &zero, &zero, &zero, NULL,
                net_interface_init, ethernet_input) == NULL) {
    logk("network: netif registration failed\n");
    irq_restore(state);
    return false;
  }

  netif_set_default(&net_interface);
  netif_set_status_callback(&net_interface, net_interface_status);
  netif_set_link_up(&net_interface);
  netif_set_up(&net_interface);
  net_started = true;
  if (dhcp_start(&net_interface) != ERR_OK) {
    logk("network: DHCP start failed\n");
    net_started = false;
    netif_remove(&net_interface);
    irq_restore(state);
    return false;
  }
  irq_restore(state);

  logk("network: %s ready\n", driver);
  return true;
}

void net_stack_tick(void) {
  if (!net_started) {
    return;
  }
  irq_state_t state = irq_save();
  sys_check_timeouts();
  PBUF_CHECK_FREE_OOSEQ();
  irq_restore(state);
}

uint32_t net_stack_ip(void) {
  if (!net_started) {
    return 0;
  }
  irq_state_t state = irq_save();
  uint32_t address =
      lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&net_interface)));
  irq_restore(state);
  return address;
}

int net_socket_open(uint32_t owner_group, uint8_t protocol) {
  if (!net_started || (protocol != NET_PROTOCOL_TCP && protocol != NET_PROTOCOL_UDP)) {
    return -1;
  }

  irq_state_t state = irq_save();
  for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
    net_socket_t *socket = &net_sockets[i];
    if (socket->state != NET_SOCKET_FREE) {
      continue;
    }

    uint32_t generation = socket->generation + 1;
    if (generation == 0 || generation > NET_HANDLE_GENERATION_MAX) {
      generation = 1;
    }
    memset(socket, 0, sizeof(*socket));
    socket->generation = generation;
    socket->owner_group = owner_group;
    socket->protocol = protocol;
    if (protocol == NET_PROTOCOL_UDP) {
      socket->pcb.udp = udp_new_ip_type(IPADDR_TYPE_V4);
      if (socket->pcb.udp != NULL) {
        socket->state = NET_SOCKET_UDP;
        udp_recv(socket->pcb.udp, net_udp_receive, socket);
      }
    } else {
      socket->pcb.tcp = tcp_new_ip_type(IPADDR_TYPE_V4);
      if (socket->pcb.tcp != NULL) {
        socket->state = NET_SOCKET_TCP_READY;
        net_tcp_attach_callbacks(socket, socket->pcb.tcp);
      }
    }
    if (socket->state != NET_SOCKET_FREE) {
      int handle = net_socket_handle(i, generation);
      irq_restore(state);
      return handle;
    }
    socket->owner_group = 0;
    socket->protocol = 0;
  }
  irq_restore(state);
  return -1;
}

int net_socket_close(uint32_t owner_group, int handle) {
  irq_state_t state = irq_save();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return -1;
  }
  net_socket_dispose(socket);
  irq_restore(state);
  return 0;
}

int net_socket_configure(uint32_t owner_group, int handle,
                         uint32_t remote_ip, uint16_t remote_port,
                         uint32_t local_ip, uint16_t local_port) {
  irq_state_t state = irq_save();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->configured ||
      (socket->state != NET_SOCKET_UDP && socket->state != NET_SOCKET_TCP_READY)) {
    irq_restore(state);
    return -1;
  }
  if (socket->protocol == NET_PROTOCOL_UDP &&
      ((remote_ip == 0) != (remote_port == 0))) {
    irq_restore(state);
    return -1;
  }

  socket->remote_ip = remote_ip;
  socket->remote_port = remote_port;
  socket->local_ip = local_ip;
  socket->local_port = local_port;
  if (socket->protocol == NET_PROTOCOL_UDP) {
    ip_addr_t local;
    net_ip_from_u32(&local, local_ip);
    if (udp_bind(socket->pcb.udp, &local, local_port) != ERR_OK) {
      irq_restore(state);
      return -1;
    }
    if (remote_ip != 0) {
      ip_addr_t remote;
      net_ip_from_u32(&remote, remote_ip);
      if (udp_connect(socket->pcb.udp, &remote, remote_port) != ERR_OK) {
        irq_restore(state);
        return -1;
      }
    }
  }
  socket->configured = true;
  irq_restore(state);
  return 0;
}

int net_socket_send(uint32_t owner_group, int handle, const void *data,
                    uint32_t length) {
  if (data == NULL || length > 0xffffu) {
    return -1;
  }
  if (length == 0) {
    return 0;
  }

  uint32_t sent = 0;
  for (;;) {
    irq_state_t state = irq_save();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL || !socket->configured) {
      irq_restore(state);
      return -1;
    }

    if (socket->protocol == NET_PROTOCOL_UDP) {
      struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
      if (packet == NULL || pbuf_take(packet, data, (u16_t)length) != ERR_OK) {
        if (packet != NULL) {
          pbuf_free(packet);
        }
        irq_restore(state);
        return -1;
      }
      err_t error = udp_send(socket->pcb.udp, packet);
      pbuf_free(packet);
      irq_restore(state);
      return error == ERR_OK ? (int)length : -1;
    }

    if (socket->state != NET_SOCKET_TCP_CONNECTED || socket->pcb.tcp == NULL) {
      irq_restore(state);
      return -1;
    }
    uint16_t available = tcp_sndbuf(socket->pcb.tcp);
    if (available != 0) {
      uint16_t part = (uint16_t)((length - sent) < available ? length - sent
                                                              : available);
      err_t error = tcp_write(socket->pcb.tcp, (const uint8_t *)data + sent,
                              part, TCP_WRITE_FLAG_COPY);
      if (error == ERR_OK) {
        tcp_output(socket->pcb.tcp);
        sent += part;
      }
      irq_restore(state);
      if (error == ERR_OK && sent == length) {
        return (int)sent;
      }
      if (error != ERR_OK && error != ERR_MEM) {
        return -1;
      }
    } else {
      irq_restore(state);
    }
    net_yield();
  }
}

int net_socket_recv(uint32_t owner_group, int handle, void *data,
                    uint32_t capacity) {
  if (data == NULL || capacity == 0) {
    return -1;
  }

  for (;;) {
    irq_state_t state = irq_save();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return -1;
    }

    net_rx_item_t *item = socket->receive_head;
    if (item != NULL) {
      socket->receive_head = item->next;
      if (socket->receive_head == NULL) {
        socket->receive_tail = NULL;
      }
      uint16_t total = item->packet->tot_len;
      uint16_t copied = total < capacity ? total : (uint16_t)capacity;
      pbuf_copy_partial(item->packet, data, copied, 0);
      if (socket->protocol == NET_PROTOCOL_TCP && socket->pcb.tcp != NULL) {
        tcp_recved(socket->pcb.tcp, total);
      }
      pbuf_free(item->packet);
      item->packet = NULL;
      item->next = net_rx_free;
      net_rx_free = item;
      irq_restore(state);
      return copied;
    }

    if (socket->protocol == NET_PROTOCOL_TCP &&
        (socket->peer_closed || socket->state == NET_SOCKET_TCP_FAILED)) {
      irq_restore(state);
      return socket->state == NET_SOCKET_TCP_FAILED ? -1 : 0;
    }
    irq_restore(state);
    net_yield();
  }
}

int net_socket_connect(uint32_t owner_group, int handle) {
  irq_state_t state = irq_save();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->protocol != NET_PROTOCOL_TCP ||
      socket->state != NET_SOCKET_TCP_READY || !socket->configured ||
      socket->remote_ip == 0 || socket->remote_port == 0 ||
      !net_tcp_bind(socket)) {
    irq_restore(state);
    return -1;
  }

  ip_addr_t remote;
  net_ip_from_u32(&remote, socket->remote_ip);
  if (tcp_connect(socket->pcb.tcp, &remote, socket->remote_port,
                  net_tcp_connected) != ERR_OK) {
    irq_restore(state);
    return -1;
  }
  socket->state = NET_SOCKET_TCP_CONNECTING;
  irq_restore(state);

  uint32_t started = timerctl.count;
  for (;;) {
    state = irq_save();
    socket = net_socket_find(owner_group, handle);
    if (socket == NULL || socket->state == NET_SOCKET_TCP_FAILED) {
      irq_restore(state);
      return -1;
    }
    if (socket->state == NET_SOCKET_TCP_CONNECTED) {
      irq_restore(state);
      return 0;
    }
    if (timerctl.count - started >= NET_CONNECT_TIMEOUT_TICKS) {
      net_tcp_clear_callbacks(socket->pcb.tcp);
      tcp_abort(socket->pcb.tcp);
      socket->pcb.tcp = NULL;
      socket->state = NET_SOCKET_TCP_FAILED;
      irq_restore(state);
      return -1;
    }
    irq_restore(state);
    net_yield();
  }
}

int net_socket_listen(uint32_t owner_group, int handle) {
  irq_state_t state = irq_save();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->protocol != NET_PROTOCOL_TCP ||
      socket->state != NET_SOCKET_TCP_READY || !socket->configured ||
      !net_tcp_bind(socket)) {
    irq_restore(state);
    return -1;
  }

  err_t error;
  struct tcp_pcb *listener =
      tcp_listen_with_backlog_and_err(socket->pcb.tcp, 1, &error);
  if (listener == NULL || error != ERR_OK) {
    socket->state = NET_SOCKET_TCP_FAILED;
    irq_restore(state);
    return -1;
  }
  socket->pcb.tcp = listener;
  socket->state = NET_SOCKET_TCP_LISTENING;
  tcp_arg(listener, socket);
  tcp_accept(listener, net_tcp_accept);
  irq_restore(state);

  uint32_t started = timerctl.count;
  for (;;) {
    state = irq_save();
    socket = net_socket_find(owner_group, handle);
    if (socket == NULL || socket->state == NET_SOCKET_TCP_FAILED) {
      irq_restore(state);
      return -1;
    }
    if (socket->state == NET_SOCKET_TCP_CONNECTED) {
      irq_restore(state);
      return 0;
    }
    if (timerctl.count - started >= NET_CONNECT_TIMEOUT_TICKS) {
      tcp_arg(socket->pcb.tcp, NULL);
      tcp_accept(socket->pcb.tcp, NULL);
      tcp_abort(socket->pcb.tcp);
      socket->pcb.tcp = NULL;
      socket->state = NET_SOCKET_TCP_FAILED;
      irq_restore(state);
      return -1;
    }
    irq_restore(state);
    net_yield();
  }
}

static u8_t net_ping_receive(void *arg, struct raw_pcb *pcb,
                             struct pbuf *packet, const ip_addr_t *address) {
  (void)pcb;
  (void)address;
  net_ping_t *request = (net_ping_t *)arg;
  struct icmp_echo_hdr reply;
  uint16_t header_length = ip_current_header_tot_len();
  if (request != NULL && packet->tot_len >= header_length + sizeof(reply) &&
      pbuf_copy_partial(packet, &reply, sizeof(reply), header_length) ==
          sizeof(reply) &&
      ICMPH_TYPE(&reply) == ICMP_ER && ICMPH_CODE(&reply) == 0 &&
      reply.id == lwip_htons(request->id) &&
      reply.seqno == lwip_htons(request->sequence)) {
    request->reply = true;
    pbuf_free(packet);
    return 1;
  }
  return 0;
}

int net_stack_ping(uint32_t address) {
  if (!net_started || address == 0) {
    return -1;
  }

  irq_state_t state = irq_save();
  if (net_ping.active) {
    irq_restore(state);
    return -1;
  }
  memset(&net_ping, 0, sizeof(net_ping));
  net_ping.id = (uint16_t)rand();
  net_ping.sequence = 1;
  net_ping.pcb = raw_new_ip_type(IPADDR_TYPE_V4, IP_PROTO_ICMP);
  if (net_ping.pcb == NULL) {
    irq_restore(state);
    return -1;
  }
  raw_recv(net_ping.pcb, net_ping_receive, &net_ping);
  net_ping.active = true;

  uint8_t payload[sizeof(struct icmp_echo_hdr) + NET_PING_DATA_BYTES];
  memset(payload, 0xa5, sizeof(payload));
  struct icmp_echo_hdr *header = (struct icmp_echo_hdr *)payload;
  ICMPH_TYPE_SET(header, ICMP_ECHO);
  ICMPH_CODE_SET(header, 0);
  header->id = lwip_htons(net_ping.id);
  header->seqno = lwip_htons(net_ping.sequence);
  header->chksum = 0;
  header->chksum = inet_chksum(payload, sizeof(payload));

  struct pbuf *packet =
      pbuf_alloc(PBUF_IP, sizeof(payload), PBUF_RAM);
  ip_addr_t destination;
  net_ip_from_u32(&destination, address);
  err_t error = packet == NULL ? ERR_MEM : pbuf_take(packet, payload, sizeof(payload));
  if (error == ERR_OK) {
    error = raw_sendto(net_ping.pcb, packet, &destination);
  }
  if (packet != NULL) {
    pbuf_free(packet);
  }
  if (error != ERR_OK) {
    raw_remove(net_ping.pcb);
    net_ping.pcb = NULL;
    net_ping.active = false;
    irq_restore(state);
    return -1;
  }
  irq_restore(state);

  uint32_t started = timerctl.count;
  while (timerctl.count - started < NET_PING_TIMEOUT_TICKS) {
    state = irq_save();
    bool reply = net_ping.reply;
    irq_restore(state);
    if (reply) {
      break;
    }
    net_yield();
  }

  state = irq_save();
  bool reply = net_ping.reply;
  raw_remove(net_ping.pcb);
  net_ping.pcb = NULL;
  net_ping.active = false;
  irq_restore(state);
  return reply ? 0 : -1;
}

void net_task_cleanup(uint32_t owner_group) {
  irq_state_t state = irq_save();
  for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
    if (net_sockets[i].state != NET_SOCKET_FREE &&
        net_sockets[i].owner_group == owner_group) {
      net_socket_dispose(&net_sockets[i]);
    }
  }
  irq_restore(state);
}
