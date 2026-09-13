#ifndef PLANT_COPYFILE_H
#define PLANT_COPYFILE_H

#include <stddef.h>

typedef struct copyfile_state *copyfile_state_t;
typedef unsigned int copyfile_flags_t;
typedef int (*copyfile_callback_t)(int what, int stage,
                                   copyfile_state_t state, const char *source,
                                   const char *destination, void *context);

#define COPYFILE_DATA 0x00000001u
#define COPYFILE_COPY_DATA 0x00000002u
#define COPYFILE_STATE_STATUS_CB 0x00000001
#define COPYFILE_STATE_STATUS_CTX 0x00000002
#define COPYFILE_CONTINUE 0
#define COPYFILE_QUIT 1
#define COPYFILE_ERR 2
#define COPYFILE_PROGRESS 3

#ifdef __cplusplus
extern "C" {
#endif

copyfile_state_t copyfile_state_alloc(void);
int copyfile_state_free(copyfile_state_t state);
int copyfile_state_set(copyfile_state_t state, unsigned int key,
                       const void *value);
int fcopyfile(int source, int destination, copyfile_state_t state,
              copyfile_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif
