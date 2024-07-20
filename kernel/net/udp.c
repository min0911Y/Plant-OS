#include <dos.h>
#include <net.h>
// UDP
u8 *UDP_Packet(u16 dest_port, u16 src_port, u8 *data, u32 size) {
  struct UDPMessage *res = (struct UDPMessage *)page_malloc(size + sizeof(struct UDPMessage));
  u8                *dat = (u8 *)res;
  memcpy((void *)(dat + sizeof(struct UDPMessage)), data, size);
  res->srcPort  = ((src_port & 0xff00) >> 8) | ((src_port & 0x00ff) << 8);
  res->dstPort  = ((dest_port & 0xff00) >> 8) | ((dest_port & 0x00ff) << 8);
  res->length   = size + sizeof(struct UDPMessage);
  res->length   = ((res->length & 0xff00) >> 8) | ((res->length & 0x00ff) << 8);
  res->checkSum = 0;
  return (u8 *)res;
}
void udp_provider_send(u32 destip, u32 srcip, u16 dest_port, u16 src_port, u8 *data, u32 size) {
  if (destip == 0xffffffff) {
    IPV4ProviderSend(17, 0xffffffffffff, destip, srcip, UDP_Packet(dest_port, src_port, data, size),
                     size + sizeof(struct UDPMessage));
    return;
  }
  IPV4ProviderSend(17, IPParseMAC(destip), destip, srcip,
                   UDP_Packet(dest_port, src_port, data, size), size + sizeof(struct UDPMessage));
}
void udp_handler(void *base) {
  // printk("UDP Packet:\n");
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct UDPMessage  *udp =
      (struct UDPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  if (swap16(udp->srcPort) == DHCP_SERVER_PORT) { // DHCP给我们分配IP
    dhcp_handler(base);
  } else if (swap32(ipv4->srcIP) == DNS_SERVER_IP && swap16(udp->srcPort) == DNS_PORT) {
    dns_handler(base);
  } else {
    // Socket
    struct Socket *socket = Socket_Find(swap32(ipv4->srcIP), swap16(udp->srcPort),
                                        swap32(ipv4->dstIP), swap16(udp->dstPort), UDP_PROTOCOL);
    if (socket == NULL) { return; }
    if (socket->Handler != NULL) { socket->Handler(socket, base); }
  }
}
