#ifndef PLANT_SPAWN_H
#define PLANT_SPAWN_H

#include <signal.h>
#include <sys/types.h>

typedef struct {
  unsigned reserved;
} posix_spawn_file_actions_t;
typedef struct {
  unsigned flags;
} posix_spawnattr_t;

#ifdef __cplusplus
extern "C" {
#endif
int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attributes, char *const argv[],
                char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attributes, char *const argv[],
                 char *const envp[]);
#ifdef __cplusplus
}
#endif

#endif
