#include <dos.h>
#include <net.h>

// DHCP
u32        gateway, submask, dns, ip, dhcp_ip;
static int fill_dhcp_option(u8 *packet, u8 code, u8 *data, u8 len) {
  packet[0] = code;
  packet[1] = len;
  memcpy(&packet[2], data, len);

  return len + (sizeof(u8) * 2);
}
static int fill_dhcp_discovery_options(struct DHCPMessage *dhcp) {
  int len = 0;
  u32 req_ip;
  u8  parameter_req_list[] = {MESSAGE_TYPE_REQ_SUBNET_MASK, MESSAGE_TYPE_ROUTER, MESSAGE_TYPE_DNS,
                              MESSAGE_TYPE_DOMAIN_NAME};
  u8  option;

  option  = DHCP_OPTION_DISCOVER;
  len    += fill_dhcp_option(&dhcp->bp_options[len], MESSAGE_TYPE_DHCP, &option, sizeof(option));
  req_ip  = swap32(0xffffffff);
  len +=
      fill_dhcp_option(&dhcp->bp_options[len], MESSAGE_TYPE_REQ_IP, (u8 *)&req_ip, sizeof(req_ip));
  len    += fill_dhcp_option(&dhcp->bp_options[len], MESSAGE_TYPE_PARAMETER_REQ_LIST,
                             (u8 *)&parameter_req_list, sizeof(parameter_req_list));
  option  = 0;
  len    += fill_dhcp_option(&dhcp->bp_options[len], MESSAGE_TYPE_END, &option, sizeof(option));

  return len;
}
static void dhcp_output(struct DHCPMessage *dhcp, u8 *mac, int *len) {
  *len += sizeof(struct DHCPMessage);
  memset(dhcp, 0, sizeof(struct DHCPMessage));

  dhcp->opcode = DHCP_BOOTREQUEST;
  dhcp->htype  = DHCP_HARDWARE_TYPE_10_EHTHERNET;
  dhcp->hlen   = 6;
  memcpy(dhcp->chaddr, mac, DHCP_CHADDR_LEN);

  dhcp->magic_cookie = swap32(DHCP_MAGIC_COOKIE);
}
int dhcp_discovery(u8 *mac) {
  int                 len  = 0;
  struct DHCPMessage *dhcp = (struct DHCPMessage *)malloc(sizeof(struct DHCPMessage));

  len = fill_dhcp_discovery_options(dhcp);
  dhcp_output(dhcp, mac, &len);
  udp_provider_send(0xffffffff, 0x0, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, (u8 *)dhcp, len);
  return 0;
}
void dhcp_handler(void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct UDPMessage  *udp =
      (struct UDPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  struct DHCPMessage *dhcp =
      (struct DHCPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
                             sizeof(struct UDPMessage));
  if (dhcp->bp_options[0] == 53 && dhcp->bp_options[1] == 1 &&
      dhcp->bp_options[2] == DHCP_OPTION_OFFER) {
    // printk("DHCP Offer\n");
    ip      = dhcp->yiaddr;
    u8 nip1 = ip;
    u8 nip2 = ip >> 8;
    u8 nip3 = ip >> 16;
    u8 nip4 = ip >> 24;
    // printk("DHCP: %d.%d.%d.%d\n", (u8)(ipv4->srcIP),
    //        (u8)(ipv4->srcIP >> 8), (u8)(ipv4->srcIP >> 16),
    //        (u8)(ipv4->srcIP >> 24));
    dhcp_ip = swap32(ipv4->srcIP);
    //printk("IP: %d.%d.%d.%d\n", nip1, nip2, nip3, nip4);
    ip = swap32(ip);

    u8 *options = &dhcp->bp_options[0];
    while (options[0] != 0xff) {
      if (options[0] == MESSAGE_TYPE_DNS) {
        // printk("DNS: %d.%d.%d.%d\n", options[2], options[3], options[4],
        //     options[5]);
        dns = swap32(*(u32 *)&options[2]);
      } else if (options[0] == MESSAGE_TYPE_REQ_SUBNET_MASK) {
        // printk("Subnet Mask: %d.%d.%d.%d\n", options[2], options[3], options[4],
        //      options[5]);
        submask = swap32(*(u32 *)&options[2]);
      } else if (options[0] == MESSAGE_TYPE_ROUTER) {
        // printk("Gateway: %d.%d.%d.%d\n", options[2], options[3], options[4],
        //      options[5]);
        gateway = swap32(*(u32 *)&options[2]);
      }
      options += options[1] + 2;
    }
  }
}