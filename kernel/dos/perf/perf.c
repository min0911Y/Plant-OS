#include <dos.h>
#include <irq.h>
#include <limits.h>
#include <perf.h>

#ifdef KERNEL_PERF

#ifndef KERNEL_PERF_STACK_CAPACITY
#define KERNEL_PERF_STACK_CAPACITY 4096
#endif

enum {
  PERF_STACK_CAPACITY = KERNEL_PERF_STACK_CAPACITY,
  /* Bound open-addressing probes in interrupt context. */
  PERF_STACK_LIMIT = PERF_STACK_CAPACITY * 3 / 4,
  PERF_STACK_MAX_DEPTH = 16,
};

_Static_assert((PERF_STACK_CAPACITY & (PERF_STACK_CAPACITY - 1)) == 0,
               "perf stack table capacity must be a power of two");
_Static_assert(PERF_STACK_CAPACITY >= 4,
               "perf stack table capacity is too small");

typedef enum {
  PERF_SAMPLE_KERNEL,
  PERF_SAMPLE_USER,
} perf_sample_kind_t;

typedef struct {
  uint32_t count;
  uint32_t tid;
  uint32_t generation;
  uint8_t cpu;
  uint8_t kind;
  uint8_t depth;
  uint8_t reserved;
  uintptr_t pc[PERF_STACK_MAX_DEPTH];
} perf_stack_t;

static volatile perf_state_t perf_state = PERF_STATE_IDLE;
static perf_session_t perf_session;
static uint32_t perf_sample_count;
static uint32_t perf_stack_count;
static uint32_t perf_dropped;
static perf_stack_t perf_stacks[PERF_STACK_CAPACITY];

extern uint8_t __kernel_text_start[];
extern uint8_t __kernel_text_end[];

static bool perf_kernel_text_address(uintptr_t address) {
  return address >= (uintptr_t)__kernel_text_start &&
         address < (uintptr_t)__kernel_text_end;
}

static bool perf_stack_frame_address(uintptr_t address, uintptr_t stack_top) {
  if (stack_top < TASK_KERNEL_STACK_SIZE ||
      (address & (sizeof(uintptr_t) - 1)) != 0) {
    return false;
  }
  uintptr_t stack_bottom = stack_top - TASK_KERNEL_STACK_SIZE;
  return address >= stack_bottom &&
         address <= stack_top - 2 * sizeof(uintptr_t);
}

static void perf_unwind(perf_stack_t *stack, uintptr_t frame_pointer,
                        uintptr_t stack_top) {
  while (stack->depth < PERF_STACK_MAX_DEPTH &&
         perf_stack_frame_address(frame_pointer, stack_top)) {
    const uintptr_t *frame = (const uintptr_t *)frame_pointer;
    uintptr_t next = frame[0];
    uintptr_t return_address = frame[1];

    if (!perf_kernel_text_address(return_address)) {
      return;
    }
    /* A return address names the instruction before the saved continuation. */
    stack->pc[stack->depth++] = return_address - 1;
    if (next <= frame_pointer) {
      return;
    }
    frame_pointer = next;
  }
}

static uint32_t perf_stack_hash(const perf_stack_t *stack) {
  uint32_t hash = 2166136261u;
  const uint32_t metadata[] = {stack->tid, stack->generation, stack->cpu,
                               stack->kind, stack->depth};

  for (unsigned i = 0; i < sizeof(metadata) / sizeof(metadata[0]); i++) {
    hash = (hash ^ metadata[i]) * 16777619u;
  }
  for (uint32_t i = 0; i < stack->depth; i++) {
    uintptr_t address = stack->pc[i];
    hash = (hash ^ (uint32_t)address) * 16777619u;
#if __SIZEOF_POINTER__ > 4
    hash = (hash ^ (uint32_t)(address >> 32)) * 16777619u;
#endif
  }
  return hash;
}

static bool perf_stack_equal(const perf_stack_t *left,
                             const perf_stack_t *right) {
  if (left->tid != right->tid || left->generation != right->generation ||
      left->cpu != right->cpu || left->kind != right->kind ||
      left->depth != right->depth) {
    return false;
  }
  for (uint32_t i = 0; i < left->depth; i++) {
    if (left->pc[i] != right->pc[i]) {
      return false;
    }
  }
  return true;
}

static void perf_record(const perf_stack_t *sample) {
  if (perf_sample_count == UINT_MAX) {
    perf_dropped++;
    return;
  }
  uint32_t index = perf_stack_hash(sample) & (PERF_STACK_CAPACITY - 1);

  for (uint32_t probes = 0; probes < PERF_STACK_CAPACITY; probes++) {
    perf_stack_t *stack = &perf_stacks[index];
    if (stack->count == 0) {
      if (perf_stack_count >= PERF_STACK_LIMIT) {
        perf_dropped++;
        return;
      }
      *stack = *sample;
      stack->count = 1;
      perf_stack_count++;
      perf_sample_count++;
      return;
    }
    if (perf_stack_equal(stack, sample)) {
      stack->count++;
      perf_sample_count++;
      return;
    }
    index = (index + 1) & (PERF_STACK_CAPACITY - 1);
  }
  perf_dropped++;
}

static void perf_puts(const char *string) {
  while (*string != '\0') {
    write_serial(*string++);
  }
}

