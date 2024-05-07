#include "../sdl2/include/SDL.h"
#include <fcntl.h>
#include <gui.h>
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

  for (;;)
    ;
}
void b1() {
  AddThread("", c1, (unsigned)malloc(1024 * 512) + 1024 * 512);
  printf("B1\n");

  for (;;)
    ;
}
#define POINTS_COUNT 4

static SDL_Point points[POINTS_COUNT] = {
    {320, 200}, {300, 240}, {340, 240}, {320, 200}};
#define PACK_XY(x, y) ((x << 16) | y)
static SDL_Rect bigrect = {0, 0, 280, 280};
enum { MOUSE_STAY = 1, MOUSE_CLICK_LEFT, MOUSE_CLICK_RIGHT, CLOSE_WINDOW };



#define WINDOW_WIDTH 800  
#define WINDOW_HEIGHT 600  
#define SQUARE_SIZE 50  
#define CIRCLE_RADIUS 200  
#define CIRCLE_CENTER_X (WINDOW_WIDTH / 2)  
#define CIRCLE_CENTER_Y (WINDOW_HEIGHT / 2)  
#define ANGLE_INCREMENT 2 // 控制正方形移动速度的角度增量  
  
SDL_Window *window;  
SDL_Renderer *renderer;  
SDL_Event event;  
double angle = 0; // 初始角度  
  
int main(int argc, char* argv[]) {  
    SDL_Init(SDL_INIT_VIDEO);  
    window = SDL_CreateWindow("SDL Circle Motion", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);  
    renderer = SDL_CreateRenderer(window, -1, 0);  
  
    while (1) {  
        while (SDL_PollEvent(&event)) {  
            if (event.type == SDL_QUIT) {  
                exit(0);  
            }  
        }  
  
        // 更新角度和位置  
        angle += ANGLE_INCREMENT;  
        if (angle > 360) {  
            angle = 0; // 或者使用 fmod(angle, 360) 来避免超过360度  
        }  
  
        // 计算正方形在圆周上的位置  
        int squareX = CIRCLE_CENTER_X + (int)(CIRCLE_RADIUS * cos(angle * M_PI / 180.0));  
        int squareY = CIRCLE_CENTER_Y - (int)(CIRCLE_RADIUS * sin(angle * M_PI / 180.0)); // 注意Y轴方向可能需要调整  
  
        // 清除屏幕  
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // 白色背景  
        SDL_RenderClear(renderer);  
  
        // 绘制正方形  
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // 红色正方形  
        SDL_Rect squareRect = {squareX - SQUARE_SIZE / 2, squareY - SQUARE_SIZE / 2, SQUARE_SIZE, SQUARE_SIZE};  
        SDL_RenderFillRect(renderer, &squareRect);  
  
        // 更新屏幕  
        SDL_RenderPresent(renderer);  
  
        // 控制帧率，例如：延迟以保持一定的帧率  
        SDL_Delay(16); // 大约60 FPS  
    }  
  
    SDL_DestroyRenderer(renderer);  
    SDL_DestroyWindow(window);  
    SDL_Quit();  
    return 0;  
}
int main1(int argc, char **argv) {

  // window_t wnd1 = create_window("a",0,0,800,600);
  // unsigned *buf1 = window_get_fb(wnd1);
  // buf1[0 * 800 + 0] = 0x0;
  // buf1[0 * 800 + 1] = 0x0;
  // buf1[1 * 800 + 0] = 0x0;
  // buf1[1 * 800 + 1] = 0x0;
  // window_refresh(wnd1,PACK_XY(0,0),PACK_XY(1,1));
  // sleep(1000);
  // close_window(wnd1);
  // return 0;
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return -1;
  }

  // 创建窗口
  SDL_Window *window =
      SDL_CreateWindow("SDL2 Window", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_SHOWN);
  if (window == NULL) {
    fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }
  SDL_Renderer *renderer;
  renderer = SDL_CreateRenderer(window, -1, 0);
  printf("Create Window successfully\n");
  SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
  // sleep(1000);
  /* Select the color for drawing. It is set to white here. */

  // SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
  // /* Clear the entire screen to our selected color. */
  // SDL_RenderClear(renderer);

  //  设置颜色，画点
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  SDL_RenderDrawPoint(renderer, 500, 500);

  //  设置颜色，画线
  SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
  SDL_RenderDrawLines(renderer, points, POINTS_COUNT);

  //  设置颜色，画矩形
  SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
  SDL_Rect rect = {200, 300, 100, 100};
  SDL_RenderDrawRect(renderer, &rect);

  //  设置颜色，填充目标矩形区域
  SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
  SDL_Rect rect2 = {400, 400, 100, 100};
  SDL_RenderFillRect(renderer, &rect2);

  //  设置颜色，填充目标矩形区域
  SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
  SDL_RenderFillRect(renderer, &bigrect);

  /* Up until now everything was drawn behind the scenes.
  This will show the new, red contents of the window. */
  SDL_RenderPresent(renderer);
  SDL_Event event;
  SDL_StartTextInput();
  while (1) {
    // Wait
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
        printf("DOWN %d %d\n", event.button.x, event.button.y);
      } else if (event.type == SDL_TEXTINPUT) {
        printf("%s",event.text.text);
      }
    }
  }
  // 退出SDL
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;

  window_t wnd = create_window("TEST", 0, 0, 320, 200);
  sleep(1000);
  for (int i = 2; i < 100; i++) {
    for (int j = 21; j < 100; j++) {
      draw_px(wnd, i, j, 0);
    }
  }

  window_refresh(wnd, (2 >> 16) | (21), (100 >> 16) | 100);
  while (1) {
    unsigned a = window_get_event(wnd);
    if (a == -1) {
      continue;
    }
    unsigned b;
    switch (a) {
    case MOUSE_STAY:
      b = window_get_event(wnd);
      break;
    case MOUSE_CLICK_LEFT:
      b = window_get_event(wnd);
      int x = b >> 16;
      int y = b & 0xffff;
      draw_px(wnd, x, y, 0x0);
      draw_px(wnd, x + 1, y, 0x0);
      draw_px(wnd, x, y + 1, 0x0);
      draw_px(wnd, x + 1, y + 1, 0x0);
      window_refresh(wnd, b, ((x + 1) << 16) | (y + 1));
      break;
    case MOUSE_CLICK_RIGHT:
      b = window_get_event(wnd);
      printf("CLICK RIGHT %d %d\n", b >> 16, b & 0x0000ffff);
      break;
    case CLOSE_WINDOW:
      printf("CLOSE\n");
      close_window(wnd);
      exit(0);
      break;
    default:
      break;
    }
  }
  for (;;)
    ;
  return 0;
  if (fork() == 0) {
    printf("P\n");
    sleep(1000);
    exit(0);
  } else {
    printf("S\n");
  }
  return 0;
  AddThread("", b1, (unsigned)malloc(1024 * 512) + 1024 * 512);

  printf("MAIN\n");
  for (;;)
    ;
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
    if (a == 0)
      break;
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