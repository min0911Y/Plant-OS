/* 网络粘合层 */
#include <dos.h>
extern u8 mac0, mac1, mac2, mac3, mac4, mac5;
u32       strtoul(const char *str, char **endptr, int base);
void      Rtl8139Send(u8 *buffer, int size);
typedef struct {
  bool (*find)();
  void (*init)();
  void (*Send)(u8 *buffer, u32 size);
  char card_name[50];
  int  use; // 正在使用
  int  flag;
} network_card;
bool         pcnet_find_card();
network_card network_card_CTL[25] = {};
static u8   *IP_Packet_Base[16]   = {};
static u32   Find_IP_Packet(u16 ident) {
  for (int i = 0; i != 16; i++) {
    if (IP_Packet_Base[i] != NULL) {
      struct IPV4Message *ipv4 =
          (struct IPV4Message *)(IP_Packet_Base[i] + sizeof(struct EthernetFrame_head));
      if (swap16(ipv4->ident) == ident) { return i; }
    }
  }
  return -1;
}
static void IP_Assembling(struct IPV4Message *ipv4, u8 *RawData) {
  u32                 i_p = Find_IP_Packet(swap16(ipv4->ident));
  struct IPV4Message *ipv4_p =
      (struct IPV4Message *)(IP_Packet_Base[i_p] + sizeof(struct EthernetFrame_head));
  u32 size_p = swap16(ipv4_p->totalLength);
  ipv4_p->totalLength =
      swap16(swap16(ipv4->totalLength) + swap16(ipv4_p->totalLength) - sizeof(struct IPV4Message));
  IP_Packet_Base[i_p] = (u8 *)realloc((void *)IP_Packet_Base[i_p], swap16(ipv4_p->totalLength));
  memcpy((void *)(IP_Packet_Base[i_p] + size_p),
         RawData + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message),
         swap16(ipv4->totalLength) - sizeof(struct IPV4Message));
  return;
}

void init_networkCTL() {} // 废弃的函数

void register_network_card(network_card netCard) {
  for (int i = 0; i < 25; i++) {
    if (network_card_CTL[i].flag == 0) {
      network_card_CTL[i] = netCard;
      break;
    }
  }
}

void init_card() {
  // printk("init card\n");
  for (int i = 0; i < 25; i++) {
    if (network_card_CTL[i].flag == 0) { continue; }
    if (network_card_CTL[i].find()) {
      printk("Find --- %s\n", network_card_CTL[i].card_name);
      network_card_CTL[i].use = 1;
      network_card_CTL[i].init();
      extern u32 ip, gateway, submask, dns;

      ip      = 0xFFFFFFFF;
      gateway = 0xFFFFFFFF;
      submask = 0xFFFFFFFF;
      dns     = 0xFFFFFFFF;
      if (env_read("ip") == NULL) {
        dhcp_discovery(&mac0);
        while (gateway == 0xFFFFFFFF && submask == 0xFFFFFFFF && dns == 0xFFFFFFFF &&
               ip == 0xFFFFFFFF)
          ;

        char buf[100];
        sprintf(buf, "%x", ip);
        env_write("ip", buf);
        sprintf(buf, "%x", gateway);
        env_write("gateway", buf);
        sprintf(buf, "%x", submask);
        env_write("submask", buf);
        sprintf(buf, "%x", dns);
        env_write("dns", buf);

      } else {
        ip      = strtoul(env_read("ip"), NULL, 16);
        gateway = strtoul(env_read("gateway"), NULL, 16);
        submask = strtoul(env_read("submask"), NULL, 16);
        dns     = strtoul(env_read("dns"), NULL, 16);
      }

      for (u8 i = 1; i != 0; i++) {
        // printk("%d\n",i);
        IPParseMAC((ip & 0xffffff00) | i);
      }
      break;
    }
  }
}

void netcard_send(u8 *buffer, u32 size) {
  for (int i = 0; i < 25; i++) {
    if (network_card_CTL[i].use) {
      if (DriveSemaphoreTake(GetDriveCode("NETCARD_DRIVE"))) {
        // printk("Send....%s %d
        // %d\n",network_card_CTL[i].card_name,network_card_CTL[i].use,i);
        network_card_CTL[i].Send(buffer, size);
        DriveSemaphoreGive(GetDriveCode("NETCARD_DRIVE"));
        break;
      }
    }
  }
}

void Card_Recv_Handler(u8 *RawData) {
  struct EthernetFrame_head *header = (struct EthernetFrame_head *)(RawData);
  if (header->type == swap16(IP_PROTOCOL)) { // IP数据报
    struct IPV4Message *ipv4 = (struct IPV4Message *)(RawData + sizeof(struct EthernetFrame_head));
    if (ipv4->version == 4) {
      if ((swap16(ipv4->flagsAndOffset) >> IP_MF) & 1) {
        if (Find_IP_Packet(swap16(ipv4->ident)) == -1) {
          for (int i = 0; i != 16; i++) {
            if (IP_Packet_Base[i] == NULL) {
              IP_Packet_Base[i] =
                  (u8 *)malloc(swap16(ipv4->totalLength) + sizeof(struct EthernetFrame_head));
              memcpy((void *)IP_Packet_Base[i], RawData,
                     swap16(ipv4->totalLength) + sizeof(struct EthernetFrame_head));
              break;
            }
          }
        } else {
          IP_Assembling(ipv4, RawData);
        }
      } else if (!((swap16(ipv4->flagsAndOffset) >> IP_MF) & 1)) {
        u32   i_p  = Find_IP_Packet(swap16(ipv4->ident));
        void *base = RawData;
        if (i_p != -1) {
          IP_Assembling(ipv4, RawData);
          base = (void *)IP_Packet_Base[i_p];
        }
        if (ipv4->protocol == ICMP_PROTOCOL) { // ICMP
          icmp_handler(base);
        } else if (ipv4->protocol == UDP_PROTOCOL) { // UDP
          udp_handler(base);
        } else if (ipv4->protocol == TCP_PROTOCOL) { // TCP
          tcp_handler(base);
        }
        if (i_p != -1) {
          free((void *)IP_Packet_Base[i_p]);
          IP_Packet_Base[i_p] = NULL;
        }
      }
    }
  } else if (header->type == swap16(ARP_PROTOCOL)) { // ARP
    arp_handler(RawData);
  }
}

void init_network() {
  // printk("init_network\n");
  network_card nc;
  strcpy(nc.card_name, "pcnet");
  nc.find = pcnet_find_card;
  nc.flag = 1;
  nc.init = init_pcnet_card;
  nc.Send = PcnetSend;
  nc.use  = 0;
  register_network_card(nc);
  network_card rtl8139;
  strcpy(rtl8139.card_name, "rtl8139");
  rtl8139.find = rtl8139_find_card;
  rtl8139.flag = 1;
  rtl8139.init = init_rtl8139_card;
  rtl8139.Send = Rtl8139Send;
  rtl8139.use  = 0;
  register_network_card(rtl8139);
}