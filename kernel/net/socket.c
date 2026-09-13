#include <dos.h>
#include <fcntl.h>
#include <irq.h>
#include <stdint.h>

#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/raw.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>

#define NET_SOCKET_CAPACITY 32u
#define NET_SOCKET_PACKET_CAPACITY 64u
#define NET_SOCKET_INDEX_BITS 6u
#define NET_SOCKET_INDEX_MASK ((1u << NET_SOCKET_INDEX_BITS) - 1u)
#define NET_SOCKET_GENERATION_MAX 0x00ffffffu
#define NET_SOCKET_SLOT_NONE 0xffu
#define NET_SOCKET_TASK_NONE ((uint32_t)-1)
#define NET_SOCKET_RX_LIMIT (64u * 1024u)
#define NET_SOCKET_MAX_PAYLOAD 65535u
#define NET_SOCKET_MAX_BACKLOG 8u
#define NET_SOCKET_CONNECT_TIMEOUT_TICKS 1000u
#define NET_SOCKET_DNS_REQUEST_CAPACITY 4u
#define NET_SOCKET_DNS_NAME_MAX 255u

#define NET_SOCKET_OPTION_DEBUG (1u << 0)
#define NET_SOCKET_OPTION_REUSEADDR (1u << 1)
#define NET_SOCKET_OPTION_KEEPALIVE (1u << 2)
#define NET_SOCKET_OPTION_DONTROUTE (1u << 3)
#define NET_SOCKET_OPTION_BROADCAST (1u << 4)
#define NET_SOCKET_OPTION_OOBINLINE (1u << 5)
#define NET_SOCKET_OPTION_REUSEPORT (1u << 6)

typedef enum {
  NET_SOCKET_FREE,
  NET_SOCKET_INET_UDP,
  NET_SOCKET_INET_RAW,
  NET_SOCKET_INET_TCP_READY,
  NET_SOCKET_INET_TCP_CONNECTING,
  NET_SOCKET_INET_TCP_CONNECTED,
  NET_SOCKET_INET_TCP_LISTENING,
  NET_SOCKET_INET_TCP_FAILED,
  NET_SOCKET_LOCAL_READY,
  NET_SOCKET_LOCAL_CONNECTED,
  NET_SOCKET_LOCAL_LISTENING,
} net_socket_state_t;

typedef struct {
  uint32_t tid;
  uint32_t generation;
  uint32_t deadline;
} net_socket_waiter_t;

typedef struct net_socket_packet {
  struct net_socket_packet *next;
  struct pbuf *pbuf;
  void *local_data;
  uint32_t total;
  uint32_t offset;
  net_socket_address_t source;
} net_socket_packet_t;

typedef struct {
  uint32_t owner_group;
  uint32_t generation;
  uint32_t queued_bytes;
  uint32_t receive_timeout_ticks;
  uint32_t send_timeout_ticks;
  uint32_t receive_buffer_size;
  uint32_t send_buffer_size;
  uint32_t option_flags;
  uint32_t last_error;
  uint32_t peer_generation;
  uint32_t pending_parent_generation;
  uint16_t pending_count;
  uint8_t domain;
  uint8_t type;
  uint8_t protocol;
  uint8_t state;
  uint8_t peer_slot;
  uint8_t pending_head;
  uint8_t pending_tail;
  uint8_t pending_next;
  uint8_t pending_parent;
  uint8_t backlog;
  bool bound;
  bool peer_closed;
  bool nonblocking;
  bool shutdown_read;
  bool shutdown_write;
  bool linger_enabled;
  int32_t linger_seconds;
  uint8_t ip_tos;
  uint8_t multicast_ttl;
  bool multicast_loop;
  net_socket_address_t local;
  net_socket_address_t peer;
  union {
    struct udp_pcb *udp;
    struct tcp_pcb *tcp;
    struct raw_pcb *raw;
  } pcb;
  net_socket_packet_t *receive_head;
  net_socket_packet_t *receive_tail;
  net_socket_waiter_t reader;
  net_socket_waiter_t writer;
  net_socket_waiter_t connector;
  net_socket_waiter_t acceptor;
} net_socket_t;

typedef enum {
  NET_DNS_FREE,
  NET_DNS_PENDING,
  NET_DNS_COMPLETE,
  NET_DNS_FAILED,
} net_dns_state_t;

typedef struct {
  net_dns_state_t state;
  bool abandoned;
  uint32_t owner_tid;
  uint32_t owner_generation;
  uint32_t address;
  char name[NET_SOCKET_DNS_NAME_MAX + 1];
  net_socket_waiter_t waiter;
} net_dns_request_t;

static net_socket_t net_sockets[NET_SOCKET_CAPACITY];
static net_socket_packet_t net_packets[NET_SOCKET_PACKET_CAPACITY];
static net_socket_packet_t *net_packet_free;
static net_dns_request_t net_dns_requests[NET_SOCKET_DNS_REQUEST_CAPACITY];
static bool net_socket_initialized;
static bool net_socket_input_active;
static bool net_socket_input_woke_task;

void net_socket_input_begin(void) {
  net_socket_input_active = true;
  net_socket_input_woke_task = false;
}

bool net_socket_input_end(void) {
  bool woke_task = net_socket_input_woke_task;
  net_socket_input_active = false;
  net_socket_input_woke_task = false;
  return woke_task;
}

static bool net_socket_deadline_passed(uint32_t deadline) {
  return deadline != 0 && (int32_t)(timerctl.count - deadline) >= 0;
}

static uint32_t net_socket_deadline_after(uint32_t ticks) {
  uint32_t deadline = timerctl.count + ticks;
  return deadline == 0 ? 1 : deadline;
}

static void net_socket_waiter_init(net_socket_waiter_t *waiter) {
  waiter->tid = NET_SOCKET_TASK_NONE;
  waiter->generation = 0;
  waiter->deadline = 0;
}

static bool net_socket_waiter_matches(const net_socket_waiter_t *waiter,
                                      const mtask *task) {
  return waiter->tid == task->tid && waiter->generation == task->generation;
}

static void net_socket_waiter_wake(net_socket_waiter_t *waiter) {
  if (waiter->tid == NET_SOCKET_TASK_NONE) {
    return;
  }

  mtask *task = get_task(waiter->tid);
  uint32_t generation = waiter->generation;
  net_socket_waiter_init(waiter);
  if (task != NULL && task->generation == generation) {
    task_run(task);
    if (net_socket_input_active && task->ready) {
      mtask_run_now(task);
      net_socket_input_woke_task = true;
    }
  }
}

static void net_socket_waiter_cancel(net_socket_waiter_t *waiter,
                                     uint32_t tid, uint32_t generation) {
  if (waiter->tid == tid && waiter->generation == generation) {
    net_socket_waiter_init(waiter);
  }
}

/* The caller holds the interrupt state saved in state.  Publishing the
 * waiter and switching with IRQs disabled closes the lost-wakeup window. */
static int net_socket_wait(net_socket_waiter_t *waiter, uint32_t deadline,
                           irq_state_t state) {
  mtask *self = current_task();
  if (waiter->tid != NET_SOCKET_TASK_NONE &&
      !net_socket_waiter_matches(waiter, self)) {
    irq_restore(state);
    return NET_SOCKET_ERR_BUSY;
  }
  if (net_socket_deadline_passed(deadline)) {
    irq_restore(state);
    return NET_SOCKET_ERR_TIMEDOUT;
  }
  if (self->ready) {
    self->ready = 0;
    irq_restore(state);
    return 0;
  }

  waiter->tid = self->tid;
  waiter->generation = self->generation;
  waiter->deadline = deadline;
  task_fall_blocked_reason(WAITING, WAIT_REASON_SOCKET);

  if (net_socket_waiter_matches(waiter, self)) {
    net_socket_waiter_init(waiter);
  }
  if (self->wait_reason == WAIT_REASON_SOCKET) {
    self->wait_reason = WAIT_REASON_NONE;
  }
  irq_restore(state);
  return 0;
}

static void net_socket_system_init(void) {
  if (net_socket_initialized) {
    return;
  }

  net_packet_free = NULL;
  for (unsigned i = 0; i < NET_SOCKET_PACKET_CAPACITY; i++) {
    net_packets[i].next = net_packet_free;
    net_packet_free = &net_packets[i];
  }
  net_socket_initialized = true;
}

static net_socket_packet_t *net_socket_packet_alloc(void) {
  if (net_packet_free == NULL) {
    return NULL;
  }
  net_socket_packet_t *packet = net_packet_free;
  net_packet_free = packet->next;
  memset(packet, 0, sizeof(*packet));
  return packet;
}

static void net_socket_packet_free(net_socket_packet_t *packet) {
  if (packet->pbuf != NULL) {
    pbuf_free(packet->pbuf);
  }
  if (packet->local_data != NULL) {
    free(packet->local_data);
  }
  memset(packet, 0, sizeof(*packet));
  packet->next = net_packet_free;
  net_packet_free = packet;
}

static void net_socket_address_init(net_socket_address_t *address,
                                    uint16_t family) {
  memset(address, 0, sizeof(*address));
  address->family = family;
}

static bool net_socket_address_equal(const net_socket_address_t *left,
                                     const net_socket_address_t *right) {
  if (left->family != right->family) {
    return false;
  }
  if (left->family == NET_SOCKET_AF_INET) {
    return left->value.inet.address == right->value.inet.address &&
           left->value.inet.port == right->value.inet.port;
  }
  if (left->family == NET_SOCKET_AF_LOCAL) {
    return left->value.local.length == right->value.local.length &&
           memcmp(left->value.local.path, right->value.local.path,
                  left->value.local.length) == 0;
  }
  return false;
}

static void net_socket_address_from_ip(net_socket_address_t *address,
                                       const ip_addr_t *ip, uint16_t port) {
  net_socket_address_init(address, NET_SOCKET_AF_INET);
  address->value.inet.address = ip4_addr_get_u32(ip_2_ip4(ip));
  address->value.inet.port = port;
}

