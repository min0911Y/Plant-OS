#include <native_thread.h>

intptr_t thread_syscall(unsigned operation, native_thread_request_t *request,
                        size_t size);

intptr_t native_thread_call(unsigned operation,
                            native_thread_request_t *request) {
  return thread_syscall(operation, request, sizeof(*request));
}
