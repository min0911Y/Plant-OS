#include <syscall.h>
#include <runtime_args.h>
#include <stdio.h>
#include <string.h>
#undef bool
#undef true
#undef false
int main(int argc,char **argv);
extern "C" void init_env();
extern "C" void init_mem();
extern "C" void init_float();
extern "C" void return_to_app();
extern "C" void set_rt(unsigned rt);
extern "C" void Main()
{
  set_rt((unsigned)return_to_app);
  // 初始化stdio stderr
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
  stdout->buffer = (unsigned char *)NULL;
  stdout->mode = WRITE;
  stderr->buffer = (unsigned char *)NULL;
  stderr->mode = WRITE;
  stdin->buffer = (unsigned char *)NULL;
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
extern "C" void __main()
{
    //莫名其妙的错误
}
void __chkstk_ms()
{
    //莫名其妙的错误
}

extern "C" void __cxa_pure_virtual()
{
    // Do nothing or print an error message.
}

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
 
void operator delete(void *p,unsigned int size)
{
    (void)size;
    free(p);
}
 
void operator delete[](void *p,unsigned int size)
{
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
