#include <dos.h>
#include <net_link.h>

bool pcnet_link_start(net_link_receive_t receive, uint8_t mac[6]);
int pcnet_link_transmit(const uint8_t *frame, uint16_t length);
bool rtl8139_link_start(net_link_receive_t receive, uint8_t mac[6]);
int rtl8139_link_transmit(const uint8_t *frame, uint16_t length);

typedef struct {
  const char *name;
  bool (*start)(net_link_receive_t receive, uint8_t mac[6]);
  int (*transmit)(const uint8_t *frame, uint16_t length);
} network_driver_t;

static const network_driver_t network_drivers[] = {
    {.name = "pcnet", .start = pcnet_link_start,
     .transmit = pcnet_link_transmit},
    {.name = "rtl8139", .start = rtl8139_link_start,
     .transmit = rtl8139_link_transmit},
};

static const network_driver_t *network_active;

bool net_link_start(net_link_receive_t receive, uint8_t mac[6],
                    const char **name) {
  if (receive == NULL || mac == NULL || network_active != NULL) {
    return false;
  }

  for (unsigned i = 0; i < sizeof(network_drivers) / sizeof(network_drivers[0]);
       i++) {
    const network_driver_t *driver = &network_drivers[i];
    if (!driver->start(receive, mac)) {
      logk("network: %s unavailable\n", driver->name);
      continue;
    }
    network_active = driver;
    if (name != NULL) {
      *name = driver->name;
    }
    return true;
  }
  return false;
}

int net_link_transmit(const uint8_t *frame, uint16_t length) {
  if (network_active == NULL || frame == NULL || length < 14 ||
      length > 1514) {
    return -1;
  }
  return network_active->transmit(frame, length);
}
