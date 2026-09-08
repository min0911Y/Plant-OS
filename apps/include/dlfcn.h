#ifndef PLANT_DLFCN_H
#define PLANT_DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *)0)

typedef struct native_dl_info {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif
void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
int dladdr(const void *address, Dl_info *information);
#ifdef __cplusplus
}
#endif
#endif
