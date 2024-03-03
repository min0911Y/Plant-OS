#include <syscall.h>
#include <arg.h>
#include <stdio.h>
#undef bool
#undef true
#undef false
int main(int argc,char **argv);
extern "C" void init_env();
extern "C" void init_mem();
extern "C" void init_float();
extern "C" void Main()
{
  // 初始化stdio stderr
  init_mem();
  stdout = (FILE *)malloc(sizeof(FILE));
  stdin = (FILE *)malloc(sizeof(FILE));
  stderr = (FILE *)malloc(sizeof(FILE));
  stdout->buffer = (unsigned char *)NULL;
  stdout->mode = WRITE;
  stderr->buffer = (unsigned char *)NULL;
  stderr->mode = WRITE;
  stdin->buffer = (unsigned char *)malloc(1024);
  stdin->fileSize = -1;
  stdin->bufferSize = 1024;
  stdin->p = 0;
  stdin->mode = READ;
  char *buf = (char *)malloc(1024);
  char **argv;
  GetCmdline(buf);
  init_env();
  argv = (char **)malloc(sizeof(char *) * (get_argc(buf) + 1));
  for (int i = 0; i < get_argc(buf); i++) {
    argv[i] = (char *)malloc(128);
  }
  for (int i = 0; i < get_argc(buf); i++) {
    get_arg(argv[i], buf, i);
  }
  init_float();

  exit(main(get_argc(buf), argv));
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
    return malloc(size);
}
 
void *operator new[](size_t size)
{
    return malloc(size);
}
 
void operator delete(void *p)
{
    free(p);
}
 
void operator delete[](void *p)
{
    free(p);
}