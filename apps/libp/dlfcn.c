#include <dlfcn.h>
#include <loader.h>
#include <tls.h>

extern const runtime_linker_t *runtime_linker;

void *dlopen(const char *path, int flags) {
  tls_control_t *tls = tls_current();
  tls->loader_error = NULL;
  if (flags & ~(RTLD_NOW | RTLD_LAZY | RTLD_GLOBAL | RTLD_NOLOAD)) {
    tls->loader_error = "unsupported dynamic loader flags";
    return NULL;
  }
  if (!runtime_linker) {
    tls->loader_error = "no runtime linker";
    return NULL;
  }
  runtime_linker->lock(true);
  void *handle = runtime_linker->load(path, flags, __builtin_return_address(0));
  runtime_linker->lock(false);
  return handle;
}
void *dlsym(void *handle, const char *name) {
  tls_control_t *tls = tls_current();
  tls->loader_error = NULL;
  if (!name || !runtime_linker) {
    tls->loader_error = "invalid dynamic symbol lookup";
    return NULL;
  }
  void *address = NULL;
  runtime_linker->lock(true);
  int result = runtime_linker->symbol(handle, name, __builtin_return_address(0),
                                      &address);
  runtime_linker->lock(false);
  if (result) {
    tls->loader_error = "symbol not found";
    return NULL;
  }
  return address;
}
int dlclose(void *handle) {
  tls_control_t *tls = tls_current();
  int result = -1;
  if (runtime_linker) {
    runtime_linker->lock(true);
    result = runtime_linker->close(handle);
    runtime_linker->lock(false);
  }
  tls->loader_error = result ? "invalid dynamic library handle" : NULL;
  return result;
}
char *dlerror(void) {
  tls_control_t *tls = tls_current();
  const char *error = tls->loader_error;
  tls->loader_error = NULL;
  return (char *)error;
}
int dladdr(const void *address, Dl_info *information) {
  if (!runtime_linker || !information)
    return 0;
  runtime_linker->lock(true);
  int result = runtime_linker->address_info(address, information);
  runtime_linker->lock(false);
  return result;
}
