#include <net.h>
// ICMP
static u32 ping_reply_flags = 1;
static u8  ping_reply_data[2048];
u8        *ICMP_Packet(u8 type, u8 code, u16 ID, u16 sequence, u8 *data, u32 size) {
  struct ICMPMessage *res = (struct ICMPMessage *)page_malloc(sizeof(struct ICMPMessage) + size);
  u8                 *dat = (u8 *)res;
  memcpy(dat + sizeof(struct ICMPMessage), data, size);
  res->type     = type;
  res->code     = code;
  res->ID       = swap16(ID);
  res->sequence = swap16(sequence);
  res->checksum = 0;
  res->checksum = CheckSum((u16 *)dat, size + sizeof(struct ICMPMessage));
  return (u8 *)res;
}
void ICMPProviderSend(u32 destip, u32 srcip, u8 type, u8 code, u16 ID, u16 sequence, u8 *data,
                      u32 size) {
  IPV4ProviderSend(1, IPParseMAC(destip), destip, srcip,
                   ICMP_Packet(type, code, ID, sequence, data, size),
                   size + sizeof(struct ICMPMessage));
}
int ping(u32 dstIP) {
  extern u32 ip;
  u8         data[PING_SIZE];
  for (int i = 0; i != PING_SIZE; i++)
    data[i] = PING_DATA;
  clean(ping_reply_data, 2048);
  ping_reply_flags = 1;
  ICMPProviderSend(dstIP, ip, 8, 0, PING_ID, PING_SEQ, data, PING_SIZE);
  extern struct TIMERCTL timerctl;
  u32                    time = timerctl.count;
  while (ping_reply_flags) {
    if (timerctl.count - time > PING_WAITTIME) { return -1; }
  }
  struct ICMPMessage *icmp =
      (struct ICMPMessage *)(ping_reply_data + sizeof(struct EthernetFrame_head) +
                             sizeof(struct IPV4Message));
  u8 *dat = ping_reply_data + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
            sizeof(struct ICMPMessage);
  if (swap16(icmp->ID) != PING_ID || swap16(icmp->sequence) != PING_SEQ ||
      strncmp(dat, data, PING_SIZE) != 0) {
    return -1;
  }
  return 0;
}
void cmd_ping(char *cmdline) {
  u32 dst_ip;
  u8  ip0, ip1, ip2, ip3;
  if (cmdline[0] < '0' || cmdline[0] > '9') { // 不是数字
    u8 *dns = (u8 *)page_malloc(strlen(cmdline) + 1);
    memcpy(dns + 1, cmdline, strlen(cmdline));
    dst_ip = dns_parse_ip(dns + 1);
    page_free(dns, strlen(cmdline) + 1);
  } else {
    dst_ip = IP2UINT32_T(cmdline);
  }
  ip0 = (u8)(dst_ip >> 24);
  ip1 = (u8)(dst_ip >> 16);
  ip2 = (u8)(dst_ip >> 8);
  ip3 = (u8)(dst_ip);
  printk("Pinging %d.%d.%d.%d from %d bytes data:\n\n", ip0, ip1, ip2, ip3, PING_SIZE + 2 + 2);
  u8         lost = 0, get = 4;
  extern u32 ip;
  uint64_t   mac_address = IPParseMAC(dst_ip);
  u8         data[PING_SIZE];
  for (int i = 0; i != PING_SIZE; i++)
    data[i] = PING_DATA;
  u8 *packet = ICMP_Packet(8, 0, PING_ID, PING_SEQ, data, PING_SIZE);
  for (int i = 0; i != 4; i++) {
    ping_reply_flags = 1;
    clean(ping_reply_data, 2048);
    u32 time = timerctl.count;
    IPV4ProviderSend(1, mac_address, dst_ip, ip, packet, PING_SIZE + sizeof(struct ICMPMessage));
    while (ping_reply_flags) {
      if (timerctl.count - time >= PING_WAITTIME) {
        printk("Reply timed out.\n");
        lost++;
        get--;
        break;
      }
    }
    time = timerctl.count - time;
    sleep(1000); // 不要着急 休息一下
    struct IPV4Message *ipv4 =
        (struct IPV4Message *)(ping_reply_data + sizeof(struct EthernetFrame_head));
    struct ICMPMessage *icmp =
        (struct ICMPMessage *)(ping_reply_data + sizeof(struct EthernetFrame_head) +
                               sizeof(struct IPV4Message));
    u8 *dat = ping_reply_data + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
              sizeof(struct ICMPMessage);
    if (swap16(icmp->ID) == PING_ID && swap16(icmp->sequence) == PING_SEQ &&
        strncmp(dat, data, PING_SIZE) == 0) {
      printk("From %d.%d.%d.%d reply: bytes:%d time:%dms TTL:%d\n", ip0, ip1, ip2, ip3,
             PING_SIZE + 2 + 2, time, ipv4->timeToLive);
    }
  }
  printk("\nAll:4 packets Get:%d packets Lost:%d packets\n\n", get, lost);
}
void icmp_handler(void *base) {
  extern u32                 ip;
  struct EthernetFrame_head *header = (struct EthernetFrame_head *)(base);
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct ICMPMessage *icmp =
      (struct ICMPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  if (icmp->type == 8 && icmp->code == 0) { // Ping请求
    u32 size  = (((ipv4->totalLength & 0xff00) >> 8) | ((ipv4->totalLength & 0x00ff) << 8));
    size     -= sizeof(struct IPV4Message);
    size     -= sizeof(struct ICMPMessage);
    u8 *data  = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
               sizeof(struct ICMPMessage);
    u32 src_ip = ((ipv4->srcIP << 24) & 0xff000000) | ((ipv4->srcIP << 8) & 0x00ff0000) |
                 ((ipv4->srcIP >> 8) & 0xff00) | ((ipv4->srcIP >> 24) & 0xff);
    u8 *packet = ICMP_Packet(0, 0, swap16(icmp->ID), swap16(icmp->sequence), data, size);
    IPV4ProviderSend(1, *(uint64_t *)&header->src_mac[0], src_ip, ip, packet,
                     size + sizeof(struct ICMPMessage)); // 给答复
  } else if (icmp->type == 0 && icmp->code == 0) {       // Ping答复
    memcpy(ping_reply_data, base, 2048);
    ping_reply_flags = 0;
  }
}