static void net_socket_ip_from_address(ip_addr_t *ip, uint32_t address) {
  ip_addr_set_ip4_u32(ip, address);
}

static int net_socket_lwip_error(err_t error) {
  if (error == ERR_MEM) {
    return NET_SOCKET_ERR_NOMEM;
  }
  if (error == ERR_USE) {
    return NET_SOCKET_ERR_ADDRINUSE;
  }
  if (error == ERR_TIMEOUT) {
    return NET_SOCKET_ERR_TIMEDOUT;
  }
  if (error == ERR_RTE || error == ERR_CONN || error == ERR_CLSD) {
    return NET_SOCKET_ERR_NOTCONN;
  }
  return NET_SOCKET_ERR_INVAL;
}

static int net_socket_handle(const net_socket_t *socket) {
  unsigned index = (unsigned)(socket - net_sockets);
  return (int)(NET_SOCKET_HANDLE_TAG |
               (socket->generation << NET_SOCKET_INDEX_BITS) | (index + 1));
}

static net_socket_t *net_socket_find(uint32_t owner_group, int handle) {
  uint32_t value = (uint32_t)handle;
  if (handle <= 0 || (value & NET_SOCKET_HANDLE_TAG_MASK) !=
                         NET_SOCKET_HANDLE_TAG) {
    return NULL;
  }
  uint32_t slot = value & NET_SOCKET_INDEX_MASK;
  uint32_t generation = (value & ~NET_SOCKET_HANDLE_TAG) >>
                        NET_SOCKET_INDEX_BITS;
  if (slot == 0 || slot > NET_SOCKET_CAPACITY || generation == 0) {
    return NULL;
  }

  net_socket_t *socket = &net_sockets[slot - 1];
  if (socket->state == NET_SOCKET_FREE || socket->owner_group != owner_group ||
      socket->generation != generation) {
    return NULL;
  }
  return socket;
}

static net_socket_t *net_socket_from_slot(uint8_t slot, uint32_t generation) {
  if (slot == NET_SOCKET_SLOT_NONE || slot >= NET_SOCKET_CAPACITY) {
    return NULL;
  }
  net_socket_t *socket = &net_sockets[slot];
  if (socket->state == NET_SOCKET_FREE || socket->generation != generation) {
    return NULL;
  }
  return socket;
}

static net_socket_t *net_socket_local_peer(net_socket_t *socket) {
  return net_socket_from_slot(socket->peer_slot, socket->peer_generation);
}

static void net_socket_waiters_init(net_socket_t *socket) {
  net_socket_waiter_init(&socket->reader);
  net_socket_waiter_init(&socket->writer);
  net_socket_waiter_init(&socket->connector);
  net_socket_waiter_init(&socket->acceptor);
}

static void net_socket_wake_all(net_socket_t *socket) {
  net_socket_waiter_wake(&socket->reader);
  net_socket_waiter_wake(&socket->writer);
  net_socket_waiter_wake(&socket->connector);
  net_socket_waiter_wake(&socket->acceptor);
}

static net_socket_t *net_socket_reserve(uint32_t owner_group) {
  for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
    net_socket_t *socket = &net_sockets[i];
    if (socket->state != NET_SOCKET_FREE) {
      continue;
    }

    uint32_t generation = socket->generation + 1;
    if (generation == 0 || generation > NET_SOCKET_GENERATION_MAX) {
      generation = 1;
    }
    memset(socket, 0, sizeof(*socket));
    socket->generation = generation;
    socket->owner_group = owner_group;
    socket->peer_slot = NET_SOCKET_SLOT_NONE;
    socket->pending_head = NET_SOCKET_SLOT_NONE;
    socket->pending_tail = NET_SOCKET_SLOT_NONE;
    socket->pending_next = NET_SOCKET_SLOT_NONE;
    socket->pending_parent = NET_SOCKET_SLOT_NONE;
    socket->receive_buffer_size = NET_SOCKET_RX_LIMIT;
    socket->send_buffer_size = NET_SOCKET_RX_LIMIT;
    socket->multicast_ttl = 1;
    socket->multicast_loop = true;
    net_socket_waiters_init(socket);
    return socket;
  }
  return NULL;
}

static bool net_socket_queue_has_room(const net_socket_t *socket,
                                      uint32_t length) {
  return length <= NET_SOCKET_RX_LIMIT &&
         socket->queued_bytes <= NET_SOCKET_RX_LIMIT - length;
}

static void net_socket_packet_append(net_socket_t *socket,
                                     net_socket_packet_t *packet) {
  packet->next = NULL;
  if (socket->receive_tail != NULL) {
    socket->receive_tail->next = packet;
  } else {
    socket->receive_head = packet;
  }
  socket->receive_tail = packet;
  socket->queued_bytes += packet->total;
  net_socket_waiter_wake(&socket->reader);
}

/* Takes ownership only after the packet is queued.  TCP uses ERR_MEM to ask
 * lwIP to retain and retry a packet that could not enter this queue. */
static bool net_socket_queue_owned_pbuf(net_socket_t *socket,
                                        struct pbuf *packet,
                                        const net_socket_address_t *source) {
  uint32_t length = packet->tot_len;
  if (!net_socket_queue_has_room(socket, length)) {
    return false;
  }

  net_socket_packet_t *item = net_socket_packet_alloc();
  if (item == NULL) {
    return false;
  }
  item->pbuf = packet;
  item->total = length;
  item->source = *source;
  net_socket_packet_append(socket, item);
  return true;
}

/* lwIP continues processing a RAW packet after this callback and advances its
 * payload pointer.  Queue an owned clone so SOCK_RAW consistently exposes the
 * complete IPv4 packet without interfering with ICMP handling. */
static bool net_socket_queue_raw_packet(net_socket_t *socket,
                                        struct pbuf *packet,
                                        const net_socket_address_t *source) {
  struct pbuf *copy = pbuf_clone(PBUF_RAW, PBUF_RAM, packet);
  if (copy == NULL) {
    return false;
  }
  if (net_socket_queue_owned_pbuf(socket, copy, source)) {
    return true;
  }
  pbuf_free(copy);
  return false;
}

/* Takes ownership of data on success only. */
static bool net_socket_queue_local(net_socket_t *socket, void *data,
                                   uint32_t length,
                                   const net_socket_address_t *source) {
  if (!net_socket_queue_has_room(socket, length)) {
    return false;
  }

  net_socket_packet_t *item = net_socket_packet_alloc();
  if (item == NULL) {
    return false;
  }
  item->local_data = data;
  item->total = length;
  item->source = *source;
  net_socket_packet_append(socket, item);
  return true;
}

static void net_socket_clear_packets(net_socket_t *socket) {
  net_socket_packet_t *packet = socket->receive_head;
  while (packet != NULL) {
    net_socket_packet_t *next = packet->next;
    net_socket_packet_free(packet);
    packet = next;
  }
  socket->receive_head = NULL;
  socket->receive_tail = NULL;
  socket->queued_bytes = 0;
}

static void net_socket_pending_push(net_socket_t *listener,
                                    net_socket_t *child) {
  uint8_t child_slot = (uint8_t)(child - net_sockets);
  child->pending_next = NET_SOCKET_SLOT_NONE;
  child->pending_parent = (uint8_t)(listener - net_sockets);
  child->pending_parent_generation = listener->generation;
  if (listener->pending_tail != NET_SOCKET_SLOT_NONE) {
    net_sockets[listener->pending_tail].pending_next = child_slot;
  } else {
    listener->pending_head = child_slot;
  }
  listener->pending_tail = child_slot;
  listener->pending_count++;
  net_socket_waiter_wake(&listener->acceptor);
}

static net_socket_t *net_socket_pending_pop(net_socket_t *listener) {
  uint8_t slot = listener->pending_head;
  if (slot == NET_SOCKET_SLOT_NONE || slot >= NET_SOCKET_CAPACITY) {
    return NULL;
  }

  net_socket_t *child = &net_sockets[slot];
  listener->pending_head = child->pending_next;
  if (listener->pending_head == NET_SOCKET_SLOT_NONE) {
    listener->pending_tail = NET_SOCKET_SLOT_NONE;
  }
  if (listener->pending_count != 0) {
    listener->pending_count--;
  }
  child->pending_next = NET_SOCKET_SLOT_NONE;
  child->pending_parent = NET_SOCKET_SLOT_NONE;
  child->pending_parent_generation = 0;
  return child;
}

static void net_socket_pending_remove(net_socket_t *listener,
                                      net_socket_t *child) {
  uint8_t wanted = (uint8_t)(child - net_sockets);
  uint8_t previous = NET_SOCKET_SLOT_NONE;
  uint8_t slot = listener->pending_head;
  while (slot != NET_SOCKET_SLOT_NONE && slot < NET_SOCKET_CAPACITY) {
    net_socket_t *candidate = &net_sockets[slot];
    if (slot == wanted) {
      if (previous == NET_SOCKET_SLOT_NONE) {
        listener->pending_head = candidate->pending_next;
      } else {
        net_sockets[previous].pending_next = candidate->pending_next;
      }
      if (listener->pending_tail == slot) {
        listener->pending_tail = previous;
      }
      if (listener->pending_count != 0) {
        listener->pending_count--;
      }
      candidate->pending_next = NET_SOCKET_SLOT_NONE;
      candidate->pending_parent = NET_SOCKET_SLOT_NONE;
      candidate->pending_parent_generation = 0;
      return;
    }
    previous = slot;
    slot = candidate->pending_next;
  }
}

