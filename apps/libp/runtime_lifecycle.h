#ifndef LIBP_RUNTIME_LIFECYCLE_H
#define LIBP_RUNTIME_LIFECYCLE_H
#ifdef __cplusplus
extern "C" {
#endif
void runtime_initialize_static(void);
void runtime_finalize_static(void);
#ifdef __cplusplus
}
#endif
#endif
