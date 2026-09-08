#ifndef PLANT_PWD_H
#define PLANT_PWD_H
#include <sys/types.h>
struct passwd {
  char *pw_name, *pw_passwd;
  uid_t pw_uid;
  gid_t pw_gid;
  char *pw_gecos, *pw_dir, *pw_shell;
};
#ifdef __cplusplus
extern "C" {
#endif
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
int getpwnam_r(const char *name, struct passwd *entry, char *buffer,
               size_t size, struct passwd **result);
int getpwuid_r(uid_t uid, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result);
#ifdef __cplusplus
}
#endif
#endif
