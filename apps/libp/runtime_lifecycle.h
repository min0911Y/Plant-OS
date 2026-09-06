#ifndef LIBP_RUNTIME_LIFECYCLE_H
#define LIBP_RUNTIME_LIFECYCLE_H
#include <loader.h>
#ifdef __cplusplus
extern "C" {
#endif
void runtime_initialize(const runtime_linker_t *linker);
void runtime_finalize(void);
#ifdef __cplusplus
}
#endif
#endif
