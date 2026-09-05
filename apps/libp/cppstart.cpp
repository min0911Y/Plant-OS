#include "runtime_lifecycle.h"
#include <runtime_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#undef bool
#undef true
#undef false
int main(int argc,char **argv);
extern "C" void init_env();
extern "C" void init_mem();
extern "C" void init_float();
extern "C" void return_to_app();
extern "C" void set_rt(uintptr_t rt);
extern "C" void Main()
{
  set_rt((uintptr_t)return_to_app);
  init_mem();
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
  init_env();
  init_float();
  runtime_initialize_static();
  int status = main(arguments.argc, arguments.argv);
  runtime_arguments_destroy(&arguments);
  exit(status);
}
extern "C" void __main()
{
    //莫名其妙的错误
}
void __chkstk_ms()
{
    //莫名其妙的错误
}

extern "C" void __cxa_pure_virtual() { abort(); }

void *operator new(size_t size)
{
    void *p = malloc(size);
 //   logkf("mother fucker %d %p\n",size,p);
    return p;
}
 
void *operator new[](size_t size)
{
    void *p = malloc(size);
  //  logkf("mother fucker %d %p\n",size,p);
    return p;
}

void operator delete(void *p, size_t size) {
  (void)size;
  free(p);
}

void operator delete[](void *p, size_t size) {
  (void)size;
  free(p);
}
void operator delete(void *p)
{
    free(p);
}
 
void operator delete[](void *p)
{
    free(p);
}

extern "C" void __gxx_personality_v0()
{
  exit (-1);
}
