#pragma once
#include "net.h"

#define FTP_PORT_MODE           1
#define FTP_PASV_MODE           2
#define FTP_SERVER_DATA_PORT    20
#define FTP_SERVER_COMMAND_PORT 21

struct FTP_Client {
  int (*Login)(struct FTP_Client *ftp_c_, u8 *user, u8 *pass);
  int (*TransModeChoose)(struct FTP_Client *ftp_c_, int mode);
  void (*Logout)(struct FTP_Client *ftp_c_);
  int (*Download)(struct FTP_Client *ftp_c_, u8 *path_pdos, u8 *path_ftp, int mode);
  int (*Upload)(struct FTP_Client *ftp_c_, u8 *path_pdos, u8 *path_ftp, int mode);
  int (*Delete)(struct FTP_Client *ftp_c_, u8 *path_ftp);
  u8 *(*Getlist)(struct FTP_Client *ftp_c_);
  struct Socket *socket_cmd;
  struct Socket *socket_dat;
  bool           is_using;
  bool           is_login;
  u8            *recv_buf_cmd;
  bool           recv_flag_cmd;
  u32            reply_code;
  u8            *recv_buf_dat;
  bool           recv_flag_dat;
  u32            recv_dat_size;
};
