#ifndef PLANT_GRP_H
#define PLANT_GRP_H

#include <sys/types.h>

struct group {
  char *gr_name;
  char *gr_passwd;
  gid_t gr_gid;
  char **gr_mem;
};

#ifdef __cplusplus
extern "C" {
#endif
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
int getgrgid_r(gid_t gid, struct group *entry, char *buffer, size_t size,
               struct group **result);
int getgrnam_r(const char *name, struct group *entry, char *buffer, size_t size,
               struct group **result);
#ifdef __cplusplus
}
#endif

#endif
