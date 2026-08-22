#include <dos.h>
#include <perf.h>

#ifdef KERNEL_PERF

#define PERF_MAX_SAMPLES 4096
#define PERF_MAX_DEPTH 16
#define PERF_KERNEL_BASE 0x00280000u
#define PERF_KERNEL_LIMIT 0x04000000u
#define PERF_TASK_STACK_SIZE (1024u * 1024u)

typedef struct perf_sample_t {
  uint32_t tick;
  uint32_t tid;
  uint8_t user;
  uint8_t depth;
  uint32_t pc[PERF_MAX_DEPTH];
} perf_sample_t;

static volatile int perf_running;
static volatile int perf_dumped;
static uint32_t perf_dropped;
static uint32_t perf_sample_count;
static perf_sample_t perf_samples[PERF_MAX_SAMPLES];

static int perf_kernel_addr(uint32_t addr) {
  return addr >= PERF_KERNEL_BASE && addr < PERF_KERNEL_LIMIT;
}

static int perf_stack_addr(uint32_t addr, uint32_t top) {
  uint32_t bottom;

  if (top < PERF_TASK_STACK_SIZE) {
    return 0;
  }
  bottom = top - PERF_TASK_STACK_SIZE;
  return addr >= bottom && addr + 8 >= addr && addr + 8 <= top;
}

static void perf_puts(const char *s) {
  while (*s) {
    write_serial(*s++);
  }
}

static void perf_put_hex32(uint32_t value) {
  static const char hex[] = "0123456789abcdef";

  perf_puts("0x");
  for (int i = 28; i >= 0; i -= 4) {
    write_serial(hex[(value >> i) & 0xf]);
  }
}

static void perf_put_u32(uint32_t value) {
  char buf[11];
  int pos = 0;

  if (value == 0) {
    write_serial('0');
    return;
  }

  while (value != 0 && pos < (int)sizeof(buf)) {
    buf[pos++] = '0' + value % 10;
    value /= 10;
  }
  while (pos > 0) {
    write_serial(buf[--pos]);
  }
}

static void perf_unwind_ebp(perf_sample_t *sample, uint32_t ebp,
                            uint32_t stack_top) {
  uint32_t previous = ebp;

  while (sample->depth < PERF_MAX_DEPTH && perf_stack_addr(ebp, stack_top)) {
    uint32_t *frame = (uint32_t *)(uintptr_t)ebp;
    uint32_t next_ebp = frame[0];
    uint32_t ret = frame[1];

    if (!perf_kernel_addr(ret)) {
      break;
    }

    sample->pc[sample->depth++] = ret;

    if (next_ebp <= previous || next_ebp - previous > 0x10000) {
      break;
    }
    previous = next_ebp;
    ebp = next_ebp;
  }
}

void perf_boot_start(void) {
  int eflags = io_load_eflags();

  io_cli();
  perf_sample_count = 0;
  perf_dropped = 0;
  perf_dumped = 0;
  perf_running = 1;
  io_store_eflags(eflags);
}

void perf_sample_irq(const perf_irq_frame_t *frame) {
  uint32_t index;
  perf_sample_t *sample;
  mtask *task;

  if (!perf_running || perf_dumped || frame == NULL) {
    return;
  }

  index = perf_sample_count;
  if (index >= PERF_MAX_SAMPLES) {
    perf_dropped++;
    return;
  }
  perf_sample_count = index + 1;

  task = current_task();
  sample = &perf_samples[index];
  sample->tick = (uint32_t)timerctl.count;
  sample->tid = task ? (uint32_t)task->tid : 0xffffffffu;
  sample->user = ((frame->cs & 3) == 3);
  sample->depth = 0;

  if (perf_kernel_addr(frame->eip)) {
    sample->pc[sample->depth++] = frame->eip;
  } else {
    return;
  }

  if (task && task->top != 0) {
    perf_unwind_ebp(sample, frame->ebp, task->top);
  }
}

void perf_boot_stop_and_dump(const char *reason) {
  uint32_t count;

  if (perf_dumped) {
    return;
  }

  io_cli();
  perf_running = 0;
  perf_dumped = 1;
  count = perf_sample_count;

  perf_puts("\nPERF_BEGIN");
  if (reason) {
    perf_puts(" reason=");
    perf_puts(reason);
  }
  perf_puts(" samples=");
  perf_put_u32(count);
  perf_puts(" dropped=");
  perf_put_u32(perf_dropped);
  perf_puts("\n");

  for (uint32_t i = 0; i < count; i++) {
    perf_sample_t *sample = &perf_samples[i];

    perf_put_u32(sample->tick);
    write_serial(' ');
    perf_put_u32(sample->tid);
    write_serial(' ');
    perf_put_u32(sample->depth);
    for (uint32_t j = 0; j < sample->depth; j++) {
      write_serial(' ');
      perf_put_hex32(sample->pc[j]);
    }
    perf_puts("\n");
  }

  perf_puts("PERF_END\n");
}

#else

void perf_boot_start(void) {}

void perf_sample_irq(const perf_irq_frame_t *frame) {
  (void)frame;
}

void perf_boot_stop_and_dump(const char *reason) {
  (void)reason;
}

#endif
