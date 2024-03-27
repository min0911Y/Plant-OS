#include <arg.h>
#include <math.h>
#include <stdio.h>
#include <syscall.h>
int main(int argc, char **argv);
void init_env();
void init_mem();
void return_to_app();
void set_rt(unsigned rt);
void Main() {
  // 初始化stdio stderr
  set_rt(return_to_app);
  init_mem();
  stdout = malloc(sizeof(FILE));
  stdin = malloc(sizeof(FILE));
  stderr = malloc(sizeof(FILE));
  stdout->buffer = NULL;
  stdout->mode = WRITE;
  stderr->buffer = NULL;
  stderr->mode = WRITE;
  stdin->buffer = (char *)malloc(1024);
  stdin->fileSize = -1;
  stdin->bufferSize = 1024;
  stdin->p = 0;
  stdin->mode = READ;
  char *buf = (char *)malloc(1024);
  char **argv;
  GetCmdline(buf);
  init_env();
  argv = malloc(sizeof(char *) * (get_argc(buf) + 1));
  for (int i = 0; i < get_argc(buf); i++) {
    argv[i] = malloc(128);
  }
  for (int i = 0; i < get_argc(buf); i++) {
    get_arg(argv[i], buf, i);
  }
  init_float();

  exit(main(get_argc(buf), argv));
  while(1);
}
void __main() {
  //莫名其妙的错误
}
void __chkstk_ms() {
  //莫名其妙的错误
}
