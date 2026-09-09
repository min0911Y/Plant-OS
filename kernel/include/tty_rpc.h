#ifndef KERNEL_TTY_RPC_H
#define KERNEL_TTY_RPC_H
#include "../../apps/include/tty_rpc.h"
#include <define.h>

struct tty *fartty_alloc(mtask *server, unsigned opcode, int xsize, int ysize);
struct tty *fartty_lookup(uintptr_t handle);
void fartty_task_cleanup(mtask *task);
int fartty_get_pointer(struct tty *tty, tty_pointer_t *pointer);
#endif
