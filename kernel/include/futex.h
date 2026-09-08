#ifndef KERNEL_FUTEX_H
#define KERNEL_FUTEX_H

#include "../../apps/include/futex.h"

struct mtask;
int futex_operation(unsigned operation, const futex_request_t *request);
void futex_tick(void);
void futex_cancel_task(struct mtask *task);

#endif