static net_socket_t *net_socket_find_local_bound(
    const net_socket_address_t *address, uint8_t type) {
  for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
    net_socket_t *socket = &net_sockets[i];
    if (socket->state == NET_SOCKET_FREE || socket->domain != NET_SOCKET_AF_LOCAL ||
        socket->type != type || !socket->bound ||
        !net_socket_address_equal(&socket->local, address)) {
      continue;
    }
    return socket;
  }
  return NULL;
}

static void net_socket_tcp_clear_callbacks(struct tcp_pcb *pcb) {
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_err(pcb, NULL);
}

static void net_socket_tcp_refresh_local(net_socket_t *socket) {
  if (socket->pcb.tcp == NULL) {
    return;
  }
  net_socket_address_from_ip(&socket->local, &socket->pcb.tcp->local_ip,
                             lwip_htons(socket->pcb.tcp->local_port));
}

static void net_socket_udp_refresh_local(net_socket_t *socket) {
  if (socket->pcb.udp == NULL) {
    return;
  }
  net_socket_address_from_ip(&socket->local, &socket->pcb.udp->local_ip,
                             lwip_htons(socket->pcb.udp->local_port));
}

static void net_socket_raw_refresh_local(net_socket_t *socket) {
  if (socket->pcb.raw == NULL) {
    return;
  }
  net_socket_address_from_ip(&socket->local, &socket->pcb.raw->local_ip, 0);
}

static void net_socket_refresh_local(net_socket_t *socket) {
  if (socket->domain != NET_SOCKET_AF_INET) {
    return;
  }
  if (socket->protocol == NET_SOCKET_PROTOCOL_UDP) {
    net_socket_udp_refresh_local(socket);
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_TCP) {
    net_socket_tcp_refresh_local(socket);
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_ICMP) {
    net_socket_raw_refresh_local(socket);
  }
}

static void net_socket_tcp_attach_callbacks(net_socket_t *socket,
                                            struct tcp_pcb *pcb);

static err_t net_socket_tcp_receive(void *arg, struct tcp_pcb *pcb,
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
    net_socket_waiter_wake(&socket->reader);
    net_socket_waiter_wake(&socket->writer);
    net_socket_waiter_wake(&socket->connector);
    return ERR_OK;
  }
  if (socket->shutdown_read) {
    u16_t length = packet->tot_len;
    pbuf_free(packet);
    tcp_recved(pcb, length);
    return ERR_OK;
  }
  if (error != ERR_OK) {
    pbuf_free(packet);
    socket->state = NET_SOCKET_INET_TCP_FAILED;
    socket->last_error = 104; /* ECONNRESET */
    net_socket_wake_all(socket);
    return ERR_OK;
  }

  net_socket_address_t source = socket->peer;
  if (!net_socket_queue_owned_pbuf(socket, packet, &source)) {
    return ERR_MEM;
  }
  (void)pcb;
  return ERR_OK;
}

static err_t net_socket_tcp_sent(void *arg, struct tcp_pcb *pcb, u16_t length) {
  (void)pcb;
  (void)length;
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket != NULL) {
    net_socket_waiter_wake(&socket->writer);
  }
  return ERR_OK;
}

static void net_socket_tcp_error(void *arg, err_t error) {
  (void)error;
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL) {
    return;
  }
  socket->pcb.tcp = NULL;
  socket->state = NET_SOCKET_INET_TCP_FAILED;
  socket->peer_closed = true;
  socket->last_error = 104; /* ECONNRESET */
  net_socket_wake_all(socket);
}

static err_t net_socket_tcp_connected(void *arg, struct tcp_pcb *pcb,
                                      err_t error) {
  net_socket_t *socket = (net_socket_t *)arg;
  if (socket == NULL || error != ERR_OK) {
    if (socket != NULL) {
      socket->state = NET_SOCKET_INET_TCP_FAILED;
      socket->last_error = 111; /* ECONNREFUSED */
      net_socket_wake_all(socket);
    }
    return ERR_OK;
  }
  socket->pcb.tcp = pcb;
  socket->state = NET_SOCKET_INET_TCP_CONNECTED;
  socket->last_error = 0;
  net_socket_tcp_refresh_local(socket);
  net_socket_waiter_wake(&socket->connector);
  return ERR_OK;
}

static err_t net_socket_tcp_accept(void *arg, struct tcp_pcb *pcb,
                                   err_t error) {
  net_socket_t *listener = (net_socket_t *)arg;
  if (listener == NULL || error != ERR_OK || pcb == NULL ||
      listener->state != NET_SOCKET_INET_TCP_LISTENING ||
      listener->pending_count >= listener->backlog) {
    if (pcb != NULL) {
      tcp_abort(pcb);
    }
    return ERR_ABRT;
  }

  net_socket_t *child = net_socket_reserve(listener->owner_group);
  if (child == NULL) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  child->domain = NET_SOCKET_AF_INET;
  child->type = NET_SOCKET_STREAM;
  child->protocol = NET_SOCKET_PROTOCOL_TCP;
  child->state = NET_SOCKET_INET_TCP_CONNECTED;
  child->receive_timeout_ticks = listener->receive_timeout_ticks;
  child->send_timeout_ticks = listener->send_timeout_ticks;
  child->option_flags = listener->option_flags;
  child->receive_buffer_size = listener->receive_buffer_size;
  child->send_buffer_size = listener->send_buffer_size;
  child->pcb.tcp = pcb;
  net_socket_address_from_ip(&child->local, &pcb->local_ip,
                             lwip_htons(pcb->local_port));
  net_socket_address_from_ip(&child->peer, &pcb->remote_ip,
                             lwip_htons(pcb->remote_port));
  net_socket_tcp_attach_callbacks(child, pcb);
  net_socket_pending_push(listener, child);
  tcp_backlog_accepted(pcb);
  return ERR_OK;
}

static void net_socket_tcp_attach_callbacks(net_socket_t *socket,
                                            struct tcp_pcb *pcb) {
  tcp_arg(pcb, socket);
  tcp_recv(pcb, net_socket_tcp_receive);
  tcp_sent(pcb, net_socket_tcp_sent);
  tcp_err(pcb, net_socket_tcp_error);
}

static void net_socket_udp_receive(void *arg, struct udp_pcb *pcb,
                                   struct pbuf *packet,
                                   const ip_addr_t *address, u16_t port) {
  (void)pcb;
  net_socket_t *socket = (net_socket_t *)arg;
  net_socket_address_t source;
  net_socket_address_from_ip(&source, address, lwip_htons(port));
  if (socket == NULL || !net_socket_queue_owned_pbuf(socket, packet, &source)) {
    pbuf_free(packet);
  }
}

static u8_t net_socket_raw_receive(void *arg, struct raw_pcb *pcb,
                                   struct pbuf *packet,
                                   const ip_addr_t *address) {
  (void)pcb;
  net_socket_t *socket = (net_socket_t *)arg;
  net_socket_address_t source;
  net_socket_address_from_ip(&source, address, 0);
  if (socket != NULL) {
    (void)net_socket_queue_raw_packet(socket, packet, &source);
  }
  return 0;
}

static void net_socket_tcp_abort(net_socket_t *socket) {
  if (socket->pcb.tcp != NULL) {
    net_socket_tcp_clear_callbacks(socket->pcb.tcp);
    tcp_abort(socket->pcb.tcp);
    socket->pcb.tcp = NULL;
  }
  socket->state = NET_SOCKET_INET_TCP_FAILED;
  socket->peer_closed = true;
  socket->last_error = 104; /* ECONNRESET */
  net_socket_wake_all(socket);
}

static void net_socket_dispose(net_socket_t *socket) {
  if (socket->state == NET_SOCKET_FREE) {
    return;
  }

  net_socket_wake_all(socket);
  net_socket_t *parent = net_socket_from_slot(socket->pending_parent,
                                               socket->pending_parent_generation);
  if (parent != NULL) {
    net_socket_pending_remove(parent, socket);
  }

  for (;;) {
    net_socket_t *child = net_socket_pending_pop(socket);
    if (child == NULL) {
      break;
    }
    net_socket_dispose(child);
  }

  net_socket_t *peer = net_socket_local_peer(socket);
  if (peer != NULL) {
    peer->peer_slot = NET_SOCKET_SLOT_NONE;
    peer->peer_generation = 0;
    peer->peer_closed = true;
    net_socket_waiter_wake(&peer->reader);
    net_socket_waiter_wake(&peer->writer);
    net_socket_waiter_wake(&peer->connector);
  }

  if (socket->protocol == NET_SOCKET_PROTOCOL_UDP && socket->pcb.udp != NULL) {
    udp_recv(socket->pcb.udp, NULL, NULL);
    udp_remove(socket->pcb.udp);
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_TCP &&
             socket->pcb.tcp != NULL) {
    if (socket->state == NET_SOCKET_INET_TCP_LISTENING) {
      tcp_arg(socket->pcb.tcp, NULL);
      tcp_accept(socket->pcb.tcp, NULL);
      if (tcp_close(socket->pcb.tcp) != ERR_OK) {
        tcp_abort(socket->pcb.tcp);
      }
    } else {
      net_socket_tcp_clear_callbacks(socket->pcb.tcp);
      tcp_abort(socket->pcb.tcp);
    }
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_ICMP &&
             socket->pcb.raw != NULL) {
    raw_recv(socket->pcb.raw, NULL, NULL);
    raw_remove(socket->pcb.raw);
  }

  net_socket_clear_packets(socket);
  uint32_t generation = socket->generation;
  memset(socket, 0, sizeof(*socket));
  socket->generation = generation;
  socket->state = NET_SOCKET_FREE;
}

