#ifndef PLANT_OS_USB_H
#define PLANT_OS_USB_H

#include <ctypes.h>

typedef enum {
  USB_SPEED_UNKNOWN,
  USB_SPEED_LOW,
  USB_SPEED_FULL,
  USB_SPEED_HIGH,
  USB_SPEED_SUPER,
} usb_speed_t;

enum {
  USB_REQUEST_GET_DESCRIPTOR = 6,
  USB_DESCRIPTOR_DEVICE = 1,
  USB_DESCRIPTOR_CONFIGURATION = 2,
  USB_DESCRIPTOR_INTERFACE = 4,
  USB_DESCRIPTOR_ENDPOINT = 5,
  USB_DESCRIPTOR_HID = 0x21,
  USB_DESCRIPTOR_REPORT = 0x22,
  USB_DESCRIPTOR_SS_ENDPOINT = 48,
  USB_DIRECTION_IN = 0x80,
  USB_ENDPOINT_BULK = 2,
  USB_ENDPOINT_INTERRUPT = 3,
  USB_ERROR_IO = -1,
  USB_ERROR_STALL = -2,
  USB_ERROR_DISCONNECTED = -3,
};

typedef struct __attribute__((packed)) {
  uint8_t type, request;
  uint16_t value, index, length;
} usb_setup_t;

typedef struct __attribute__((packed)) {
  uint8_t length, type;
  uint16_t usb_version;
  uint8_t device_class, subclass, protocol, max_packet_size;
  uint16_t vendor, product, device_version;
  uint8_t manufacturer, product_string, serial_number, configurations;
} usb_device_descriptor_t;

typedef struct usb_device usb_device_t;
typedef struct usb_interface usb_interface_t;
typedef struct {
  usb_device_t *hub, *device; /* NULL hub denotes a host root port. */
  uint8_t number;
  bool dirty, attempted;
} usb_port_t;
typedef struct {
  uint8_t address, type, interval, burst, mult;
  uint16_t packet_size, bytes_per_interval;
} usb_endpoint_t;

/* IRQ context: data is borrowed until return; no allocation or waiting. */
typedef bool (*usb_interrupt_callback_t)(void *context, const uint8_t *data,
                                         unsigned length);

typedef struct {
  /* Control returns the actual byte count, or a negative failure status.
   * Enumeration and these callbacks run in the host's service task. */
  int (*control)(usb_device_t *device, const usb_setup_t *setup, void *data);
  bool (*set_packet_size)(usb_device_t *device, uint16_t size);
  bool (*configure)(usb_device_t *device, const usb_endpoint_t *endpoints,
                    unsigned count);
  int (*transfer)(usb_device_t *device, uint8_t endpoint, void *data,
                  unsigned length);
  bool (*interrupt)(usb_device_t *device, uint8_t endpoint,
                    usb_interrupt_callback_t callback, void *context);
  bool (*clear_halt)(usb_device_t *device, uint8_t endpoint);
  bool (*hub)(usb_device_t *device, unsigned ports, unsigned think_time,
              bool multi_tt);
  bool (*attach)(usb_port_t *port, usb_speed_t speed);
  void (*detach)(usb_device_t *device);
} usb_host_ops_t;

struct usb_interface {
  usb_interface_t *next;
  usb_device_t *device;
  void *data;
  void (*detach)(usb_interface_t *interface);
  const uint8_t *descriptors;
  unsigned length;
  uint8_t number, class_code, subclass, protocol;
};

struct usb_device {
  const usb_host_ops_t *ops;
  usb_port_t *port;
  usb_speed_t speed;
  usb_device_descriptor_t descriptor;
  uint8_t *configuration;
  uint16_t configuration_size;
  usb_interface_t *interfaces;
  bool connected;
};

typedef struct usb_work {
  struct usb_work *next;
  void (*execute)(struct usb_work *work);
  void (*destroy)(struct usb_work *work);
  uint32_t tid, generation;
  enum { USB_WORK_QUEUED, USB_WORK_ACTIVE, USB_WORK_DONE } state;
} usb_work_t;

_Static_assert(sizeof(usb_setup_t) == 8, "USB setup packet layout");
_Static_assert(sizeof(usb_device_descriptor_t) == 18,
               "USB device descriptor layout");

bool usb_enumerate(usb_device_t *device);
bool usb_connected(const usb_device_t *device);
bool usb_bind(usb_device_t *device);
void usb_disconnect(usb_device_t *device);
bool usb_hid_bind(usb_interface_t *interface);
bool usb_storage_bind(usb_interface_t *interface);
bool usb_hub_bind(usb_interface_t *interface);
bool usb_hub_service(void);
bool usb_hub_pending(void);
void usb_work_submit(usb_work_t *work);
void usb_work_release(usb_work_t *work);
void usb_cancel_task(uint32_t tid, uint32_t generation);
void usb_service_register(uint32_t tid);
bool usb_service_wake(void);
bool usb_service_work(void);
bool usb_work_pending(void);
void xhci_initialize(void);

/* Service/task context only: always serial, also screen with USB_DEBUG=1. */
void usb_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef KERNEL_USB_DEBUG
typedef struct {
  uint16_t vendor, product;
  unsigned interface, sequence, length, id, expected, changes;
  bool valid, wake, pointer;
} usb_input_diagnostic_t;

/* Copies a bounded trace in IRQ context; the USB service prints it later. */
bool usb_debug_input(const usb_input_diagnostic_t *event);
#endif

#endif
