#ifndef KERNEL_USER_THREAD_H
#define KERNEL_USER_THREAD_H

#include "../../apps/include/native_thread.h"

struct mtask;
int user_thread_create(native_thread_request_t *request);
int user_thread_set_pointer(const native_thread_request_t *request);
int user_thread_get_stack(native_thread_request_t *request);
void user_thread_release(struct mtask *task);
int task_join_thread(uint32_t tid, uint32_t generation);
int task_detach_thread(uint32_t tid, uint32_t generation);
int task_terminate_thread(uint32_t tid, uint32_t generation);
void task_wait_threads(void);
void task_exit_process(unsigned status) __attribute__((noreturn));

#endif