static int net_socket_bind_inet(net_socket_t *socket,
                                const net_socket_address_t *address) {
  ip_addr_t local;
  net_socket_ip_from_address(&local, address->value.inet.address);
  err_t error = ERR_VAL;
  if (socket->protocol == NET_SOCKET_PROTOCOL_UDP) {
    error = udp_bind(socket->pcb.udp, &local, lwip_ntohs(address->value.inet.port));
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_TCP) {
    error = tcp_bind(socket->pcb.tcp, &local, lwip_ntohs(address->value.inet.port));
  } else if (socket->protocol == NET_SOCKET_PROTOCOL_ICMP &&
             address->value.inet.port == 0) {
    error = raw_bind(socket->pcb.raw, &local);
  }
  if (error != ERR_OK) {
    return net_socket_lwip_error(error);
  }
  socket->bound = true;
  socket->local = *address;
  net_socket_refresh_local(socket);
  return 0;
}

static int net_socket_bind_any(net_socket_t *socket) {
  if (socket->bound) {
    return 0;
  }
  net_socket_address_t address;
  net_socket_address_init(&address, NET_SOCKET_AF_INET);
  return net_socket_bind_inet(socket, &address);
}

int net_socket_create(uint32_t owner_group, int domain, int type,
                      int protocol) {
  if (domain == NET_SOCKET_AF_INET && !net_stack_ready()) {
    return NET_SOCKET_ERR_NETDOWN;
  }
  if (domain == NET_SOCKET_AF_LOCAL) {
    if ((type != NET_SOCKET_STREAM && type != NET_SOCKET_DGRAM) || protocol != 0) {
      return NET_SOCKET_ERR_PROTOCOL;
    }
  } else if (domain == NET_SOCKET_AF_INET) {
    if ((type == NET_SOCKET_STREAM &&
         protocol != 0 && protocol != NET_SOCKET_PROTOCOL_TCP) ||
        (type == NET_SOCKET_DGRAM &&
         protocol != 0 && protocol != NET_SOCKET_PROTOCOL_UDP) ||
        (type == NET_SOCKET_RAW && protocol != NET_SOCKET_PROTOCOL_ICMP) ||
        (type != NET_SOCKET_STREAM && type != NET_SOCKET_DGRAM &&
         type != NET_SOCKET_RAW)) {
      return NET_SOCKET_ERR_PROTOCOL;
    }
  } else {
    return NET_SOCKET_ERR_INVAL;
  }

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_reserve(owner_group);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOMEM;
  }

  socket->domain = (uint8_t)domain;
  socket->type = (uint8_t)type;
  socket->protocol = (uint8_t)protocol;
  net_socket_address_init(&socket->local, (uint16_t)domain);
  if (domain == NET_SOCKET_AF_LOCAL) {
    socket->state = NET_SOCKET_LOCAL_READY;
  } else if (type == NET_SOCKET_DGRAM) {
    socket->protocol = NET_SOCKET_PROTOCOL_UDP;
    socket->pcb.udp = udp_new_ip_type(IPADDR_TYPE_V4);
    socket->state = socket->pcb.udp == NULL ? NET_SOCKET_FREE
                                             : NET_SOCKET_INET_UDP;
    if (socket->pcb.udp != NULL) {
      udp_recv(socket->pcb.udp, net_socket_udp_receive, socket);
    }
  } else if (type == NET_SOCKET_STREAM) {
    socket->protocol = NET_SOCKET_PROTOCOL_TCP;
    socket->pcb.tcp = tcp_new_ip_type(IPADDR_TYPE_V4);
    socket->state = socket->pcb.tcp == NULL ? NET_SOCKET_FREE
                                             : NET_SOCKET_INET_TCP_READY;
    if (socket->pcb.tcp != NULL) {
      net_socket_tcp_attach_callbacks(socket, socket->pcb.tcp);
    }
  } else {
    socket->protocol = NET_SOCKET_PROTOCOL_ICMP;
    socket->pcb.raw = raw_new_ip_type(IPADDR_TYPE_V4, IP_PROTO_ICMP);
    socket->state = socket->pcb.raw == NULL ? NET_SOCKET_FREE
                                             : NET_SOCKET_INET_RAW;
    if (socket->pcb.raw != NULL) {
      raw_recv(socket->pcb.raw, net_socket_raw_receive, socket);
    }
  }

  if (socket->state == NET_SOCKET_FREE) {
    socket->owner_group = 0;
    socket->protocol = 0;
    irq_restore(state);
    return NET_SOCKET_ERR_NOMEM;
  }
  int handle = net_socket_handle(socket);
  irq_restore(state);
  return handle;
}

int net_socket_close(uint32_t owner_group, int handle) {
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  net_socket_dispose(socket);
  irq_restore(state);
  return 0;
}

int net_socket_bind(uint32_t owner_group, int handle,
                    const net_socket_address_t *address) {
  if (address == NULL) {
    return NET_SOCKET_ERR_INVAL;
  }

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->domain != address->family || socket->bound) {
    irq_restore(state);
    return socket == NULL ? NET_SOCKET_ERR_NOENT : NET_SOCKET_ERR_INVAL;
  }

  int result;
  if (socket->domain == NET_SOCKET_AF_LOCAL) {
    if (socket->state != NET_SOCKET_LOCAL_READY ||
        address->value.local.length == 0) {
      result = NET_SOCKET_ERR_INVAL;
    } else if (net_socket_find_local_bound(address, socket->type) != NULL) {
      result = NET_SOCKET_ERR_ADDRINUSE;
    } else {
      socket->local = *address;
      socket->bound = true;
      result = 0;
    }
  } else if ((socket->protocol == NET_SOCKET_PROTOCOL_UDP &&
              socket->state == NET_SOCKET_INET_UDP) ||
             (socket->protocol == NET_SOCKET_PROTOCOL_TCP &&
              socket->state == NET_SOCKET_INET_TCP_READY) ||
             (socket->protocol == NET_SOCKET_PROTOCOL_ICMP &&
              socket->state == NET_SOCKET_INET_RAW)) {
    result = net_socket_bind_inet(socket, address);
  } else {
    result = NET_SOCKET_ERR_INVAL;
  }
  irq_restore(state);
  return result;
}

static int net_socket_connect_local_dgram(net_socket_t *socket,
                                          const net_socket_address_t *address) {
  net_socket_t *target = net_socket_find_local_bound(address, NET_SOCKET_DGRAM);
  if (target == NULL) {
    return NET_SOCKET_ERR_NOENT;
  }
  socket->peer = *address;
  return 0;
}

static int net_socket_connect_local_stream(net_socket_t *socket,
                                           const net_socket_address_t *address) {
  net_socket_t *listener = net_socket_find_local_bound(address, NET_SOCKET_STREAM);
  if (listener == NULL || listener->state != NET_SOCKET_LOCAL_LISTENING) {
    return NET_SOCKET_ERR_NOENT;
  }
  if (listener->pending_count >= listener->backlog) {
    return NET_SOCKET_ERR_AGAIN;
  }

  net_socket_t *child = net_socket_reserve(listener->owner_group);
  if (child == NULL) {
    return NET_SOCKET_ERR_NOMEM;
  }
  child->domain = NET_SOCKET_AF_LOCAL;
  child->type = NET_SOCKET_STREAM;
  child->state = NET_SOCKET_LOCAL_CONNECTED;
  child->receive_timeout_ticks = listener->receive_timeout_ticks;
  child->send_timeout_ticks = listener->send_timeout_ticks;
  child->option_flags = listener->option_flags;
  child->receive_buffer_size = listener->receive_buffer_size;
  child->send_buffer_size = listener->send_buffer_size;
  child->local = listener->local;
  if (socket->bound) {
    child->peer = socket->local;
  } else {
    net_socket_address_init(&child->peer, NET_SOCKET_AF_LOCAL);
  }
  socket->peer = listener->local;
  socket->state = NET_SOCKET_LOCAL_CONNECTED;
  socket->peer_closed = false;
  child->peer_closed = false;
  socket->peer_slot = (uint8_t)(child - net_sockets);
  socket->peer_generation = child->generation;
  child->peer_slot = (uint8_t)(socket - net_sockets);
  child->peer_generation = socket->generation;
  net_socket_pending_push(listener, child);
  return 0;
}

int net_socket_connect(uint32_t owner_group, int handle,
                       const net_socket_address_t *address) {
  if (address == NULL) {
    return NET_SOCKET_ERR_INVAL;
  }

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->domain != address->family) {
    irq_restore(state);
    return socket == NULL ? NET_SOCKET_ERR_NOENT : NET_SOCKET_ERR_INVAL;
  }

  if (socket->domain == NET_SOCKET_AF_LOCAL) {
    int result;
    if (socket->type == NET_SOCKET_DGRAM &&
        socket->state == NET_SOCKET_LOCAL_READY) {
      result = net_socket_connect_local_dgram(socket, address);
    } else if (socket->type == NET_SOCKET_STREAM &&
               socket->state == NET_SOCKET_LOCAL_READY) {
      result = net_socket_connect_local_stream(socket, address);
    } else {
      result = NET_SOCKET_ERR_INVAL;
    }
    irq_restore(state);
    return result;
  }

  if (socket->protocol == NET_SOCKET_PROTOCOL_UDP &&
      socket->state == NET_SOCKET_INET_UDP) {
    int result = net_socket_bind_any(socket);
    if (result == 0) {
      ip_addr_t remote;
      net_socket_ip_from_address(&remote, address->value.inet.address);
      err_t error = udp_connect(socket->pcb.udp, &remote,
                                lwip_ntohs(address->value.inet.port));
      result = error == ERR_OK ? 0 : net_socket_lwip_error(error);
      if (result == 0) {
        socket->peer = *address;
      }
    }
    irq_restore(state);
    return result;
  }

  if (socket->protocol == NET_SOCKET_PROTOCOL_ICMP &&
      socket->state == NET_SOCKET_INET_RAW && address->value.inet.port == 0) {
    ip_addr_t remote;
    net_socket_ip_from_address(&remote, address->value.inet.address);
    err_t error = raw_connect(socket->pcb.raw, &remote);
    if (error == ERR_OK) {
      socket->peer = *address;
    }
    irq_restore(state);
    return error == ERR_OK ? 0 : net_socket_lwip_error(error);
  }

  if (socket->protocol != NET_SOCKET_PROTOCOL_TCP ||
      socket->state != NET_SOCKET_INET_TCP_READY) {
    irq_restore(state);
    return NET_SOCKET_ERR_INVAL;
  }
  int result = net_socket_bind_any(socket);
  if (result != 0) {
    irq_restore(state);
    return result;
  }
  ip_addr_t remote;
  net_socket_ip_from_address(&remote, address->value.inet.address);
  err_t error = tcp_connect(socket->pcb.tcp, &remote,
                            lwip_ntohs(address->value.inet.port),
                            net_socket_tcp_connected);
  if (error != ERR_OK) {
    irq_restore(state);
    return net_socket_lwip_error(error);
  }
  socket->peer = *address;
  socket->state = NET_SOCKET_INET_TCP_CONNECTING;
  socket->last_error = 115; /* EINPROGRESS */
  uint32_t deadline = timerctl.count + NET_SOCKET_CONNECT_TIMEOUT_TICKS;
  net_stack_poll_local(address->value.inet.address);
  irq_restore(state);

  if (socket->nonblocking) {
    return NET_SOCKET_ERR_INPROGRESS;
  }

  for (;;) {
    state = irq_save();
    socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    if (socket->state == NET_SOCKET_INET_TCP_CONNECTED) {
      irq_restore(state);
      return 0;
    }
    if (socket->state == NET_SOCKET_INET_TCP_FAILED) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOTCONN;
    }
    if (net_socket_deadline_passed(deadline)) {
      net_socket_tcp_abort(socket);
      irq_restore(state);
      return NET_SOCKET_ERR_TIMEDOUT;
    }
    result = net_socket_wait(&socket->connector, deadline, state);
    if (result != 0 && result != NET_SOCKET_ERR_TIMEDOUT) {
      return result;
    }
  }
}

