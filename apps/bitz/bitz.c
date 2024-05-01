#include <fcntl.h>
#include <net.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
int flag_m = 0;
int m_tid;
void do_test(unsigned eip);
void a() {
  while (1) {
    int len = MessageLength(m_tid);
    if (len == -1)
      continue;
    char *dat = (char *)malloc(len);
    GetMessage(dat, m_tid);
    dat[13] = 0;
    // printf("[Task a]Message has been recviced,\'%s\'\n",dat);
    flag_m = 1;
    free(dat);
  }
}
void test_signal() {
  printf("SIGINT!!!\n");
  exit(0);
}
void c(void *data) { printf("%08x\n", data); }
char *s;
int a111 = 0;
#define A(x) (x >> 24)
#define B(x) ((x >> 16) & 0xff)
#define C(x) ((x >> 8) & 0xff)
#define D(x) (x & 0xff)

void c1() {
  printf("C1\n");

  for(;;);
}
void b1() {
  AddThread("",c1,(unsigned)malloc(1024*512)+1024*512);
  printf("B1\n");

  for(;;);
}
int main(int argc, char **argv) {
  asm ("int $0x72");
  return 0;
  if(fork() == 0) {
    printf("P\n");
    sleep(1000);
    exit(0);
  } else {
    printf("S\n");
  }
  return 0;
  AddThread("",b1,(unsigned)malloc(1024*512)+1024*512);

  printf("MAIN\n");
  for(;;);
  return 0;
  unsigned IP;
  IP = GetIP();
  printf("%d.%d.%d.%d\n", A(IP), B(IP), C(IP), D(IP));
  socket_t s;
  s = Socket_Alloc(TCP_PROTOCOL);
  Socket_Init(s, 0, 0, IP, 2115);
  listen(s);
  printf("A Connect\n");
  char buf[512] = {0};
  while (1) {
    int a = Socket_Recv(s, buf, 512);
    if(a == 0) break;
    buf[a] = 0;
    printf("%s", buf);
  }
  Socket_Free(s);
  return 0;
  asm volatile("ud2");
  write((int)stdout, "hello", 5);
  return 0;
  unsigned int *v = set_mode(1024, 768);
  logkf("%08x\n", v);
  memset(v, 0xff, 1024 * 768 * 4);
  for (;;)
    ;
  return 0;
  do_test(c);
  for (;;)
    ;
  return 0;
  char *a[30000];
  void *b1;
  b1 = malloc(40);
  free(b1);
  printf("b = %p\n", b1);
  for (int i = 0; i < 30000; i++) {
    a[i] = malloc(10);
  }
  printf("free\n");
  for (int i = 0; i < 30000; i++) {
    free(a[i]);
  }
  b1 = malloc(40);
  printf("b = %p\n", b1);
  return 0;
  int i;
  if (i = fork()) {
    printf("child task return %d\n", waittid(i));
  } else {
    printf("CHILD\n");
    exit(114514);
  }
  return 0;
  s = strdup("hello, world");
  float b = 0;
  if (fork()) {
    printf("PARENT %s\n", s);
    unsigned *r = malloc(16);
    printf("malloced %08x\n", r);
    r[0] = 114514;
    while (1) {
      b = b + 0.1;
      printf("%d %f\n", r[0], b);
    }
    while (!a111)
      ;
    printf("A = 1!!!\n");
    for (;;)
      ;
  } else {
    printf("SON %s\n", s);
    a111 = 1;
    printf("a = %d\n", a111);
    unsigned *r = malloc(16);
    printf("malloced %08x\n", r);
    while (1) {
      b = b + 0.5;
      r[0] = 3;
    }
    for (;;)
      ;
  }
  return 0;
  signal(0, test_signal);
  // for(int i = 0;;i++) {
  // }
  if (argc == 1) {
    print("ERROR:No input file.");
    return 0;
  }
  Bitz(argv[1]);
  // int tid = AddThread("", a, (unsigned int)malloc(4096) + 4096);
  // m_tid = NowTaskID();
  // int j = 0;
  // clear();
  // while (1) {
  //   flag_m = 0;
  //   SendMessage(tid, (void *)"hello, world", 12);
  //   goto_xy(0, 0);
  //   printf("[main thread %d] the message has been received\n", j++);
  // }
  return 0;
}
void Bitz(char *filename) {

  if (filesize(filename) == -1) {
    print("File not found\n");
    return;
  }

  int size = filesize(filename);
  // printf("%s",filename);
  unsigned char *file_buffer = (unsigned char *)malloc(size);
  api_ReadFile(filename, file_buffer);

  print("         00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  "
        "0123456789ABCDEF\n");
  print("00000000 ");
  int i;
  int l = 0;
  for (i = 0; i < size; i++) {
    char *buffer = malloc(15);
    sprintf(buffer, "%02x ", file_buffer[i]);
    print(buffer);
    if ((i + 1) % 16 == 0) {
      // 输出对应的ascii码
      print(" ");
      for (int j = i - 15; j <= i; j++) {
        if (file_buffer[j] >= 32 && file_buffer[j] <= 126 &&
            file_buffer[j] != ' ') {
          putch(file_buffer[j]);
        } else {
          putch('.');
        }
      }
      l++;
      print("\n");
      if (l == 23) {
        print("Press any key to continue...");
        getch();
        print("\n");
        l = 0;
      }
      printf("%08x ", i + 1);
    }
    free(buffer);
  }
  // 剩下的
  i--;
  for (;; i++) {
    if (i % 16 == 15) {
      break;
    }
    print("00 ");
  }
  print(" ");
  for (int j = i - 15; j <= size; j++) {
    if (file_buffer[j] >= 32 && file_buffer[j] <= 126) {
      putch(file_buffer[j]);
    } else {
      putch('.');
    }
  }
  free(file_buffer);
}