static void perf_put_address(uintptr_t value) {
  static const char hex[] = "0123456789abcdef";

  perf_puts("0x");
  for (int shift = (int)(sizeof(value) * 8) - 4; shift >= 0; shift -= 4) {
    write_serial(hex[(value >> shift) & 0xf]);
  }
}

static void perf_put_u32(uint32_t value) {
  char digits[10];
  unsigned length = 0;

  do {
    digits[length++] = '0' + value % 10;
    value /= 10;
  } while (value != 0);
  while (length != 0) {
    write_serial(digits[--length]);
  }
}

static void perf_dump_stack(const perf_stack_t *stack) {
  write_serial(stack->kind == PERF_SAMPLE_KERNEL ? 'K' : 'U');
  write_serial(' ');
  perf_put_u32(stack->count);
  write_serial(' ');
  perf_put_u32(stack->cpu);
  write_serial(' ');
  perf_put_u32(stack->tid);
  write_serial(' ');
  perf_put_u32(stack->generation);
  write_serial(' ');
  perf_put_u32(stack->depth);
  for (uint32_t i = 0; i < stack->depth; i++) {
    write_serial(' ');
    perf_put_address(stack->pc[i]);
  }
  write_serial('\n');
}

void perf_get_status(perf_status_t *status) {
  if (status == NULL) {
    return;
  }
  irq_state_t interrupt_state = irq_save();
  status->state = perf_state;
  status->samples = perf_sample_count;
  status->stacks = perf_stack_count;
  status->dropped = perf_dropped;
  status->capacity = PERF_STACK_LIMIT;
  irq_restore(interrupt_state);
}

int perf_start(perf_session_t session) {
  irq_state_t interrupt_state = irq_save();
  if (perf_state == PERF_STATE_RUNNING || perf_state == PERF_STATE_DUMPING) {
    irq_restore(interrupt_state);
    return PERF_ERR_STATE;
  }
  memset(perf_stacks, 0, sizeof(perf_stacks));
  perf_sample_count = 0;
  perf_stack_count = 0;
  perf_dropped = 0;
  perf_session = session;
  perf_state = PERF_STATE_RUNNING;
  irq_restore(interrupt_state);
  return PERF_OK;
}

void perf_sample(uintptr_t instruction_pointer, uintptr_t frame_pointer,
                 bool user_mode) {
  if (perf_state != PERF_STATE_RUNNING) {
    return;
  }

  mtask *task = current_task();
  bool valid_task = task != NULL && task->tid != NULL_TID;
  perf_stack_t sample = {
      .tid = valid_task ? task->tid : UINT_MAX,
      .generation = valid_task ? task->generation : 0,
      .cpu = (uint8_t)(valid_task ? task->cpu : smp_current_cpu()),
      .kind = user_mode ? PERF_SAMPLE_USER : PERF_SAMPLE_KERNEL,
  };

  if (!user_mode) {
    sample.pc[sample.depth++] = instruction_pointer;
    if (valid_task && task->top != 0) {
      perf_unwind(&sample, frame_pointer, task->top);
    }
  }
  perf_record(&sample);
}

int perf_stop_and_dump(const char *reason) {
  irq_state_t interrupt_state = irq_save();
  if (perf_state != PERF_STATE_RUNNING) {
    irq_restore(interrupt_state);
    return PERF_ERR_STATE;
  }
  perf_session_t session = perf_session;
  perf_state = PERF_STATE_DUMPING;
  irq_restore(interrupt_state);

  perf_puts("\nPERF_BEGIN version=2 session=");
  perf_puts(session == PERF_SESSION_BOOT ? "boot" : "manual");
  if (reason != NULL && *reason != '\0') {
    perf_puts(" reason=");
    perf_puts(reason);
  }
  perf_puts(" samples=");
  perf_put_u32(perf_sample_count);
  perf_puts(" stacks=");
  perf_put_u32(perf_stack_count);
  perf_puts(" dropped=");
  perf_put_u32(perf_dropped);
  perf_puts(" text_start=");
  perf_put_address((uintptr_t)__kernel_text_start);
  perf_puts(" text_end=");
  perf_put_address((uintptr_t)__kernel_text_end);
  write_serial('\n');

  for (uint32_t i = 0; i < PERF_STACK_CAPACITY; i++) {
    if (perf_stacks[i].count != 0) {
      perf_dump_stack(&perf_stacks[i]);
    }
  }
  perf_puts("PERF_END\n");

  interrupt_state = irq_save();
  perf_state = PERF_STATE_IDLE;
  irq_restore(interrupt_state);
  return PERF_OK;
}

#else

void perf_get_status(perf_status_t *status) {
  if (status == NULL) {
    return;
  }
  *status = (perf_status_t){.state = PERF_STATE_UNAVAILABLE};
}

int perf_start(perf_session_t session) {
  (void)session;
  return PERF_ERR_UNAVAILABLE;
}

void perf_sample(uintptr_t instruction_pointer, uintptr_t frame_pointer,
                 bool user_mode) {
  (void)instruction_pointer;
  (void)frame_pointer;
  (void)user_mode;
}

int perf_stop_and_dump(const char *reason) {
  (void)reason;
  return PERF_ERR_UNAVAILABLE;
}

#endif