int net_socket_listen(uint32_t owner_group, int handle, int backlog) {
  if (backlog <= 0) {
    return NET_SOCKET_ERR_INVAL;
  }
  uint8_t limit = backlog > (int)NET_SOCKET_MAX_BACKLOG
                      ? NET_SOCKET_MAX_BACKLOG
                      : (uint8_t)backlog;

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL || socket->type != NET_SOCKET_STREAM) {
    irq_restore(state);
    return socket == NULL ? NET_SOCKET_ERR_NOENT : NET_SOCKET_ERR_INVAL;
  }
  if (socket->domain == NET_SOCKET_AF_LOCAL) {
    if (socket->state != NET_SOCKET_LOCAL_READY || !socket->bound) {
      irq_restore(state);
      return NET_SOCKET_ERR_INVAL;
    }
    socket->backlog = limit;
    socket->state = NET_SOCKET_LOCAL_LISTENING;
    irq_restore(state);
    return 0;
  }
  if (socket->state != NET_SOCKET_INET_TCP_READY) {
    irq_restore(state);
    return NET_SOCKET_ERR_INVAL;
  }
  int result = net_socket_bind_any(socket);
  if (result != 0) {
    irq_restore(state);
    return result;
  }
  err_t error;
  struct tcp_pcb *listener =
      tcp_listen_with_backlog_and_err(socket->pcb.tcp, limit, &error);
  if (listener == NULL || error != ERR_OK) {
    irq_restore(state);
    return net_socket_lwip_error(error);
  }
  socket->pcb.tcp = listener;
  socket->backlog = limit;
  socket->state = NET_SOCKET_INET_TCP_LISTENING;
  tcp_arg(listener, socket);
  tcp_accept(listener, net_socket_tcp_accept);
  irq_restore(state);
  return 0;
}

int net_socket_accept(uint32_t owner_group, int handle,
                      net_socket_address_t *peer_address) {
  uint32_t deadline = 0;
  for (;;) {
    irq_state_t state = irq_save();
    net_socket_system_init();
    net_socket_t *listener = net_socket_find(owner_group, handle);
    if (listener == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    if (listener->state != NET_SOCKET_LOCAL_LISTENING &&
        listener->state != NET_SOCKET_INET_TCP_LISTENING) {
      irq_restore(state);
      return NET_SOCKET_ERR_INVAL;
    }
    net_socket_t *child = net_socket_pending_pop(listener);
    if (child != NULL) {
      if (peer_address != NULL) {
        *peer_address = child->peer;
      }
      int accepted = net_socket_handle(child);
      irq_restore(state);
      return accepted;
    }
    if (listener->nonblocking) {
      irq_restore(state);
      return NET_SOCKET_ERR_AGAIN;
    }
    if (deadline == 0 && listener->receive_timeout_ticks != 0) {
      deadline = net_socket_deadline_after(listener->receive_timeout_ticks);
    }
    int result = net_socket_wait(&listener->acceptor, deadline, state);
    if (result != 0) {
      return result;
    }
  }
}

static bool net_socket_local_source(const net_socket_t *socket,
                                    net_socket_address_t *source) {
  if (socket->bound) {
    *source = socket->local;
    return true;
  }
  net_socket_address_init(source, NET_SOCKET_AF_LOCAL);
  return false;
}

static int net_socket_send_local_stream(uint32_t owner_group, int handle,
                                        const void *data, uint32_t length,
                                        uint32_t flags) {
  for (;;) {
    irq_state_t state = irq_save();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    net_socket_t *peer = net_socket_local_peer(socket);
    if (socket->state != NET_SOCKET_LOCAL_CONNECTED || peer == NULL ||
        peer->peer_closed) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOTCONN;
    }
    if (!net_socket_queue_has_room(peer, length) || net_packet_free == NULL) {
      if (flags & NET_SOCKET_MSG_DONTWAIT) {
        irq_restore(state);
        return NET_SOCKET_ERR_AGAIN;
      }
      int result = net_socket_wait(&socket->writer, 0, state);
      if (result != 0) {
        return result;
      }
      continue;
    }
    irq_restore(state);

    void *copy = malloc((int)length);
    if (copy == NULL) {
      return NET_SOCKET_ERR_NOMEM;
    }
    memcpy(copy, data, length);

    state = irq_save();
    socket = net_socket_find(owner_group, handle);
    peer = socket == NULL ? NULL : net_socket_local_peer(socket);
    if (socket != NULL && socket->state == NET_SOCKET_LOCAL_CONNECTED &&
        peer != NULL && !peer->peer_closed &&
        net_socket_queue_has_room(peer, length) && net_packet_free != NULL) {
      net_socket_address_t source;
      net_socket_local_source(socket, &source);
      if (net_socket_queue_local(peer, copy, length, &source)) {
        irq_restore(state);
        return (int)length;
      }
    }
    bool missing_socket = socket == NULL;
    bool disconnected = peer == NULL || (peer != NULL && peer->peer_closed);
    bool would_block = !missing_socket && !disconnected &&
                       (!net_socket_queue_has_room(peer, length) ||
                        net_packet_free == NULL);
    irq_restore(state);
    free(copy);
    if (missing_socket) {
      return NET_SOCKET_ERR_NOENT;
    }
    if (disconnected) {
      return NET_SOCKET_ERR_NOTCONN;
    }
    if ((flags & NET_SOCKET_MSG_DONTWAIT) || !would_block) {
      return NET_SOCKET_ERR_AGAIN;
    }
  }
}

static int net_socket_send_local_dgram(uint32_t owner_group, int handle,
                                       const void *data, uint32_t length,
                                       uint32_t flags,
                                       const net_socket_address_t *address) {
  (void)flags;
  for (;;) {
    irq_state_t state = irq_save();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    const net_socket_address_t *destination = address;
    if (destination == NULL) {
      if (socket->peer.family != NET_SOCKET_AF_LOCAL ||
          socket->peer.value.local.length == 0) {
        irq_restore(state);
        return NET_SOCKET_ERR_NOTCONN;
      }
      destination = &socket->peer;
    }
    net_socket_address_t target_address = *destination;
    net_socket_t *target =
        net_socket_find_local_bound(&target_address, NET_SOCKET_DGRAM);
    if (target == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    if (!net_socket_queue_has_room(target, length) || net_packet_free == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_AGAIN;
    }
    irq_restore(state);

    void *copy = malloc((int)length);
    if (copy == NULL) {
      return NET_SOCKET_ERR_NOMEM;
    }
    memcpy(copy, data, length);

    state = irq_save();
    socket = net_socket_find(owner_group, handle);
    target = socket == NULL ? NULL
                            : net_socket_find_local_bound(&target_address,
                                                          NET_SOCKET_DGRAM);
    if (socket != NULL && target != NULL &&
        net_socket_queue_has_room(target, length) && net_packet_free != NULL) {
      net_socket_address_t source;
      net_socket_local_source(socket, &source);
      if (net_socket_queue_local(target, copy, length, &source)) {
        irq_restore(state);
        return (int)length;
      }
    }
    irq_restore(state);
    free(copy);
    if (socket == NULL) {
      return NET_SOCKET_ERR_NOENT;
    }
    return target == NULL ? NET_SOCKET_ERR_NOENT : NET_SOCKET_ERR_AGAIN;
  }
}

static int net_socket_send_tcp(uint32_t owner_group, int handle,
                               const void *data, uint32_t length,
                               uint32_t flags) {
  uint32_t sent = 0;
  while (sent < length) {
    irq_state_t state = irq_save();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return sent != 0 ? (int)sent : NET_SOCKET_ERR_NOENT;
    }
    if (socket->state != NET_SOCKET_INET_TCP_CONNECTED ||
        socket->pcb.tcp == NULL) {
      irq_restore(state);
      return sent != 0 ? (int)sent : NET_SOCKET_ERR_NOTCONN;
    }
    uint16_t available = tcp_sndbuf(socket->pcb.tcp);
    if (available != 0) {
      uint16_t part = (uint16_t)((length - sent) < available ? length - sent
                                                               : available);
      err_t error = tcp_write(socket->pcb.tcp, (const uint8_t *)data + sent,
                              part, TCP_WRITE_FLAG_COPY);
      if (error == ERR_OK) {
        (void)tcp_output(socket->pcb.tcp);
        sent += part;
        net_stack_poll_local(socket->peer.value.inet.address);
        irq_restore(state);
        continue;
      }
      if (error != ERR_MEM) {
        irq_restore(state);
        return sent != 0 ? (int)sent : net_socket_lwip_error(error);
      }
    }
    if (flags & NET_SOCKET_MSG_DONTWAIT) {
      irq_restore(state);
      return sent != 0 ? (int)sent : NET_SOCKET_ERR_AGAIN;
    }
    int result = net_socket_wait(&socket->writer, 0, state);
    if (result != 0) {
      return sent != 0 ? (int)sent : result;
    }
  }
  return (int)sent;
}

