#include <dlfcn.h>
#include <loader.h>
#include <tls.h>

extern const runtime_linker_t *runtime_linker;
#define PROCESS_HANDLE ((void *)1)

void *dlopen(const char *path, int flags) {
  tls_control_t *tls = tls_current();
  tls->loader_error = NULL;
  if (flags & ~(RTLD_NOW | RTLD_LAZY | RTLD_GLOBAL)) {
    tls->loader_error = "unsupported dynamic loader flags";
    return NULL;
  }
  if (!runtime_linker) {
    tls->loader_error = "no runtime linker";
    return NULL;
  }
  if (path) {
    tls->loader_error =
        "loading additional shared objects is not yet available";
    return NULL;
  }
  return PROCESS_HANDLE;
}
void *dlsym(void *handle, const char *name) {
  tls_control_t *tls = tls_current();
  tls->loader_error = NULL;
  if ((handle != PROCESS_HANDLE && handle != RTLD_DEFAULT) || !name ||
      !runtime_linker) {
    tls->loader_error = "invalid dynamic symbol lookup";
    return NULL;
  }
  void *address;
  if (runtime_linker->symbol(name, &address)) {
    tls->loader_error = "symbol not found";
    return NULL;
  }
  return address;
}
int dlclose(void *handle) {
  tls_control_t *tls = tls_current();
  tls->loader_error =
      handle == PROCESS_HANDLE ? NULL : "invalid dynamic library handle";
  return handle == PROCESS_HANDLE ? 0 : -1;
}
char *dlerror(void) {
  tls_control_t *tls = tls_current();
  const char *error = tls->loader_error;
  tls->loader_error = NULL;
  return (char *)error;
}
int dladdr(const void *address, Dl_info *information) {
  return runtime_linker && information
             ? runtime_linker->address_info(address, information)
             : 0;
}
