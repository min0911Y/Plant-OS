#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <net.h>
#define CHAT_CLIENT_PROT 21538
static uint8_t *chat_data; // 接收网卡发来的数据包
static struct Socket *socket;   // socket API需要用到这个结构体
uint32_t sip1, sip2, sip3, sip4;
uint32_t ip, server_ip;
uint16_t server_port;
void chat_recv_task() {
  for (;;) {
	memset(chat_data, 0, 4096);
    Socket_Recv(socket, chat_data, 4096);
	printf(chat_data);
  }
}
int main(int argc, char **argv) { // 用于接收用户想要发送的内容
  printf("Welcome to Plant OS Chat!\n");
  chat_data = (uint8_t *)malloc(4096);
  ip = GetIP(); // 系统IP地址
  printf("Server IP: ");
  scanf("%d.%d.%d.%d", &sip1, &sip2, &sip3, &sip4);
  server_ip = ((sip1 << 24) | (sip2 << 16) | (sip3 << 8) | sip4);
  printf("Server Port: ");
  scanf("%d", &server_port);
  printf("Ping %d.%d.%d.%d:%d...\n", sip1, sip2, sip3, sip4, server_port);
  if (ping(server_ip) == -1) {
    printf("The ICMP packet is not from %d.%d.%d.%d.\n", sip1, sip2, sip3, sip4); // 连接失败
    return;
  }
  printf("Ping %d.%d.%d.%d:%d done.\n", sip1, sip2, sip3, sip4, server_port); // 到这里说明连接成功
  printf("Connect Server...\n"); // 现在通过UDP协议连接服务器
  socket = Socket_Alloc(UDP_PROTOCOL); // 向SocketAPI分配一个socket位置，让我们通过Socket API连接到服务器（声明使用UDP协议）
  Socket_Init(socket, server_ip, server_port, ip, CHAT_CLIENT_PROT); // 对Socket进行初始化（设置服务器的IP、端口，以及自身的IP、端口）
  memset(chat_data, 0, 4096); // 清空缓冲区
  Socket_Send(socket, (uint8_t *)"CONNECT ", strlen("CONNECT ")); // 告诉服务器我们已经连接
  Socket_Recv(socket, chat_data, 4096); // 等待服务器回复
  if (strcmp((char *)chat_data, "OK") != 0) { // 如果服务器是chat程序的正确服务端，那么将会返回OK字样
    printf("The UDP packet is not from %d.%d.%d.%d:%d.\n", sip1, sip2, sip3, sip4, server_port); // 啊哦，不是
    return;
  }
  printf("Connect Server done.\n"); // 到这里说明是了
  char *inp = (char *)malloc(4096); // 设置输入缓冲区
  /* 设置声明指令（5个字节） */
  inp[0] = 'S';
  inp[1] = 'E';
  inp[2] = 'N';
  inp[3] = 'D';
  inp[4] = ' ';
  AddThread("chat_recv_task", chat_recv_task, malloc(32 * 1024) + 32 * 1024 - 4);
  for (;;) {
    printf("To %d.%d.%d.%d:%d Server ### ", sip1, sip2, sip3, sip4, server_port); // 提示符信息
    scan(&inp[5], 4096 - 5); // 输入，&inp[5]是因为上面声明了指令（五个字节），同样，4096是通过4096-5得到的
    Socket_Send(socket, (uint8_t *)inp, strlen(inp)); // 通过socket API发送
  }
}