static int net_socket_send_udp(net_socket_t *socket, const void *data,
                               uint32_t length,
                               const net_socket_address_t *address) {
  const net_socket_address_t *destination = address;
  if (destination == NULL) {
    if (socket->peer.family != NET_SOCKET_AF_INET) {
      return NET_SOCKET_ERR_NOTCONN;
    }
    destination = &socket->peer;
  }
  int result = net_socket_bind_any(socket);
  if (result != 0) {
    return result;
  }

  struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
  if (packet == NULL || pbuf_take(packet, data, (u16_t)length) != ERR_OK) {
    if (packet != NULL) {
      pbuf_free(packet);
    }
    return NET_SOCKET_ERR_NOMEM;
  }
  ip_addr_t remote;
  net_socket_ip_from_address(&remote, destination->value.inet.address);
  err_t error = address == NULL
                    ? udp_send(socket->pcb.udp, packet)
                    : udp_sendto(socket->pcb.udp, packet, &remote,
                                 lwip_ntohs(destination->value.inet.port));
  pbuf_free(packet);
  if (error != ERR_OK) {
    return net_socket_lwip_error(error);
  }
  net_socket_udp_refresh_local(socket);
  net_stack_poll_local(destination->value.inet.address);
  return (int)length;
}

static int net_socket_send_raw(net_socket_t *socket, const void *data,
                               uint32_t length,
                               const net_socket_address_t *address) {
  const net_socket_address_t *destination = address;
  if (destination == NULL) {
    if (socket->peer.family != NET_SOCKET_AF_INET) {
      return NET_SOCKET_ERR_NOTCONN;
    }
    destination = &socket->peer;
  }
  if (destination->value.inet.port != 0) {
    return NET_SOCKET_ERR_INVAL;
  }
  struct pbuf *packet = pbuf_alloc(PBUF_IP, (u16_t)length, PBUF_RAM);
  if (packet == NULL || pbuf_take(packet, data, (u16_t)length) != ERR_OK) {
    if (packet != NULL) {
      pbuf_free(packet);
    }
    return NET_SOCKET_ERR_NOMEM;
  }
  ip_addr_t remote;
  net_socket_ip_from_address(&remote, destination->value.inet.address);
  err_t error = raw_sendto(socket->pcb.raw, packet, &remote);
  pbuf_free(packet);
  if (error != ERR_OK) {
    return net_socket_lwip_error(error);
  }
  net_stack_poll_local(destination->value.inet.address);
  return (int)length;
}

int net_socket_sendto(uint32_t owner_group, int handle, const void *data,
                      uint32_t length, uint32_t flags,
                      const net_socket_address_t *address) {
  if ((flags & ~NET_SOCKET_MSG_DONTWAIT) != 0 ||
      (length != 0 && data == NULL) || length > NET_SOCKET_MAX_PAYLOAD) {
    return NET_SOCKET_ERR_INVAL;
  }

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  if (socket->shutdown_write) {
    irq_restore(state);
    return NET_SOCKET_ERR_PIPE;
  }
  if (socket->nonblocking) {
    flags |= NET_SOCKET_MSG_DONTWAIT;
  }
  if (address != NULL && address->family != socket->domain) {
    irq_restore(state);
    return NET_SOCKET_ERR_INVAL;
  }
  if (length == 0) {
    irq_restore(state);
    return 0;
  }

  if (socket->domain == NET_SOCKET_AF_LOCAL) {
    uint8_t type = socket->type;
    irq_restore(state);
    if (type == NET_SOCKET_STREAM) {
      return address == NULL ? net_socket_send_local_stream(owner_group, handle,
                                                             data, length, flags)
                             : NET_SOCKET_ERR_INVAL;
    }
    return net_socket_send_local_dgram(owner_group, handle, data, length, flags,
                                       address);
  }
  if (socket->protocol == NET_SOCKET_PROTOCOL_TCP) {
    irq_restore(state);
    return address == NULL ? net_socket_send_tcp(owner_group, handle, data,
                                                  length, flags)
                           : NET_SOCKET_ERR_INVAL;
  }
  int result = socket->protocol == NET_SOCKET_PROTOCOL_UDP
                   ? net_socket_send_udp(socket, data, length, address)
                   : net_socket_send_raw(socket, data, length, address);
  irq_restore(state);
  return result;
}

static uint32_t net_socket_packet_copy(const net_socket_packet_t *packet,
                                       void *destination, uint32_t offset,
                                       uint32_t length) {
  if (packet->pbuf != NULL) {
    return pbuf_copy_partial(packet->pbuf,
                             (uint8_t *)destination + offset, (u16_t)length,
                             (u16_t)packet->offset);
  }
  memcpy((uint8_t *)destination + offset,
         (const uint8_t *)packet->local_data + packet->offset, length);
  return length;
}

static void net_socket_wake_local_writer(net_socket_t *receiver) {
  net_socket_t *sender = net_socket_local_peer(receiver);
  if (sender != NULL) {
    net_socket_waiter_wake(&sender->writer);
  }
}

int net_socket_recvfrom(uint32_t owner_group, int handle, void *data,
                        uint32_t length, uint32_t flags,
                        net_socket_address_t *peer_address) {
  if ((flags & ~NET_SOCKET_MSG_DONTWAIT) != 0 ||
      (length != 0 && data == NULL)) {
    return NET_SOCKET_ERR_INVAL;
  }

  uint32_t deadline = 0;
  for (;;) {
    irq_state_t state = irq_save();
    net_socket_system_init();
    net_socket_t *socket = net_socket_find(owner_group, handle);
    if (socket == NULL) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    if (socket->shutdown_read) {
      net_socket_clear_packets(socket);
      irq_restore(state);
      return 0;
    }
    if (length == 0) {
      irq_restore(state);
      return 0;
    }

    net_socket_packet_t *packet = socket->receive_head;
    if (packet != NULL) {
      if (peer_address != NULL) {
        *peer_address = packet->source;
      }
      uint32_t received = 0;
      bool stream = socket->type == NET_SOCKET_STREAM;
      while (packet != NULL && received < length) {
        uint32_t available = packet->total - packet->offset;
        uint32_t part = available < length - received ? available
                                                       : length - received;
        if (net_socket_packet_copy(packet, data, received, part) != part) {
          irq_restore(state);
          return received != 0 ? (int)received : NET_SOCKET_ERR_INVAL;
        }
        packet->offset += part;
        socket->queued_bytes -= part;
        received += part;
        if (socket->protocol == NET_SOCKET_PROTOCOL_TCP &&
            socket->pcb.tcp != NULL) {
          tcp_recved(socket->pcb.tcp, (u16_t)part);
        }
        if (!stream && packet->offset != packet->total) {
          socket->queued_bytes -= packet->total - packet->offset;
          packet->offset = packet->total;
        }
        if (packet->offset != packet->total && stream) {
          break;
        }

        socket->receive_head = packet->next;
        if (socket->receive_head == NULL) {
          socket->receive_tail = NULL;
        }
        net_socket_packet_free(packet);
        net_socket_wake_local_writer(socket);
        if (!stream) {
          break;
        }
        packet = socket->receive_head;
      }
      irq_restore(state);
      return (int)received;
    }

    if (socket->type == NET_SOCKET_STREAM &&
        (socket->peer_closed || socket->state == NET_SOCKET_INET_TCP_FAILED)) {
      int result = socket->state == NET_SOCKET_INET_TCP_FAILED
                       ? NET_SOCKET_ERR_NOTCONN
                       : 0;
      irq_restore(state);
      return result;
    }
    if ((flags & NET_SOCKET_MSG_DONTWAIT) || socket->nonblocking) {
      irq_restore(state);
      return NET_SOCKET_ERR_AGAIN;
    }
    if (deadline == 0 && socket->receive_timeout_ticks != 0) {
      deadline = net_socket_deadline_after(socket->receive_timeout_ticks);
    }
    int result = net_socket_wait(&socket->reader, deadline, state);
    if (result != 0) {
      return result;
    }
  }
}

int net_socket_getname(uint32_t owner_group, int handle, bool peer,
                       net_socket_address_t *address) {
  if (address == NULL) {
    return NET_SOCKET_ERR_INVAL;
  }
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  if (peer) {
    if (socket->peer.family == 0) {
      irq_restore(state);
      return NET_SOCKET_ERR_NOTCONN;
    }
    *address = socket->peer;
  } else {
    net_socket_refresh_local(socket);
    *address = socket->local;
  }
  irq_restore(state);
  return 0;
}

