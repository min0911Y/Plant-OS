#include <arch/x86/control.h>
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <page_fault.h>

typedef bool (*x86_exception_handler_t)(x86_exception_frame_t *frame);

typedef enum {
  X86_EXCEPTION_USER_TERMINATE,
  X86_EXCEPTION_ALWAYS_FATAL,
} x86_exception_policy_t;

typedef struct {
  const char *name;
  x86_exception_policy_t policy;
  x86_exception_handler_t handler;
} x86_exception_descriptor_t;

static bool x86_exception_handle_device_not_available(
    x86_exception_frame_t *frame) {
  (void)frame;
  x86_fpu_handle_device_not_available(current_task());
  return true;
}

static bool x86_exception_handle_page_fault(x86_exception_frame_t *frame) {
  return page_fault_try_resolve(x86_cr2_read(), frame->error);
}

static const x86_exception_descriptor_t
    exception_descriptors[X86_EXCEPTION_COUNT] = {
        [0] = {"#DE Divide Error", X86_EXCEPTION_USER_TERMINATE, NULL},
        [1] = {"#DB Debug", X86_EXCEPTION_USER_TERMINATE, NULL},
        [2] = {"#NMI Non-maskable Interrupt", X86_EXCEPTION_ALWAYS_FATAL,
               NULL},
        [3] = {"#BP Breakpoint", X86_EXCEPTION_USER_TERMINATE, NULL},
        [4] = {"#OF Overflow", X86_EXCEPTION_USER_TERMINATE, NULL},
        [5] = {"#BR Bound Range Exceeded", X86_EXCEPTION_USER_TERMINATE,
               NULL},
        [6] = {"#UD Invalid Opcode", X86_EXCEPTION_USER_TERMINATE, NULL},
        [7] = {"#NM Device Not Available", X86_EXCEPTION_USER_TERMINATE,
               x86_exception_handle_device_not_available},
        [8] = {"#DF Double Fault", X86_EXCEPTION_ALWAYS_FATAL, NULL},
        [9] = {"Coprocessor Segment Overrun", X86_EXCEPTION_USER_TERMINATE,
               NULL},
        [10] = {"#TS Invalid TSS", X86_EXCEPTION_USER_TERMINATE, NULL},
        [11] = {"#NP Segment Not Present", X86_EXCEPTION_USER_TERMINATE,
                NULL},
        [12] = {"#SS Stack-Segment Fault", X86_EXCEPTION_USER_TERMINATE,
                NULL},
        [13] = {"#GP General Protection", X86_EXCEPTION_USER_TERMINATE,
                NULL},
        [14] = {"#PF Page Fault", X86_EXCEPTION_USER_TERMINATE,
                x86_exception_handle_page_fault},
        [15] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [16] = {"#MF x87 Floating-Point", X86_EXCEPTION_USER_TERMINATE, NULL},
        [17] = {"#AC Alignment Check", X86_EXCEPTION_USER_TERMINATE, NULL},
        [18] = {"#MC Machine Check", X86_EXCEPTION_ALWAYS_FATAL, NULL},
        [19] = {"#XM SIMD Floating-Point", X86_EXCEPTION_USER_TERMINATE, NULL},
        [20] = {"#VE Virtualization", X86_EXCEPTION_USER_TERMINATE, NULL},
        [21] = {"#CP Control Protection", X86_EXCEPTION_USER_TERMINATE, NULL},
        [22] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [23] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [24] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [25] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [26] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [27] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
        [28] = {"#HV Hypervisor Injection", X86_EXCEPTION_USER_TERMINATE,
                NULL},
        [29] = {"#VC VMM Communication", X86_EXCEPTION_USER_TERMINATE, NULL},
        [30] = {"#SX Security", X86_EXCEPTION_USER_TERMINATE, NULL},
        [31] = {"Reserved", X86_EXCEPTION_USER_TERMINATE, NULL},
};

static __attribute__((noreturn)) void
x86_exception_fail_stop(const x86_exception_frame_t *frame,
                        const x86_exception_descriptor_t *descriptor) {
  Panic_K("x86 exception vector=%d name=%s error=%08x eip=%08x cs=%08x "
          "tid=%d",
          frame->vector, descriptor->name, frame->error, frame->eip, frame->cs,
          current_task()->tid);
  for (;;) {
    asm volatile("cli\n\thlt" : : : "memory");
  }
}

void x86_exception_dispatch(x86_exception_frame_t *frame) {
  if (frame == NULL || frame->vector >= X86_EXCEPTION_COUNT) {
    Panic_K("invalid x86 exception frame");
    for (;;) {
      asm volatile("cli\n\thlt" : : : "memory");
    }
  }

  const x86_exception_descriptor_t *descriptor =
      &exception_descriptors[frame->vector];
  if (descriptor->handler != NULL && descriptor->handler(frame)) {
    return;
  }
  if (descriptor->policy == X86_EXCEPTION_USER_TERMINATE &&
      (frame->cs & 3u) == 3u) {
    logk("exception: vector=%d name=%s error=%08x eip=%08x cr2=%08x tid=%d\n",
         frame->vector, descriptor->name, frame->error, frame->eip,
         frame->vector == 14 ? x86_cr2_read() : 0, current_task()->tid);
    task_exit((unsigned)-1);
  }
  x86_exception_fail_stop(frame, descriptor);
}
