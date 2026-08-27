#include <ctypes.h>
#include <math.h>
#include <runtime_args.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

int main(int argc, char **argv);
void init_env();
void init_mem();
void return_to_app();
void set_rt(uintptr_t rt);
void init_float();

void Main() {
  set_rt((uintptr_t)return_to_app);
  init_mem();
  stdout = (FILE *)malloc(sizeof(FILE));
  stdin = (FILE *)malloc(sizeof(FILE));
  stderr = (FILE *)malloc(sizeof(FILE));
  if (stdout == NULL || stdin == NULL || stderr == NULL) {
    free(stdout);
    free(stdin);
    free(stderr);
    exit((unsigned)-1);
  }
  memset(stdout, 0, sizeof(FILE));
  memset(stdin, 0, sizeof(FILE));
  memset(stderr, 0, sizeof(FILE));
  stdout->mode = WRITE;
  stderr->mode = WRITE;
  stdin->fileSize = -1;
  stdin->mode = READ;

  runtime_arguments_t arguments;
  if (runtime_arguments_load(&arguments) != 0) {
    free(stdout);
    free(stdin);
    free(stderr);
    exit((unsigned)-1);
  }
  init_env();
  init_float();
  int status = main(arguments.argc, arguments.argv);
  runtime_arguments_destroy(&arguments);
  exit(status);
}

void __main() {}
void __chkstk_ms() {}
