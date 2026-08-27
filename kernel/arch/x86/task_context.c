#include <arch.h>
#include <string.h>

void arch_task_context_init(arch_task_context_t *context, uintptr_t entry) {
  memset(context, 0, sizeof(*context));
  context->eip = (uint32_t)entry;
}