static bool net_socket_option_flag(int option, uint32_t *mask) {
  switch (option) {
  case NET_SOCKET_SO_DEBUG:
    *mask = NET_SOCKET_OPTION_DEBUG;
    return true;
  case NET_SOCKET_SO_REUSEADDR:
    *mask = NET_SOCKET_OPTION_REUSEADDR;
    return true;
  case NET_SOCKET_SO_KEEPALIVE:
    *mask = NET_SOCKET_OPTION_KEEPALIVE;
    return true;
  case NET_SOCKET_SO_DONTROUTE:
    *mask = NET_SOCKET_OPTION_DONTROUTE;
    return true;
  case NET_SOCKET_SO_BROADCAST:
    *mask = NET_SOCKET_OPTION_BROADCAST;
    return true;
  case NET_SOCKET_SO_OOBINLINE:
    *mask = NET_SOCKET_OPTION_OOBINLINE;
    return true;
  case NET_SOCKET_SO_REUSEPORT:
    *mask = NET_SOCKET_OPTION_REUSEPORT;
    return true;
  default:
    return false;
  }
}

static bool net_socket_timeval_to_ticks(const void *value, uint32_t length,
                                        uint32_t *ticks) {
  if (value == NULL || length != sizeof(net_socket_timeval_t)) {
    return false;
  }
  const net_socket_timeval_t *time = (const net_socket_timeval_t *)value;
  if (time->seconds < 0 || time->microseconds < 0 ||
      time->microseconds >= 1000000) {
    return false;
  }
  uint64_t milliseconds = (uint64_t)time->seconds * 1000ull +
                          ((uint64_t)time->microseconds + 999u) / 1000u;
  if (milliseconds > UINT32_MAX) {
    return false;
  }
  *ticks = (uint32_t)(milliseconds / 10u + (milliseconds % 10u != 0));
  return true;
}

int net_socket_set_option(uint32_t owner_group, int handle, int level,
                          int option, const void *value, uint32_t length) {
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  if (value == NULL || length == 0) {
    irq_restore(state);
    return NET_SOCKET_ERR_INVAL;
  }

  uint32_t flag;
  if (level == NET_SOCKET_SOL_SOCKET && net_socket_option_flag(option, &flag)) {
    if (length != sizeof(int32_t)) {
      irq_restore(state);
      return NET_SOCKET_ERR_INVAL;
    }
    if (*(const int32_t *)value != 0) {
      socket->option_flags |= flag;
    } else {
      socket->option_flags &= ~flag;
    }
    irq_restore(state);
    return 0;
  }

  if (level == NET_SOCKET_SOL_SOCKET) {
    switch (option) {
    case NET_SOCKET_SO_RCVTIMEO:
      if (!net_socket_timeval_to_ticks(value, length,
                                       &socket->receive_timeout_ticks)) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      irq_restore(state);
      return 0;
    case NET_SOCKET_SO_SNDTIMEO:
      if (!net_socket_timeval_to_ticks(value, length,
                                       &socket->send_timeout_ticks)) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      irq_restore(state);
      return 0;
    case NET_SOCKET_SO_SNDBUF:
    case NET_SOCKET_SO_RCVBUF: {
      if (length != sizeof(int32_t) || *(const int32_t *)value <= 0) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      uint32_t size = (uint32_t)*(const int32_t *)value;
      if (option == NET_SOCKET_SO_SNDBUF) {
        socket->send_buffer_size = size;
      } else {
        socket->receive_buffer_size = size;
      }
      irq_restore(state);
      return 0;
    }
    case NET_SOCKET_SO_LINGER: {
      if (length != sizeof(net_socket_linger_t)) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      const net_socket_linger_t *linger =
          (const net_socket_linger_t *)value;
      if (linger->on < 0 || linger->on > 1 || linger->seconds < 0) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      socket->linger_enabled = linger->on != 0;
      socket->linger_seconds = linger->seconds;
      irq_restore(state);
      return 0;
    }
    case NET_SOCKET_SO_SNDLOWAT:
    case NET_SOCKET_SO_RCVLOWAT:
      /* The fixed queue has a one-byte low-water mark. */
      if (length != sizeof(int32_t) || *(const int32_t *)value <= 0) {
        irq_restore(state);
        return NET_SOCKET_ERR_INVAL;
      }
      irq_restore(state);
      return 0;
    case NET_SOCKET_SO_ACCEPTCONN:
    case NET_SOCKET_SO_ERROR:
    case NET_SOCKET_SO_TYPE:
      irq_restore(state);
      return NET_SOCKET_ERR_PROTOCOL;
    default:
      break;
    }
  }

  if (level == NET_SOCKET_IPPROTO_TCP && option == NET_SOCKET_TCP_NODELAY) {
    if (length != sizeof(int32_t) || socket->protocol != NET_SOCKET_PROTOCOL_TCP) {
      irq_restore(state);
      return NET_SOCKET_ERR_INVAL;
    }
    if (socket->pcb.tcp != NULL) {
      if (*(const int32_t *)value != 0) {
        tcp_nagle_disable(socket->pcb.tcp);
      } else {
        tcp_nagle_enable(socket->pcb.tcp);
      }
    }
    irq_restore(state);
    return 0;
  }

  if (level == NET_SOCKET_IPPROTO_IP) {
    if (option == NET_SOCKET_IP_TOS && length == sizeof(int32_t)) {
      socket->ip_tos = (uint8_t)*(const int32_t *)value;
      irq_restore(state);
      return 0;
    }
    if (option == NET_SOCKET_IP_MULTICAST_TTL &&
        (length == sizeof(int32_t) || length == sizeof(uint8_t))) {
      socket->multicast_ttl = length == sizeof(uint8_t)
                                  ? *(const uint8_t *)value
                                  : (uint8_t)*(const int32_t *)value;
      irq_restore(state);
      return 0;
    }
    if (option == NET_SOCKET_IP_MULTICAST_LOOP && length == sizeof(int32_t)) {
      socket->multicast_loop = *(const int32_t *)value != 0;
      irq_restore(state);
      return 0;
    }
  }

  irq_restore(state);
  return NET_SOCKET_ERR_PROTOCOL;
}

static int net_socket_option_copy(void *value, uint32_t *length,
                                  const void *source, uint32_t size) {
  if (value == NULL || length == NULL || *length < size) {
    return NET_SOCKET_ERR_INVAL;
  }
  memcpy(value, source, size);
  *length = size;
  return 0;
}

