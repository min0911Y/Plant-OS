#include "runtime_lifecycle.h"
#include <ctypes.h>
#include <math.h>
#include <runtime_args.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

int main(int argc, char **argv);
void abi_alloc_init();
void return_to_app();
void set_rt(uintptr_t rt);
void init_float();

void Main(const runtime_linker_t *linker) {
  if (runtime_thread_initialize(linker))
    _exit((unsigned)-1);
  set_rt((uintptr_t)return_to_app);
  abi_alloc_init();
  stdio_initialize();
  if (stdout == NULL || stdin == NULL || stderr == NULL) {
    stdio_shutdown();
    exit((unsigned)-1);
  }

  runtime_arguments_t arguments;
  if (runtime_arguments_load(&arguments) != 0) {
    stdio_shutdown();
    exit((unsigned)-1);
  }
  init_float();
  runtime_initialize(linker);
  int status = main(arguments.argc, arguments.argv);
  runtime_arguments_destroy(&arguments);
  exit(status);
}

void __main() {}
void __chkstk_ms() {}
