#include <arch.h>
#include <dos.h>
void KernelMain(void) {
  arch_boot_verify();
  sysinit();
  for (;;)
    ;
}
