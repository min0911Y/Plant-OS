#include "syscall_internal.h"
#include <framebuffer.h>
int framebuffer_info(framebuffer_info_t *info) {
  return libp_syscall3(0x20, 6, (uintptr_t)info, 0);
}
