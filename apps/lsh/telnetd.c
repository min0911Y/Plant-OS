/**
 * @file telnetd.c
 * @author Letter
 * @brief telnet server for letter shell
 * @version 0.1
 * @date 2021-08-07
 *
 * @copyright (c) 2021 Letter
 *
 */
#include "telnetd.h"

#include <net.h>

#include "shell.h"
#include "shell_cmd_group.h"

#if SHELL_USING_COMPANION != 1
#error telent for letter shell can not be used while shell companion is diabled
#endif

/**
 * @brief 新线程接口实例
 */
static NewThread newThread;

/**
 * @brief telnet server socket
 */
static socket_t telnetdSocket;

/**
 * @brief telent server shell
 */
static Shell *telnetdShell;

/**
 * @brief telnet server 监听端口
 */
static int telnetdPort = TELNETD_DEFAULT_SERVER_PORT;

static void telnetdServer(void);
static void telnetdConnection(int client);
static void telentdWrite(char *data, short len);

/**
 * @brief telnet 协议命令
 */
static char telnetCmd[] = {0xff, 0xfb, 0x01, 0xff, 0xfb,
                           0x03, 0xff, 0xfc, 0x1f};

/**
 * @brief telnet server初始化
 *
 * @param newThreadInterface 新线程接口
 *
 * @return int 0 启动telent成功 -1 启动失败
 */
int telentdInit(NewThread newThreadInterface) {
  newThread = newThreadInterface;
  return newThread ? 0 : -1;
}

/**
 * @brief 启动 telnet server
 *
 * @return int 0 启动telent成功 -1 启动失败
 */
#define A(x) (x >> 24)
#define B(x) ((x >> 16) & 0xff)
#define C(x) ((x >> 8) & 0xff)
#define D(x) (x & 0xff)
int telnetdStart() {
  unsigned IP;
  IP = GetIP();
  printf("%d.%d.%d.%d\n", A(IP), B(IP), C(IP), D(IP));
  telnetdServer();

  return 0;
}

/**
 * @brief 停止 telnet server
 *
 */
void telnetdStop() {
  //   Socket_Free(telnetdSocket);
  //   if (telnetdShell != NULL) {
  //     Socket_Free(
  //         (socket_t)shellCompanionGet(telnetdShell,
  //         SHELL_COMPANION_ID_TELNETD));
  //   }
}

/**
 * @brief 修改telnet server 监听端口
 *
 * @param port 端口
 *
 */
void telnetdSetPort(int port) { telnetdPort = port; }

/**
 * @brief telent 服务
 *
 *
 */
static void telnetdServer(void) {
  telnetdSocket = Socket_Alloc(TCP_PROTOCOL);
  Socket_Init(telnetdSocket, 0, 0, GetIP(), telnetdPort);
  if (1) {
    while (1) {
      listen(telnetdSocket);
      uint32_t client = telnetdSocket;
      telnetdConnection(client);
      // newThread(telnetdConnection, (void *)client);
      return;
    }
  }
  //   close(telnetdSocket);
}

/**
 * @brief telnet server 连接处理
 *
 * @param client 客户端连接socket
 *
 */
extern Shell shell;
unsigned short userShellRead1(char *data, unsigned short len) {
  int client = (int)shellCompanionGet(telnetdShell, SHELL_COMPANION_ID_TELNETD);
  unsigned short length = len;
  int l = Socket_Recv(client, data, len);
  if (!l) {
    Socket_Free(client);
    exit(0);
  }
  return len;
}
static void telnetdConnection(int client) {
  // for(;;);
  int len = 0;
  char *data = SHELL_MALLOC(16);
  char *shellBuffer = SHELL_MALLOC(TELNETD_SHELL_BUFFER_SIZE);
  telnetdShell = SHELL_MALLOC(sizeof(Shell));
  char shellPathBuffer[512] = "/";
  /** 处理 telent 协议 */
  Socket_Send(client, telnetCmd, 9);
  Socket_Recv(client, data, 6);

  shell.write = telentdWrite;
  shell.read = userShellRead1;
  shellCompanionAdd(telnetdShell, SHELL_COMPANION_ID_TELNETD, (void *)client);
}

/**
 * @brief telnet server数据写
 *
 * @param data 写入的数据
 * @param len 数据长度
 *
 */
static void telentdWrite(char *data, short len) {
  int client = (int)shellCompanionGet(telnetdShell, SHELL_COMPANION_ID_TELNETD);
  if (client != 0) {
    Socket_Send(client, data, len);
  }
}

ShellCommand telnetdGroup[] = {
    SHELL_CMD_GROUP_ITEM(SHELL_TYPE_CMD_FUNC, start, telnetdStart,
                         start telnet server),
    SHELL_CMD_GROUP_ITEM(SHELL_TYPE_CMD_FUNC, stop, telnetdStop,
                         stop telnet server),
    SHELL_CMD_GROUP_ITEM(SHELL_TYPE_CMD_FUNC, setPort, telnetdSetPort,
                         set telnet server port),
    SHELL_CMD_GROUP_END()};
SHELL_EXPORT_CMD_GROUP(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
telnetd, telnetdGroup, telnet server\ninput telent -h for more help);