int net_socket_get_option(uint32_t owner_group, int handle, int level,
                          int option, void *value, uint32_t *length) {
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  if (level == NET_SOCKET_SOL_SOCKET) {
    uint32_t flag;
    if (net_socket_option_flag(option, &flag)) {
      int32_t enabled = (socket->option_flags & flag) != 0;
      int result = net_socket_option_copy(value, length, &enabled,
                                          sizeof(enabled));
      irq_restore(state);
      return result;
    }
    switch (option) {
    case NET_SOCKET_SO_RCVTIMEO: {
      net_socket_timeval_t time = {
          (int64_t)(socket->receive_timeout_ticks / 100u),
          (int64_t)((socket->receive_timeout_ticks % 100u) * 10000u)};
      int result = net_socket_option_copy(value, length, &time, sizeof(time));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_SNDTIMEO: {
      net_socket_timeval_t time = {
          (int64_t)(socket->send_timeout_ticks / 100u),
          (int64_t)((socket->send_timeout_ticks % 100u) * 10000u)};
      int result = net_socket_option_copy(value, length, &time, sizeof(time));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_SNDBUF: {
      int32_t size = (int32_t)socket->send_buffer_size;
      int result = net_socket_option_copy(value, length, &size, sizeof(size));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_RCVBUF: {
      int32_t size = (int32_t)socket->receive_buffer_size;
      int result = net_socket_option_copy(value, length, &size, sizeof(size));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_SNDLOWAT:
    case NET_SOCKET_SO_RCVLOWAT: {
      int32_t lowat = 1;
      int result = net_socket_option_copy(value, length, &lowat, sizeof(lowat));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_LINGER: {
      net_socket_linger_t linger = {socket->linger_enabled ? 1 : 0,
                                    socket->linger_seconds};
      int result = net_socket_option_copy(value, length, &linger,
                                          sizeof(linger));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_ACCEPTCONN: {
      int32_t listening = socket->state == NET_SOCKET_LOCAL_LISTENING ||
                          socket->state == NET_SOCKET_INET_TCP_LISTENING;
      int result = net_socket_option_copy(value, length, &listening,
                                          sizeof(listening));
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_ERROR: {
      uint32_t error = socket->state == NET_SOCKET_INET_TCP_CONNECTING
                           ? 115
                           : socket->last_error;
      int32_t result_value = (int32_t)error;
      int result = net_socket_option_copy(value, length, &result_value,
                                          sizeof(result_value));
      if (result == 0 && error != 115) {
        socket->last_error = 0;
      }
      irq_restore(state);
      return result;
    }
    case NET_SOCKET_SO_TYPE: {
      int32_t type = socket->type;
      int result = net_socket_option_copy(value, length, &type, sizeof(type));
      irq_restore(state);
      return result;
    }
    default:
      break;
    }
  }

  if (level == NET_SOCKET_IPPROTO_TCP && option == NET_SOCKET_TCP_NODELAY) {
    if (socket->protocol != NET_SOCKET_PROTOCOL_TCP) {
      irq_restore(state);
      return NET_SOCKET_ERR_INVAL;
    }
    int32_t enabled = socket->pcb.tcp != NULL &&
                      tcp_nagle_disabled(socket->pcb.tcp);
    int result = net_socket_option_copy(value, length, &enabled,
                                        sizeof(enabled));
    irq_restore(state);
    return result;
  }
  if (level == NET_SOCKET_IPPROTO_IP) {
    if (option == NET_SOCKET_IP_TOS) {
      int32_t tos = socket->ip_tos;
      int result = net_socket_option_copy(value, length, &tos, sizeof(tos));
      irq_restore(state);
      return result;
    }
    if (option == NET_SOCKET_IP_MULTICAST_TTL) {
      int32_t ttl = socket->multicast_ttl;
      int result = net_socket_option_copy(value, length, &ttl, sizeof(ttl));
      irq_restore(state);
      return result;
    }
    if (option == NET_SOCKET_IP_MULTICAST_LOOP) {
      int32_t enabled = socket->multicast_loop;
      int result = net_socket_option_copy(value, length, &enabled,
                                          sizeof(enabled));
      irq_restore(state);
      return result;
    }
    if (option == NET_SOCKET_IP_MULTICAST_IF) {
      uint32_t address = net_stack_ipv4();
      int result = net_socket_option_copy(value, length, &address,
                                          sizeof(address));
      irq_restore(state);
      return result;
    }
  }

  irq_restore(state);
  return NET_SOCKET_ERR_PROTOCOL;
}

int net_socket_shutdown(uint32_t owner_group, int handle, int how) {
  if (how < 0 || how > 2) {
    return NET_SOCKET_ERR_INVAL;
  }
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  bool shut_read = how == 0 || how == 2;
  bool shut_write = how == 1 || how == 2;
  if (socket->protocol == NET_SOCKET_PROTOCOL_TCP && socket->pcb.tcp != NULL) {
    err_t error = tcp_shutdown(socket->pcb.tcp, shut_read, shut_write);
    if (error != ERR_OK && error != ERR_CONN) {
      irq_restore(state);
      return net_socket_lwip_error(error);
    }
    if (shut_write && shut_read) {
      socket->pcb.tcp = NULL;
      socket->state = NET_SOCKET_INET_TCP_FAILED;
      socket->peer_closed = true;
    }
  }
  if (shut_read) {
    socket->shutdown_read = true;
    net_socket_clear_packets(socket);
    net_socket_waiter_wake(&socket->reader);
  }
  if (shut_write) {
    socket->shutdown_write = true;
    net_socket_waiter_wake(&socket->writer);
    net_socket_t *peer = net_socket_local_peer(socket);
    if (peer != NULL) {
      peer->peer_closed = true;
      net_socket_waiter_wake(&peer->reader);
      net_socket_waiter_wake(&peer->writer);
    }
  }
  irq_restore(state);
  return 0;
}

int net_socket_pair(uint32_t owner_group, int domain, int type, int protocol,
                    int handles[2]) {
  if (handles == NULL || domain != NET_SOCKET_AF_LOCAL ||
      type != NET_SOCKET_STREAM || protocol != 0) {
    return NET_SOCKET_ERR_PROTOCOL;
  }
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *left = net_socket_reserve(owner_group);
  net_socket_t *right = left == NULL ? NULL : net_socket_reserve(owner_group);
  if (left == NULL || right == NULL) {
    if (left != NULL) {
      net_socket_dispose(left);
    }
    irq_restore(state);
    return NET_SOCKET_ERR_NOMEM;
  }
  left->domain = NET_SOCKET_AF_LOCAL;
  left->type = NET_SOCKET_STREAM;
  left->state = NET_SOCKET_LOCAL_CONNECTED;
  right->domain = NET_SOCKET_AF_LOCAL;
  right->type = NET_SOCKET_STREAM;
  right->state = NET_SOCKET_LOCAL_CONNECTED;
  net_socket_address_init(&left->local, NET_SOCKET_AF_LOCAL);
  net_socket_address_init(&right->local, NET_SOCKET_AF_LOCAL);
  left->peer_slot = (uint8_t)(right - net_sockets);
  left->peer_generation = right->generation;
  right->peer_slot = (uint8_t)(left - net_sockets);
  right->peer_generation = left->generation;
  handles[0] = net_socket_handle(left);
  handles[1] = net_socket_handle(right);
  irq_restore(state);
  return 0;
}

int net_socket_get_flags(uint32_t owner_group, int handle) {
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  int flags = 2; /* O_RDWR */
  if (socket->nonblocking) {
    flags |= 0x2000; /* O_NONBLOCK */
  }
  irq_restore(state);
  return flags;
}

int net_socket_set_flags(uint32_t owner_group, int handle, int flags) {
  if (flags & ~(O_ACCMODE | O_NONBLOCK)) {
    return NET_SOCKET_ERR_INVAL;
  }
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  socket->nonblocking = (flags & O_NONBLOCK) != 0;
  irq_restore(state);
  return 0;
}

int net_socket_bytes_available(uint32_t owner_group, int handle,
                               uint32_t *bytes) {
  if (bytes == NULL)
    return NET_SOCKET_ERR_INVAL;
  irq_state_t state = irq_save();
  net_socket_system_init();
  net_socket_t *socket = net_socket_find(owner_group, handle);
  if (socket == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_NOENT;
  }
  *bytes = socket->queued_bytes;
  irq_restore(state);
  return 0;
}

static void net_socket_dns_found(const char *name, const ip_addr_t *address,
                                 void *arg) {
  (void)name;
  net_dns_request_t *request = (net_dns_request_t *)arg;
  if (request == NULL || request->state != NET_DNS_PENDING) {
    return;
  }
  if (address != NULL && IP_IS_V4(address)) {
    request->address = ip4_addr_get_u32(ip_2_ip4(address));
    request->state = NET_DNS_COMPLETE;
  } else {
    request->state = NET_DNS_FAILED;
  }
  if (request->abandoned) {
    request->state = NET_DNS_FREE;
  } else {
    net_socket_waiter_wake(&request->waiter);
  }
}

static net_dns_request_t *net_socket_dns_reserve(void) {
  for (unsigned i = 0; i < NET_SOCKET_DNS_REQUEST_CAPACITY; i++) {
    if (net_dns_requests[i].state == NET_DNS_FREE) {
      memset(&net_dns_requests[i], 0, sizeof(net_dns_requests[i]));
      net_dns_requests[i].state = NET_DNS_PENDING;
      net_socket_waiter_init(&net_dns_requests[i].waiter);
      return &net_dns_requests[i];
    }
  }
  return NULL;
}

int net_socket_resolve(const char *name, uint32_t length,
                       uint32_t *address) {
  if (name == NULL || address == NULL || length == 0 ||
      length > NET_SOCKET_DNS_NAME_MAX || name[length - 1] != '\0') {
    return NET_SOCKET_ERR_INVAL;
  }
  if (!net_stack_ready()) {
    return NET_SOCKET_ERR_NETDOWN;
  }

  irq_state_t state = irq_save();
  net_socket_system_init();
  net_dns_request_t *request = net_socket_dns_reserve();
  if (request == NULL) {
    irq_restore(state);
    return NET_SOCKET_ERR_AGAIN;
  }
  memcpy(request->name, name, length);
  request->owner_tid = current_task()->tid;
  request->owner_generation = current_task()->generation;

  ip_addr_t result;
  err_t error = dns_gethostbyname(request->name, &result, net_socket_dns_found,
                                  request);
  if (error == ERR_OK) {
    *address = ip4_addr_get_u32(ip_2_ip4(&result));
    request->state = NET_DNS_FREE;
    irq_restore(state);
    return 0;
  }
  if (error != ERR_INPROGRESS) {
    request->state = NET_DNS_FREE;
    irq_restore(state);
    return net_socket_lwip_error(error);
  }
  irq_restore(state);

  for (;;) {
    state = irq_save();
    if (request->state == NET_DNS_COMPLETE) {
      *address = request->address;
      request->state = NET_DNS_FREE;
      irq_restore(state);
      return 0;
    }
    if (request->state != NET_DNS_PENDING) {
      request->state = NET_DNS_FREE;
      irq_restore(state);
      return NET_SOCKET_ERR_NOENT;
    }
    int wait = net_socket_wait(&request->waiter, 0, state);
    if (wait != 0) {
      return wait;
    }
  }
}

void net_socket_tick(void) {
  irq_state_t state = irq_save();
  if (net_socket_initialized) {
    for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
      net_socket_t *socket = &net_sockets[i];
      if (socket->state == NET_SOCKET_FREE) {
        continue;
      }
      net_socket_waiter_t *waiters[] = {
          &socket->reader, &socket->writer, &socket->connector,
          &socket->acceptor,
      };
      for (unsigned j = 0; j < sizeof(waiters) / sizeof(waiters[0]); j++) {
        if (net_socket_deadline_passed(waiters[j]->deadline)) {
          net_socket_waiter_wake(waiters[j]);
        }
      }
    }
  }
  irq_restore(state);
}

void net_socket_cancel_waits(uint32_t tid, uint32_t generation) {
  irq_state_t state = irq_save();
  if (!net_socket_initialized) {
    irq_restore(state);
    return;
  }
  for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
    net_socket_t *socket = &net_sockets[i];
    if (socket->state == NET_SOCKET_FREE) {
      continue;
    }
    net_socket_waiter_cancel(&socket->reader, tid, generation);
    net_socket_waiter_cancel(&socket->writer, tid, generation);
    net_socket_waiter_cancel(&socket->connector, tid, generation);
    net_socket_waiter_cancel(&socket->acceptor, tid, generation);
  }
  for (unsigned i = 0; i < NET_SOCKET_DNS_REQUEST_CAPACITY; i++) {
    net_dns_request_t *request = &net_dns_requests[i];
    if (request->state == NET_DNS_FREE || request->owner_tid != tid ||
        request->owner_generation != generation) {
      continue;
    }
    net_socket_waiter_cancel(&request->waiter, tid, generation);
    if (request->state == NET_DNS_PENDING) {
      request->abandoned = true;
    } else {
      request->state = NET_DNS_FREE;
    }
  }
  irq_restore(state);
}

void net_socket_task_cleanup(uint32_t owner_group) {
  irq_state_t state = irq_save();
  if (net_socket_initialized) {
    for (unsigned i = 0; i < NET_SOCKET_CAPACITY; i++) {
      if (net_sockets[i].state != NET_SOCKET_FREE &&
          net_sockets[i].owner_group == owner_group) {
        net_socket_dispose(&net_sockets[i]);
      }
    }
  }
  irq_restore(state);
}
