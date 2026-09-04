#include <dos.h>
#include <platform.h>

void platform_early_initialize(void) {
  init_pic();
  init_pit();
  init_acpi();
  apic_init();
  smp_topology_init();
}
