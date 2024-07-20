#include <dos.h>
// IPV4
static u16 ident = 0;
void IPV4ProviderSend(u8 protocol, u64 dest_mac, u32 dest_ip, u32 src_ip, u8 *data, u32 size) {
  struct IPV4Message *res = (struct IPV4Message *)page_malloc(sizeof(struct IPV4Message) + size);
  u8                 *dat = (u8 *)res;
  memcpy(dat + sizeof(struct IPV4Message), data, size);
  res->version      = 4;
  res->headerLength = sizeof(struct IPV4Message) / 4;
  res->tos          = 0;
  res->ident        = ident;
  res->timeToLive   = 64;
  res->protocol     = protocol;
  res->dstIP        = ((dest_ip << 24) & 0xff000000) | ((dest_ip << 8) & 0x00ff0000) |
               ((dest_ip >> 8) & 0xff00) | ((dest_ip >> 24) & 0xff);
  res->srcIP = ((src_ip << 24) & 0xff000000) | ((src_ip << 8) & 0x00ff0000) |
               ((src_ip >> 8) & 0xff00) | ((src_ip >> 24) & 0xff);
  if (sizeof(struct IPV4Message) + size <= MTU) {
    res->totalLength    = swap16(sizeof(struct IPV4Message) + size);
    res->flagsAndOffset = 0;
    res->checkSum       = 0;
    res->checkSum       = CheckSum((u16 *)dat, sizeof(struct IPV4Message));
    ether_frame_provider_send(dest_mac, 0x0800, dat, sizeof(struct IPV4Message) + size);
  } else {
    int offset = 0;
    u8 *dat1   = (u8 *)malloc(MTU);
    for (int i = 0; i * (MTU - sizeof(struct IPV4Message)) <= size; i++) {
      if (i * (MTU - sizeof(struct IPV4Message)) >= size - (MTU - sizeof(struct IPV4Message))) {
        res->totalLength =
            swap16(size - i * (MTU - sizeof(struct IPV4Message)) + sizeof(struct IPV4Message));
        res->flagsAndOffset = offset << IP_OFFSET;
        res->flagsAndOffset = swap16(res->flagsAndOffset);
        res->checkSum       = 0;
        res->checkSum       = CheckSum((u16 *)dat, sizeof(struct IPV4Message));
        memcpy((void *)dat1, (void *)res, sizeof(struct IPV4Message));
        memcpy((void *)(dat1 + sizeof(struct IPV4Message)),
               (void *)(data + i * (MTU - sizeof(struct IPV4Message))),
               size - i * (MTU - sizeof(struct IPV4Message)));
        // printk("ip:%08x,%08x
        // size:%d\nMF:0\noffset:%d\n",swap32(res->srcIP),swap32(res->dstIP),swap16(res->totalLength),(swap16(res->flagsAndOffset)
        // >> 3));
        ether_frame_provider_send(dest_mac, 0x0800, dat1,
                                  size - i * (MTU - sizeof(struct IPV4Message)) +
                                      sizeof(struct IPV4Message));
      } else {
        res->totalLength    = swap16(MTU);
        res->flagsAndOffset = (offset << IP_OFFSET) | (1 << IP_MF);
        res->flagsAndOffset = swap16(res->flagsAndOffset);
        res->checkSum       = 0;
        res->checkSum       = CheckSum((u16 *)dat, sizeof(struct IPV4Message));
        memcpy((void *)dat1, (void *)res, sizeof(struct IPV4Message));
        memcpy((void *)(dat1 + sizeof(struct IPV4Message)),
               (void *)(data + i * (MTU - sizeof(struct IPV4Message))),
               MTU - sizeof(struct IPV4Message));
        // printk("ip:%08x,%08x
        // size:%d\nMF:1\noffset:%d\n",swap32(res->srcIP),swap32(res->dstIP),swap16(res->totalLength),(swap16(res->flagsAndOffset)
        // >> 3));
        ether_frame_provider_send(dest_mac, 0x0800, dat1, MTU);
      }
      offset += (MTU - sizeof(struct IPV4Message)) / 8;
    }
    free((void *)dat1);
  }
  page_free(dat, sizeof(struct IPV4Message) + size);
  ident++;
  return;
}
u16 CheckSum(u16 *data, u32 size) {
  u32 tmp = 0;
  for (int i = 0; i < size / 2; i++) {
    tmp += ((data[i] & 0xff00) >> 8) | ((data[i] & 0x00ff) << 8);
  }
  if (size % 2) tmp += ((u16)((char *)data)[size - 1]) << 8;
  while (tmp & 0xffff0000)
    tmp = (tmp & 0xffff) + (tmp >> 16);
  return ((~tmp & 0xff00) >> 8) | ((~tmp & 0x00ff) << 8);
}
u32 IP2UINT32_T(u8 *ip) {
  u8 ip0, ip1, ip2, ip3;
  ip0  = strtol(ip, '.', 10);
  u8 t = ip0;
  while (t >= 10) {
    t /= 10;
    ip++;
  }
  ip1 = strtol(ip + 2, '.', 10);
  t   = ip1;
  while (t >= 10) {
    t /= 10;
    ip++;
  }
  ip2 = strtol(ip + 4, '.', 10);
  t   = ip2;
  while (t >= 10) {
    t /= 10;
    ip++;
  }
  ip3 = strtol(ip + 6, NULL, 10);
  return (u32)((ip0 << 24) | (ip1 << 16) | (ip2 << 8) | ip3);
}
