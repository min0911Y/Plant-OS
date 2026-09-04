#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <socket.h>
#define CHAT_CLIENT_PROT 21538
static uint8_t *chat_data; // 接收网卡发来的数据包
static socket_t chat_socket;
uint32_t sip1, sip2, sip3, sip4;
uint32_t server_ip;
uint16_t server_port;
void chat_recv_task() {
  for (;;) {
	memset(chat_data, 0, 4096);
    int received = recv(chat_socket, chat_data, 4095, 0);
    if (received <= 0) {
      return;
    }
	printf("%s", chat_data);
  }
}
int main(int argc, char **argv) { // 用于接收用户想要发送的内容
  printf("Welcome to Plant OS Chat!\n");
  chat_data = (uint8_t *)malloc(4096);
  printf("Server IP: ");
  scanf("%d.%d.%d.%d", &sip1, &sip2, &sip3, &sip4);
  server_ip = ((sip1 << 24) | (sip2 << 16) | (sip3 << 8) | sip4);
  printf("Server Port: ");
  scanf("%d", &server_port);
  printf("Connect Server...\n"); // 现在通过UDP协议连接服务器
  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(server_ip);
  server.sin_port = htons(server_port);
  chat_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_port = htons(CHAT_CLIENT_PROT);
  if (chat_socket < 0 ||
      bind(chat_socket, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
      connect(chat_socket, (const struct sockaddr *)&server, sizeof(server)) != 0) {
    printf("Connect the UDP socket failed.\n");
    socket_close(chat_socket);
    return 0;
  }
  memset(chat_data, 0, 4096); // 清空缓冲区
  send(chat_socket, "CONNECT ", strlen("CONNECT "), 0); // 告诉服务器我们已经连接
  recv(chat_socket, chat_data, 4095, 0); // 等待服务器回复
  if (strcmp((char *)chat_data, "OK") != 0) { // 如果服务器是chat程序的正确服务端，那么将会返回OK字样
    printf("The UDP packet is not from %d.%d.%d.%d:%d.\n", sip1, sip2, sip3, sip4, server_port); // 啊哦，不是
    return 0;
  }
  printf("Connect Server done.\n"); // 到这里说明是了
  char *inp = (char *)malloc(4096); // 设置输入缓冲区
  /* 设置声明指令（5个字节） */
  inp[0] = 'S';
  inp[1] = 'E';
  inp[2] = 'N';
  inp[3] = 'D';
  inp[4] = ' ';
  AddThread("chat_recv_task", (uintptr_t)chat_recv_task,
            (uintptr_t)malloc(32 * 1024) + 32 * 1024 - 4);
  for (;;) {
    printf("To %d.%d.%d.%d:%d Server ### ", sip1, sip2, sip3, sip4, server_port); // 提示符信息
    scan(&inp[5], 4096 - 5); // 输入，&inp[5]是因为上面声明了指令（五个字节），同样，4096是通过4096-5得到的
    send(chat_socket, inp, strlen(inp), 0); // 通过socket API发送
  }
}
