#include <stdio.h>
#include <sys/stat.h>
#include <syscall.h>

typedef void (*exception_trigger_t)(void);

typedef struct {
  const char *name;
  exception_trigger_t trigger;
} exception_case_t;

enum {
  COW_PAGE_SIZE = 4096,
  COW_SENTINEL = 0xa5,
};

static volatile unsigned char supervisor_cow_page[COW_PAGE_SIZE]
    __attribute__((aligned(COW_PAGE_SIZE)));

static void trigger_divide_error(void) {
  unsigned divisor;
  unsigned dividend = 1;
  unsigned remainder = 0;
  asm volatile("xorl %0, %0" : "=r"(divisor) : : "cc");
  asm volatile("divl %2"
               : "+a"(dividend), "+d"(remainder)
               : "r"(divisor)
               : "cc", "memory");
}

static void trigger_invalid_opcode(void) {
  asm volatile("ud2" : : : "memory");
}

static void trigger_general_protection(void) {
  asm volatile("cli" : : : "memory");
}

static void trigger_page_fault(void) {
  uintptr_t address = 0x60000000u;
  unsigned value = 1;
  asm volatile("movl %1, (%0)" : : "r"(address), "r"(value) : "memory");
}

static int supervisor_write_cow_test(void) {
  struct stat file_status;
  int file_size =
      stat("sys.cfg", &file_status) == 0 ? (int)file_status.st_size : -1;
  if (file_size <= 0 || file_size > COW_PAGE_SIZE) {
    logkf("EXCEPTION_TEST supervisor COW invalid size=%d\n", file_size);
    return 0;
  }

  for (unsigned i = 0; i < COW_PAGE_SIZE; i++) {
    supervisor_cow_page[i] = COW_SENTINEL;
  }

  int child = fork();
  if (child < 0) {
    logkf("EXCEPTION_TEST supervisor COW fork failed\n");
    return 0;
  }
  if (child == 0) {
    FILE *stream = fopen("sys.cfg", "rb");
    if (stream == NULL ||
        fread((char *)supervisor_cow_page, 1, file_size, stream) !=
            (size_t)file_size) {
      exit(2);
    }
    fclose(stream);
    for (int i = 0; i < file_size; i++) {
      if (supervisor_cow_page[i] != COW_SENTINEL) {
        exit(0);
      }
    }
    exit(3);
  }

  int status = waittid((unsigned)child);
  int isolated = status == 0;
  for (unsigned i = 0; i < COW_PAGE_SIZE; i++) {
    if (supervisor_cow_page[i] != COW_SENTINEL) {
      isolated = 0;
      break;
    }
  }
  logkf("EXCEPTION_TEST supervisor COW status=%d size=%d isolated=%d\n",
        status, file_size, isolated);
  return isolated;
}

int signal_tests(void);

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  static const exception_case_t cases[] = {
      {"#DE", trigger_divide_error},
      {"#UD", trigger_invalid_opcode},
      {"#GP", trigger_general_protection},
      {"#PF", trigger_page_fault},
  };
  int checks = 0;
  int fails = 0;

  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int child = fork();
    if (child < 0) {
      checks++;
      fails++;
      logkf("EXCEPTION_TEST %s fork failed\n", cases[i].name);
      continue;
    }
    if (child == 0) {
      cases[i].trigger();
      exit(2);
    }

    int status = waittid((unsigned)child);
    checks++;
    if (status != -1) {
      fails++;
    }
    logkf("EXCEPTION_TEST %s status=%d expect=-1\n", cases[i].name,
          status);
  }

  checks++;
  if (!supervisor_write_cow_test()) {
    fails++;
  }

  printf("EXCEPTION_TEST done checks=%d fails=%d\n", checks, fails);
  logkf("EXCEPTION_TEST done checks=%d fails=%d\n", checks, fails);
  return fails + signal_tests();
}
