#include <dos.h>
#include <irq.h>
#include <net_link.h>

#include <lwip/def.h>
#include <lwip/dhcp.h>
#include <lwip/err.h>
#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>

#define NET_FRAME_BYTES 1514u

static struct netif net_interface;
static uint8_t net_mac[6];
static uint8_t net_transmit_frame[NET_FRAME_BYTES];
static bool net_started;

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

bool net_stack_ready(void) {
  irq_state_t state = irq_save();
  bool ready = net_started;
  irq_restore(state);
  return ready;
}

uint32_t net_stack_ipv4(void) {
  if (!net_started) {
    return 0;
  }
  irq_state_t state = irq_save();
  uint32_t address = ip4_addr_get_u32(netif_ip4_addr(&net_interface));
  irq_restore(state);
  return address;
}
