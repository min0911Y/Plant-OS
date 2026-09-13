#ifndef PLANT_LDSO_H
#define PLANT_LDSO_H
#include <elf.h>
#include <loader.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <vm.h>

typedef struct object object_t;
typedef void (*initializer_t)(void);

struct object {
  object_t *next, *parent;
  const char *path, *soname;
  uintptr_t bias, entry;
  Elf_Phdr *segments;
  size_t segment_count;
  uintptr_t tags[DT_RELRENT + 1];
  uint64_t present;
  Elf_Dyn *dynamic;
  size_t dynamic_count;
  const char *strings;
  Elf_Sym *symbols;
  size_t symbol_count;
  uint32_t *hash, *gnu_hash;
  object_t **dependencies;
  size_t dependency_count;
  enum { OBJECT_NEW, OBJECT_VISITING, OBJECT_INITIALIZED } state;
  bool symbolic, linked;
  const Elf_Phdr *tls;
  size_t tls_module, tls_offset;
};

typedef struct {
  object_t *first, *last;
  object_t **initialized;
  size_t count, initialized_count, initialized_capacity;
  char *arena;
  size_t available;
  const char *cwd;
  const char *library_path;
  size_t tls_count, tls_size, tls_alignment;
} linker_t;
extern linker_t linker;

__attribute__((noreturn)) void fail(const char *object, const char *reason);
void *allocate(size_t size);
char *absolute_path(const char *name);
/* Returns a checked pointer into a single PT_LOAD, never a hole or padding. */
void *object_at(const object_t *object, uintptr_t address, size_t size,
                unsigned flags);
const char *object_string(const object_t *object, size_t offset);
object_t *object_load(int descriptor, const char *path, object_t *parent);
object_t *object_open_path(object_t *parent, const char *path);
object_t *object_dependency(object_t *parent, const char *name);
object_t *object_search_path(object_t *parent, const char *paths,
                             const char *name);
void object_resolve_dependencies(object_t *first);

#endif
