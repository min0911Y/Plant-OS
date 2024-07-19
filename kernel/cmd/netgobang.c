#include <dos.h>
static struct Socket *socket;    // 声明Socket变量，用于调用Socket API
static u32            ng_size;   // 网卡发来的数据包的大小
static u8            *ng_buffer; // 数据将会保存在这里
static int            now;       // 谁先手（值为1对面先手，0的话自己先手）
static char          *style[2] = {"X", "O"};
static char           map[19][19];

static void NETGOBANG_Handler(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct TCPMessage  *tcp =
      (struct TCPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  u16 size = swap16(ipv4->totalLength) - sizeof(struct IPV4Message) - (tcp->headerLength * 4);
  u8 *data = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
             (tcp->headerLength * 4);
  ng_size   = size;
  ng_buffer = (u8 *)malloc(ng_size);
  memcpy((void *)ng_buffer, (void *)data, ng_size);
}
static bool ng_click_flag;
static char password[50];
static char usr[50];
static void NETGOBANG_OnClick() {
  ng_click_flag = true;
  printk("User:");
  input(usr, 50);
  printk("Password:");
  input(password, 50);
}

int SelRoom() {
  u8 str[100];
  ng_size = 0;
  socket->Send(socket, (u8 *)"RMLS", 5);
  while (!ng_size)
    ;
  strcpy((char *)str, (char *)ng_buffer);
  int len = strlen((char *)str);
  for (int i = 0, j = 0; i < len; i++) {
    if (str[i] == ',') {
      str[i] = 0;
      printk("%s\n", &str[j]);
      j += i + 1;
    }
  }
  printk("And You can input \"Exit\" to Return.\n");
  char s[50];
  printk("Room ID:");
  input(s, 50);
  if (strcmp(s, "Exit") == 0) { return -1; }
  return strtol(s, NULL, 0);
}
void CleanMap() {
  for (int i = 0; i < 19; i++) {
    for (int j = 0; j < 19; j++) {
      map[i][j] = 0;
    }
  }
}
void ViewMap() {
  gotoxy(0, 0);
  for (int i = 0; i < 19; i++) {
    for (int j = 0; j < 19; j++) {
      if (map[i][j] != 0) {
        printk("%s", style[map[i][j] - 1]);
      } else {
        printk(" ");
      }
    }
    printk("\n");
  }
}
void FS(int *ox, int *oy) {
  int x = 0;
  int y = 0;
  while (1) {
    ViewMap();
    gotoxy(x, y);
    printk("%s", style[now]);
    char ch = getch();
    if (ch == 'w') {
      if (y > 0) y--;
    } else if (ch == 's') {
      if (y < 18) y++;
    } else if (ch == 'a') {
      if (x > 0) x--;
    } else if (ch == 'd') {
      if (x < 18) x++;
    } else if (ch == ' ') {
      if (map[y][x] != 0) {
        gotoxy(0, 19);
        printk("You can't settle in an area that has already settled!");
        continue;
      }
      map[y][x] = now + 1;
      *ox       = y;
      *oy       = x;
      gotoxy(0, 19);
      return;
    }
  }
}
void FS2() { // 根据ng_buffer获取对方的x，y信息
  char sx[50];
  char sy[50];
  Get_Arg(sx, (char *)ng_buffer, 1);
  Get_Arg(sy, (char *)ng_buffer, 2);
  int x, y;
  x         = strtol(sx, NULL, 10);
  y         = strtol(sy, NULL, 10);
  map[x][y] = (!now) + 1;
  gotoxy(0, 19);
}
void dual() {
  command_run("cls");
  ng_size = 0;
  socket->Send(socket, (u8 *)"R", 2);
  printk("Wait to ready.\n");
  while (!ng_size)
    ;
  ng_size = 0;
  if (now) {
    while (1) {
      ViewMap();
      printk("Waiting...");
      while (!ng_size)
        ;
      gotoxy(0, 19);
      printk("Next!                         ");
      if (*ng_buffer == 'W') { // 服务器告诉我们，已经分出胜负了
        command_run("cls");
        // printk("%s\n", ng_buffer);
        char suid[50];
        Get_Arg(suid, (char *)ng_buffer, 0);
        int  uid = strtol(suid, NULL, 10);
        char ss[50];
        ng_size = 0;
        sprintf(ss, "GETPL %d", uid);
        socket->Send(socket, (u8 *)ss, strlen(ss) + 1);
        while (!ng_size)
          ;
        char name[50];
        Get_Arg(name, (char *)ng_buffer, 0);
        printk("Winner is %s(UID:%d)\n", name, uid);
        CleanMap();
        command_run("pause");
        return;
      } else if (*ng_buffer != 'Y') { // 那就是还没有发，我们继续等
      RE:
        gotoxy(0, 19);
        printk("Waiting...");
        ng_size = 0;
        while (!ng_size)
          ;
        if (*ng_buffer == 'W') {
          command_run("cls");
          // printk("%s\n", ng_buffer);
          /* 这段有bug，而且写得乱，暂时不注释 */
          // FIXME:玩家名称以及UID获取错误
          char suid[50];
          Get_Arg(suid, (char *)ng_buffer, 0);
          int  uid = strtol(suid, NULL, 10);
          char ss[50];
          ng_size = 0;
          sprintf(ss, "GETPL %d", uid);
          socket->Send(socket, (u8 *)ss, strlen(ss) + 1);
          while (!ng_size)
            ;
          char name[50];
          Get_Arg(name, (char *)ng_buffer, 0);
          printk("Winner is %s(UID:%d)\n", name, uid);
          CleanMap(); // 清空Map
          command_run("pause");
          return;
        } else if (*ng_buffer != 'Y') { // 还没发？？？？继续等
          goto RE;
        } else {         // 发了是吧
          FS2();         // 解析并标记
          ViewMap();     // 重新显示一下
          gotoxy(0, 19); // 准备输出Next
          printk("Next!                         ");
        }
      } else {     // 发了
        FS2();     // 解析并标记
        ViewMap(); // 然后显示
      }
      int x, y;
      FS(&x, &y);                                     // 最后捏，到我们自己来了
      char s1[50];                                    // 发送缓冲区
      ng_size = 0;                                    // size置0
      sprintf(s1, "U %d %d", x, y);                   // U命令，告诉服务器我已落子
      socket->Send(socket, (u8 *)s1, strlen(s1) + 1); // 调用Socket API发送
    }
  } else {
    while (1) {
      gotoxy(0, 19);
      printk("Next!                         ");
      ViewMap();
      int x, y;
      FS(&x, &y);
      char s1[50];
      ng_size = 0;
      sprintf(s1, "U %d %d", x, y);
      socket->Send(socket, (u8 *)s1, strlen(s1) + 1);
      ViewMap();
      printk("Waiting...");
      while (!ng_size)
        ;
      gotoxy(0, 19);
      printk("Next!                         ");
      if (*ng_buffer == 'W') {
        command_run("cls");
        // printk("%s\n", ng_buffer);
        char suid[50];
        Get_Arg(suid, (char *)ng_buffer, 0);
        int  uid = strtol(suid, NULL, 10);
        char ss[50];
        ng_size = 0;
        sprintf(ss, "GETPL %d", uid);
        socket->Send(socket, (u8 *)ss, strlen(ss) + 1);
        while (!ng_size)
          ;
        char name[50];
        Get_Arg(name, (char *)ng_buffer, 0);
        printk("Winner is %s(UID:%d)\n", name, uid);
        CleanMap();
        command_run("pause");
        return;
      } else if (*ng_buffer != 'Y') {
      RE1:
        ng_size = 0;
        while (!ng_size)
          ;
        gotoxy(0, 19);
        printk("Waiting...");
        if (*ng_buffer == 'W') {
          command_run("cls");
          // printk("%s\n", ng_buffer);
          char suid[50];
          Get_Arg(suid, (char *)ng_buffer, 0);
          int  uid = strtol(suid, NULL, 10);
          char ss[50];
          ng_size = 0;
          sprintf(ss, "GETPL %d", uid);
          socket->Send(socket, (u8 *)ss, strlen(ss) + 1);
          while (!ng_size)
            ;
          char name[50] = {0};
          Get_Arg(name, (char *)ng_buffer, 0);
          printk("Winner is %s(UID:%d)\n", name, uid);
          CleanMap();
          command_run("pause");
          return;
        } else if (*ng_buffer != 'Y') {
          goto RE1;
        } else {
          FS2();
          ViewMap();
          gotoxy(0, 19);
          printk("Next!                         ");
        }
      } else {
        FS2();
        ViewMap();
      }
    }
  }
}
void switchUI() {
  while (1) {
    command_run("cls");
    printk("Do you want to be:\n");
    printk("    1.Create ROOM\n");
    printk("    2.Join ROOM\n");
    printk("    3.Exit\n");
    printk("Choose:");
    char s[20];
    input(s, 20);
    if (strcmp(s, "1") == 0) {
      printk("Room Name:");
      char s1[20];
      input(s1, 20);
      char s3[50];
      sprintf(s3, "CRT %s", s1);
      ng_size = 0;
      socket->Send(socket, (u8 *)s3, strlen(s3) + 1);
      while (!ng_size)
        ;
      ng_size = 0;
      printk("Waiting player to join this room...\n");
      while (!ng_size)
        ;
      now = 0;
      dual();
      ng_size = 0;
      socket->Send(socket, (u8 *)"EXIT", 5);
      while (!ng_size)
        ;
    } else if (strcmp(s, "2") == 0) {
      int rid = SelRoom();
      if (rid == -1) {
        continue;
      } else {
        char s2[50];
        sprintf(s2, "IN %d", rid);
        ng_size = 0;
        socket->Send(socket, (u8 *)s2, strlen(s2) + 1);
        while (!ng_size)
          ;
        if (*ng_buffer == 'E') {
          printk("Error: You can't join a room with people\n");
          command_run("pause");
          continue;
        }
        now = 1;
        dual();
        ng_size = 0;
        socket->Send(socket, (u8 *)"EXIT", 5);
        while (!ng_size)
          ;
      }
    } else if (strcmp(s, "3") == 0) {
      socket->Disconnect(socket);
      socket_free(socket);
      return;
    }
  }
}
void netgobang() {
  CleanMap();
  u8 str[100];
  now = 2; // 还没确定捏
  printk("Server IP:");

  input((char *)str, 100);
  u32 ip_ = IP2UINT32_T(str);
  printk("Server Port:");
  input((char *)str, 100);
  u16 port = (u16)strtol((char *)str, NULL, 10);
  printk("Connect the Server...\n");
  if (ping(ip_) == -1) {
    printk("The ICMP Packet is not from the ip.\n");
    return;
  }
  socket = socket_alloc(TCP_PROTOCOL);
  extern u32 ip;
  srand(time());
  Socket_Init(socket, ip_, port, ip, rand());
  Socket_Bind(socket, NETGOBANG_Handler);
  if (socket->Connect(socket) == -1) {
    printk("Connect the Server Failed.\n");
    return;
  }
  ng_size = 0;
  socket->Send(socket, (u8 *)"TEST", 5);
  while (!ng_size)
    ;
  if (strcmp((char *)ng_buffer, "OK") != 0) {
    printk("The Server don't support NETGOBANG.\n");
    return;
  }
  printk("Connect the Server OK.\n");
  printk("Register(0)/Login(1):");
  bool mode = getch() - '0';
  printk("\n");
  NETGOBANG_OnClick();
  if (!mode) {
    ng_size = 0;
    sprintf((char *)str, "REG %s %s", usr, password);
    socket->Send(socket, str, strlen((char *)str) + 1);
    while (!ng_size)
      ;
    ng_size = 0;
    sprintf((char *)str, "LOG %s %s", usr, password);
    socket->Send(socket, str, strlen((char *)str) + 1);
    while (!ng_size)
      ;
  } else if (mode) {
    ng_size = 0;
    sprintf((char *)str, "LOG %s %s", usr, password);
    socket->Send(socket, str, strlen((char *)str) + 1);
    while (!ng_size)
      ;
  }

  switchUI();
}