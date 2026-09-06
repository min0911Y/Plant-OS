#ifndef KERNEL_RPC_H
#define KERNEL_RPC_H
#include "../../apps/include/rpc_wire.h"

// Synchronous kernel client. Replies bypass the application's IPC inbox.
// Buffers must be kernel memory; timeout_ms must be nonzero.
int rpc_call(const rpc_endpoint_t *server, unsigned opcode, const void *arg,
             unsigned arg_len, void *ret, unsigned ret_cap, unsigned *ret_len,
             unsigned timeout_ms);
#endif
