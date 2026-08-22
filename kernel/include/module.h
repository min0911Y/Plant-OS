#ifndef _MODULE_H
#define _MODULE_H

#include <ctypes.h>
#include <stddef.h>

#define MODULE_NAME_MAX 31
#define MODULE_PATH_MAX 255
#define MODULE_INFO_MAGIC 0x4d4f444cU
#define MODULE_API_VERSION 1
#define MAX_MODULES 32
#define MAX_MODULE_EXPORTS 256

typedef struct module_export {
  const char *name;
  uintptr_t addr;
} module_export_t;

typedef struct module_info {
  uint32_t magic;
  uint32_t api_version;
  const char *name;
  int (*init)(void);
  int (*exit)(void);
  const module_export_t *exports;
  uint32_t export_count;
} module_info_t;

typedef struct module_handle {
  uint32_t id;
  char name[MODULE_NAME_MAX + 1];
  char path[MODULE_PATH_MAX + 1];
  uint32_t image_size;
  uint32_t section_count;
  uint32_t export_count;
} module_handle_t;

typedef struct module_exported_symbol {
  const char *name;
  uintptr_t addr;
} module_exported_symbol_t;

void module_init_system(void);
int module_load(const char *path);
int module_unload(const char *name);
int module_list(module_handle_t *out, int max_count);
uintptr_t module_resolve_symbol(const char *name);
bool module_register_kernel_symbol(const char *name, uintptr_t addr);

#define MODULE_INFO_SECTION __attribute__((section(".modinfo")))

#define MODULE_INFO_DEFINE(_module_name, _init, _exit, _exports)               \
  const module_info_t __module_info MODULE_INFO_SECTION = {                    \
      MODULE_INFO_MAGIC, MODULE_API_VERSION, _module_name, _init, _exit,      \
      _exports,                                                                \
      (uint32_t)(sizeof(_exports) / sizeof((_exports)[0]))}

#define MODULE_EXPORT_SYMBOL(sym) {(const char *)#sym, (uintptr_t)(sym)}

#endif
