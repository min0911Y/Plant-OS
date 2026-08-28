#ifndef KERNEL_NET_LINK_H
#define KERNEL_NET_LINK_H

#include <ctypes.h>

typedef bool (*net_link_receive_t)(const uint8_t *frame, uint16_t length);

bool net_link_start(net_link_receive_t receive, uint8_t mac[6],
                    const char **name);
int net_link_transmit(const uint8_t *frame, uint16_t length);

#endif
