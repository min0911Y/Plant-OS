#include <dos.h>
#define MAX_FTP_CLIENT_NUM 32
static struct FTP_Client ftp_c[MAX_FTP_CLIENT_NUM];
static void              ftp_client_cmd_handler(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct TCPMessage  *tcp =
      (struct TCPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  u8 *data = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
             (tcp->headerLength * 4);
  printk("Recv:%s\n", data);
  struct FTP_Client *ftp_c_ = ftp_client_find(socket);
  if (ftp_c_ != NULL) {
    printk("Not NULL\n");
    u32 length = swap16(ipv4->totalLength) - sizeof(struct IPV4Message) - (tcp->headerLength * 4);
    free((void *)ftp_c_->recv_buf_cmd);
    ftp_c_->recv_buf_cmd = (u8 *)malloc(length);
    memcpy((void *)ftp_c_->recv_buf_cmd, (void *)data, length);
    ftp_c_->reply_code    = (u32)strtol(data, NULL, 10);
    ftp_c_->recv_flag_cmd = true;
    printk("reply_code:%d\n", ftp_c_->reply_code);
  } else {
    printk("NULL\n");
  }
  //printk("Reply: %s\n", ftp_c_->recv_buf_cmd);
}
static void ftp_client_dat_handler(struct Socket *socket, void *base) {
  struct IPV4Message *ipv4 = (struct IPV4Message *)(base + sizeof(struct EthernetFrame_head));
  struct TCPMessage  *tcp =
      (struct TCPMessage *)(base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message));
  u8 *data = base + sizeof(struct EthernetFrame_head) + sizeof(struct IPV4Message) +
             (tcp->headerLength * 4);
  struct FTP_Client *ftp_c_ = ftp_client_find(socket);
  printk("DAT Handler!\n");
  if (ftp_c_ != NULL) {
    u32 pak_length =
        swap16(ipv4->totalLength) - sizeof(struct IPV4Message) - (tcp->headerLength * 4);
    u32 mlc_length = *(u32 *)(ftp_c_->recv_buf_dat - 4);
    if (ftp_c_->recv_buf_dat != NULL) {
      void *ptr            = realloc(ftp_c_->recv_buf_dat, pak_length + mlc_length);
      ftp_c_->recv_buf_dat = (u8 *)ptr;
      memcpy((void *)(ftp_c_->recv_buf_dat + mlc_length), (void *)data, pak_length);
      ftp_c_->recv_dat_size = pak_length + mlc_length;
    } else {
      ftp_c_->recv_buf_dat = (u8 *)malloc(pak_length);
      memcpy((void *)ftp_c_->recv_buf_dat, (void *)data, pak_length);
      ftp_c_->recv_dat_size = pak_length;
    }
    ftp_c_->recv_flag_dat = true;
  }
}
static int Login(struct FTP_Client *ftp_c_, u8 *user, u8 *pass) {
  char s[128];
  sprintf(s, "USER %s\r\n", user);
  ftp_c_->recv_flag_cmd = false;
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
  printk("wait");
  while (ftp_c_->recv_flag_cmd != true)
    ;
  printk("ok");
  if (ftp_c_->reply_code != 331) {
    printk("Return n 331\n");
    return -1;
  }
  sprintf(s, "PASS %s\r\n", pass);
  ftp_c_->recv_flag_cmd = false;
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
  printk("Wait 2\n");
  while (ftp_c_->recv_flag_cmd != true)
    ;
  printk("OK\n");
  if (ftp_c_->reply_code != 230) {
    printk("Return n 230\n");
    return -1;
  }
  return 0;
}
static int TransModeChoose(struct FTP_Client *ftp_c_, int mode) {
  if (mode == FTP_PORT_MODE) {
    ftp_c_->socket_dat->remotePort = 20;
    ftp_c_->socket_dat->state      = SOCKET_TCP_LISTEN;
    char s[128];
    u8   i1, i2, i3, i4;
    i1 = (u8)ftp_c_->socket_dat->localIP;
    i2 = (u8)ftp_c_->socket_dat->localIP >> 8;
    i3 = (u8)ftp_c_->socket_dat->localIP >> 16;
    i4 = (u8)ftp_c_->socket_dat->localIP >> 24;
    sprintf(s, "PORT %d,%d,%d,%d,%d,%d\r\n", i1, i2, i3, i4, ftp_c_->socket_dat->localPort / 256,
            ftp_c_->socket_dat->localPort % 256);
    ftp_c_->recv_flag_cmd = false;
    ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
    while (ftp_c_->recv_flag_cmd != true)
      ;
    while (ftp_c_->socket_dat->state != SOCKET_TCP_ESTABLISHED)
      ;
  } else if (mode == FTP_PASV_MODE) {
    ftp_c_->recv_flag_cmd = false;
    ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, "PASV\r\n", 6);
    while (ftp_c_->recv_flag_cmd != true)
      ;
    if (ftp_c_->reply_code != 227) { return -1; }
    u8 *p = strrchr(ftp_c_->recv_buf_cmd, '(');
    u16 port;
    for (int i = 0, spCount = 0; i != strlen(p); i++) {
      if (p[i] == ',') { spCount++; }
      if (spCount == 4) {
        char *ptr;
        u8    port1 = (u8)strtol(&p[i + 1], &ptr, 10);
        u8    port2 = (u8)strtol(ptr, NULL, 10);
        port        = port1 * 256 + port2;
        printk("port1=%d\nport2=%d\n", port1, port2);
        break;
      }
    }
    ftp_c_->socket_dat->remotePort = port;
    printk("Port = %d\n", port);
    if (ftp_c_->socket_dat->Connect(ftp_c_->socket_dat) == -1) { return -1; }
  }
  return 0;
}
static void Logout(struct FTP_Client *ftp_c_) {
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, "QUIT\r\n", 6);
  ftp_c_->is_login = false;
}
static int Download(struct FTP_Client *ftp_c_, u8 *path_pdos, u8 *path_ftp, int mode) {
  printk("DOWNLOAD!!!\n");
  char s[50];
  sprintf(s, "SIZE %s\r\n", path_ftp);
  ftp_c_->recv_flag_cmd = false;
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
  while (ftp_c_->recv_flag_cmd != true)
    ;
  u32 size = (u32)strtol(ftp_c_->recv_buf_cmd + 4, NULL, 10);
  printk("SIZE=%d\n", size);
  if (ftp_c_->TransModeChoose(ftp_c_, mode) == -1) {
    printk("Error!!!\n");
    return -1;
  }
  if (fsz(path_pdos) == -1) { vfs_createfile(path_pdos); }
  FILE *fp = fopen(path_pdos, "wb");
  sprintf(s, "RETR %s\r\n", path_ftp);
  ftp_c_->recv_flag_cmd = false;
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
  printk("Wait recv flag.........\n");
  while (ftp_c_->recv_flag_cmd != true)
    ;
  printk("Wait file send.........\n");
  while (ftp_c_->recv_dat_size < size)
    ;
  printk("RECV:\n");
  for (int i = 0; i != size; i++) {
    printk("%c", ftp_c_->recv_buf_dat[i]);
    fputc(ftp_c_->recv_buf_dat[i], fp);
  }
  free((void *)ftp_c_->recv_buf_dat);
  return 0;
}
static u8 *Getlist(struct FTP_Client *ftp_c_, u8 *path, int mode) {
  if (ftp_c_->TransModeChoose(ftp_c_, mode) == -1) { return -1; }
  char s[50];
  sprintf(s, "LIST %s\r\n", path);
  ftp_c_->recv_flag_cmd = false;
  ftp_c_->recv_flag_dat = false;
  ftp_c_->socket_cmd->Send(ftp_c_->socket_cmd, s, strlen(s));
  while (ftp_c_->recv_flag_cmd != true)
    ;
  if (ftp_c_->reply_code != 150) { return (u8 *)NULL; }
  while (ftp_c_->recv_flag_dat != true)
    ;
  u8 *res = (u8 *)malloc(*(u32 *)(ftp_c_->recv_buf_dat - 4));
  memcpy((void *)res, (void *)ftp_c_->recv_buf_dat, *(u32 *)(ftp_c_->recv_buf_dat - 4));
  return res;
}
struct FTP_Client *FTP_Client_Alloc(u32 remoteIP, u32 localIP, u16 localPort) {
  for (int i = 0; i != MAX_FTP_CLIENT_NUM; i++) {
    if (ftp_c[i].is_using == false) {
      ftp_c[i].is_using               = true;
      ftp_c[i].is_login               = false;
      ftp_c[i].socket_cmd             = socket_alloc(TCP_PROTOCOL);
      ftp_c[i].socket_dat             = socket_alloc(TCP_PROTOCOL);
      ftp_c[i].socket_cmd->remoteIP   = remoteIP;
      ftp_c[i].socket_dat->remoteIP   = remoteIP;
      ftp_c[i].socket_cmd->remotePort = FTP_SERVER_COMMAND_PORT;
      ftp_c[i].socket_cmd->localIP    = localIP;
      ftp_c[i].socket_dat->localIP    = localIP;
      ftp_c[i].socket_cmd->localPort  = localPort;
      ftp_c[i].socket_cmd->localPort  = localPort + 1;
      ftp_c[i].Login                  = Login;
      ftp_c[i].TransModeChoose        = TransModeChoose;
      ftp_c[i].Logout                 = Logout;
      ftp_c[i].Download               = Download;
      ftp_c[i].Getlist                = Getlist;
      ftp_c[i].recv_buf_cmd           = (u8 *)malloc(128); // 吴用
      ftp_c[i].recv_buf_dat           = (u8 *)NULL;
      ftp_c[i].recv_flag_cmd          = false;
      ftp_c[i].recv_flag_dat          = false;
      Socket_Bind(ftp_c[i].socket_cmd, ftp_client_cmd_handler);
      Socket_Bind(ftp_c[i].socket_dat, ftp_client_dat_handler);
      if (ftp_c[i].socket_cmd->Connect(ftp_c[i].socket_cmd) == -1) {
        return (struct FTP_Client *)NULL;
      }
      while (ftp_c[i].recv_flag_cmd == false)
        ;
      if (ftp_c[i].reply_code != 220) {
        printk("Connet error\n");
        return NULL;
      }
      ftp_c[i].recv_flag_cmd = false;
      return &ftp_c[i];
    }
  }
  return (struct FTP_Client *)NULL;
}
struct FTP_Client *ftp_client_find(struct Socket *s) {
  for (int i = 0; i != MAX_FTP_CLIENT_NUM; i++) {
    if (ftp_c[i].is_using == false) {
      continue;
    } else {
      if (ftp_c[i].socket_cmd == s || ftp_c[i].socket_dat == s) { return &ftp_c[i]; }
    }
  }
}