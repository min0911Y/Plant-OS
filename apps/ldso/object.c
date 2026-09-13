#include "ldso.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

linker_t linker;

void fail(const char *object, const char *reason) {
  if (linker.recovery) {
    linker.error = reason;
    longjmp(*linker.recovery, 1);
  }
  print("ld.so: ");
  if (object) {
    print((char *)object);
    print(": ");
  }
  print((char *)reason);
  print("\n");
  /* Reasons are internal constant strings, safe for the kernel's format API. */
  logk("ld.so: ");
  logk((char *)reason);
  logk("\n");
  _exit(127);
  __builtin_unreachable();
}

void *allocate(size_t size) {
  if (size > SIZE_MAX - 15)
    fail(NULL, "metadata size overflow");
  size = (size + 15) & ~(size_t)15;
  if (size > linker.available) {
    if (size > SIZE_MAX - (VM_PAGE_SIZE - 1))
      fail(NULL, "metadata size overflow");
    if (size > SIZE_MAX - sizeof(arena_block_t) - (VM_PAGE_SIZE - 1))
      fail(NULL, "metadata size overflow");
    size_t capacity = (size + sizeof(arena_block_t) + VM_PAGE_SIZE - 1) &
                      ~(size_t)(VM_PAGE_SIZE - 1);
    if (capacity < 4 * VM_PAGE_SIZE)
      capacity = 4 * VM_PAGE_SIZE;
    arena_block_t *block = vm_map(NULL, capacity);
    if (!block)
      fail(NULL, "out of memory");
    *block = (arena_block_t){linker.blocks, capacity};
    linker.blocks = block;
    linker.arena = (char *)(block + 1);
    linker.available = capacity - sizeof(*block);
  }
  void *result = linker.arena;
  linker.arena += size;
  linker.available -= size;
  return result;
}

void arena_restore(const linker_t *saved) {
  while (linker.blocks != saved->blocks) {
    arena_block_t *block = linker.blocks;
    linker.blocks = block->next;
    vm_unmap(block, block->size);
  }
  if (saved->available)
    memset(saved->arena, 0, saved->available);
  linker.arena = saved->arena;
  linker.available = saved->available;
  linker.cwd = saved->cwd;
}

char *absolute_path(const char *name) {
  if (!linker.cwd) {
    vfs_syscall_request_t request = {.size = sizeof(request)};
    int length = vfs_syscall(VFS_SYSCALL_GETCWD, &request);
    if (length < 0)
      fail(name, "cannot read working directory");
    char *cwd = allocate((size_t)length + 3);
    int drive = vfs_syscall(VFS_SYSCALL_CURRENT_DRIVE, &request);
    cwd[0] = drive;
    cwd[1] = ':';
    request.arguments.cwd.buffer = (uintptr_t)(cwd + 2);
    request.arguments.cwd.capacity = (size_t)length + 1;
    if (vfs_syscall(VFS_SYSCALL_GETCWD, &request) != length)
      fail(name, "cannot read working directory");
    linker.cwd = cwd;
  }
  size_t length = strlen(name), prefix = strlen(linker.cwd);
  if (length > SIZE_MAX - prefix - 4)
    fail(name, "path size overflow");
  char *path = allocate(prefix + length + 4);
  bool drive = length >= 2 && name[1] == ':';
  path[0] = drive ? name[0] : linker.cwd[0];
  if (path[0] >= 'a' && path[0] <= 'z')
    path[0] -= 'a' - 'A';
  path[1] = ':';
  path[2] = '/';
  size_t used = 3;
  if (!drive && name[0] != '/' && name[0] != '\\') {
    memcpy(path + 2, linker.cwd + 2, prefix - 2);
    used = prefix;
    if (path[used - 1] != '/')
      path[used++] = '/';
  }
  const char *source = name + (drive ? 2 : 0);
  /* The VFS has no symlinks. Normalize separators and dot components once so
   * aliases in dependency paths do not create duplicate object instances. */
  while (*source) {
    if (*source == '/' || *source == '\\') {
      source++;
      continue;
    }
    const char *component = source;
    while (*source && *source != '/' && *source != '\\')
      source++;
    size_t count = source - component;
    if (count == 1 && component[0] == '.')
      continue;
    if (count == 2 && component[0] == '.' && component[1] == '.') {
      if (used > 3 && path[used - 1] == '/')
        used--;
      while (used > 3 && path[used - 1] != '/')
        used--;
      continue;
    }
    if (path[used - 1] != '/')
      path[used++] = '/';
    memcpy(path + used, component, count);
    used += count;
  }
  if (used > 3 && path[used - 1] == '/')
    used--;
  path[used] = 0;
  return path;
}

void *object_at(const object_t *object, uintptr_t address, size_t size,
                unsigned flags) {
  for (size_t i = 0; i < object->segment_count; i++) {
    const Elf_Phdr *p = object->segments + i;
    if (p->p_type == PT_LOAD && (p->p_flags & flags) == flags &&
        address >= p->p_vaddr && address - p->p_vaddr <= p->p_memsz &&
        size <= p->p_memsz - (address - p->p_vaddr))
      return (void *)(object->bias + address);
  }
  fail(object->path, "ELF address outside its load segment");
}

const char *object_string(const object_t *object, size_t offset) {
  if (offset >= object->tags[DT_STRSZ] ||
      !memchr(object->strings + offset, 0, object->tags[DT_STRSZ] - offset))
    fail(object->path, "invalid dynamic string");
  return object->strings + offset;
}

static void read_at(int fd, uintptr_t offset, void *buffer, size_t size,
                    const char *path) {
  if (offset > INT_MAX || size > INT_MAX || lseek(fd, offset, SEEK_SET) < 0 ||
      read(fd, buffer, size) != size)
    fail(path, "cannot read ELF image");
}

static void dynamic_parse(object_t *object, const Elf_Phdr *segment) {
  if (segment->p_filesz < sizeof(Elf_Dyn) ||
      segment->p_filesz > segment->p_memsz ||
      segment->p_filesz % sizeof(Elf_Dyn) ||
      segment->p_vaddr % _Alignof(Elf_Dyn))
    fail(object->path, "invalid dynamic table");
  object->dynamic =
      object_at(object, segment->p_vaddr, segment->p_filesz, PF_R);
  object->dynamic_count = segment->p_filesz / sizeof(Elf_Dyn);
  bool terminated = false;
  for (size_t i = 0; i < object->dynamic_count; i++) {
    const Elf_Dyn *d = object->dynamic + i;
    uintptr_t tag = d->d_tag, value = d->d_un.d_val;
    if (tag == DT_NULL) {
      object->dynamic_count = i;
      terminated = true;
      break;
    }
    switch (tag) {
    case DT_NEEDED:
      object->dependency_count++;
      continue;
    case DT_GNU_HASH:
      if (object->gnu_hash || value % sizeof(uintptr_t))
        fail(object->path, "invalid or duplicate GNU hash table");
      object->gnu_hash = object_at(object, value, 4 * sizeof(uint32_t), PF_R);
      continue;
    case DT_VERSYM:
    case DT_VERDEF:
    case DT_VERDEFNUM:
    case DT_VERNEED:
    case DT_VERNEEDNUM:
      /* Plant's ABI resolves symbols by name and has no version namespace.
       * Ignore GNU version metadata while retaining the ordinary symbol
       * table and relocation information. */
      continue;
    case DT_RELR:
    case DT_RELRSZ:
    case DT_RELRENT:
      fail(object->path, "packed RELR relocations are not supported");
    case DT_TEXTREL:
      fail(object->path, "text relocations are not supported");
    case DT_FLAGS_1:
      if (value & ~(DF_1_NOW | DF_1_ORIGIN | DF_1_PIE))
        fail(object->path, "unsupported dynamic flags");
      continue;
    case DT_RELCOUNT:
    case DT_RELACOUNT:
      continue; /* Optimization hints; every relocation is checked below. */
    case DT_HASH:
    case DT_STRTAB:
    case DT_SYMTAB:
    case DT_STRSZ:
    case DT_SYMENT:
    case DT_REL:
    case DT_RELSZ:
    case DT_RELENT:
    case DT_RELA:
    case DT_RELASZ:
    case DT_RELAENT:
    case DT_JMPREL:
    case DT_PLTREL:
    case DT_PLTRELSZ:
    case DT_PLTGOT:
    case DT_INIT:
    case DT_FINI:
    case DT_INIT_ARRAY:
    case DT_FINI_ARRAY:
    case DT_INIT_ARRAYSZ:
    case DT_FINI_ARRAYSZ:
    case DT_PREINIT_ARRAY:
    case DT_PREINIT_ARRAYSZ:
    case DT_SONAME:
    case DT_RPATH:
    case DT_RUNPATH:
    case DT_SYMBOLIC:
    case DT_BIND_NOW:
    case DT_FLAGS:
    case DT_DEBUG:
      break;
    default:
      fail(object->path, "unsupported dynamic tag");
    }
    if (object->present & ((uint64_t)1 << tag))
      fail(object->path, "duplicate dynamic tag");
    object->present |= (uint64_t)1 << tag;
    object->tags[tag] = value;
  }
  if (!terminated || !object->tags[DT_STRSZ] || !object->tags[DT_STRTAB] ||
      !object->tags[DT_SYMTAB] || object->tags[DT_SYMENT] != sizeof(Elf_Sym) ||
      object->tags[DT_SYMTAB] % _Alignof(Elf_Sym))
    fail(object->path, "incomplete dynamic table");
  if (object->tags[DT_FLAGS] & ~(DF_ORIGIN | DF_SYMBOLIC | DF_BIND_NOW))
    fail(object->path, "unsupported dynamic flags");
  object->symbolic = (object->present & ((uint64_t)1 << DT_SYMBOLIC)) ||
                     (object->tags[DT_FLAGS] & DF_SYMBOLIC);
  object->strings =
      object_at(object, object->tags[DT_STRTAB], object->tags[DT_STRSZ], PF_R);
  if (object->strings[0] || object->strings[object->tags[DT_STRSZ] - 1])
    fail(object->path, "invalid dynamic string table");
  if (object->present & ((uint64_t)1 << DT_SONAME))
    object->soname = object_string(object, object->tags[DT_SONAME]);
  if (object->tags[DT_HASH]) {
    uintptr_t address = object->tags[DT_HASH];
    if (address % _Alignof(uint32_t))
      fail(object->path, "unaligned SysV hash table");
    object->hash = object_at(object, address, 2 * sizeof(uint32_t), PF_R);
    uint64_t words = (uint64_t)2 + object->hash[0] + object->hash[1];
    if (!object->hash[0] || !object->hash[1] ||
        words > SIZE_MAX / sizeof(uint32_t))
      fail(object->path, "invalid SysV hash table");
    object_at(object, address, words * sizeof(uint32_t), PF_R);
    object->symbol_count = object->hash[1];
  }
  if (object->gnu_hash) {
    uint32_t *hash = object->gnu_hash;
    if (!hash[0] || !hash[2] || (hash[2] & (hash[2] - 1)) || hash[3] >= 32)
      fail(object->path, "invalid GNU hash table");
    uint64_t bytes =
        16 + (uint64_t)hash[2] * sizeof(uintptr_t) + (uint64_t)hash[0] * 4;
    if (bytes > SIZE_MAX)
      fail(object->path, "GNU hash table size overflow");
    uintptr_t address = (uintptr_t)hash - object->bias;
    object_at(object, address, bytes, PF_R);
    uint32_t *buckets =
        (void *)((uintptr_t)hash + 16 + (size_t)hash[2] * sizeof(uintptr_t));
    uint32_t maximum = 0;
    for (size_t i = 0; i < hash[0]; i++) {
      if (buckets[i] && buckets[i] < hash[1])
        fail(object->path, "invalid GNU hash bucket");
      if (buckets[i] > maximum)
        maximum = buckets[i];
    }
    size_t count = hash[1];
    if (maximum) {
      count = maximum;
      for (;;) {
        uint64_t offset = bytes + (uint64_t)(count - hash[1]) * 4;
        if (offset > UINTPTR_MAX - address || count == UINT32_MAX)
          fail(object->path, "GNU hash chain overflow");
        uint32_t *chain = object_at(object, address + offset, 4, PF_R);
        count++;
        if (*chain & 1)
          break;
      }
    } else if (!object->symbol_count) {
      /* GNU ld's empty hash has symoffset=1 even when the object imports many
       * symbols. Bound those unhashable entries by the containing load segment
       * and the next dynamic table; section headers are never required. */
      uintptr_t symbols = object->tags[DT_SYMTAB], limit = UINTPTR_MAX;
      for (size_t i = 0; i < object->segment_count; i++) {
        const Elf_Phdr *p = object->segments + i;
        if (p->p_type == PT_LOAD && symbols >= p->p_vaddr &&
            symbols - p->p_vaddr < p->p_filesz)
          limit = p->p_vaddr + p->p_filesz;
      }
      unsigned tables[] = {DT_STRTAB,     DT_REL,          DT_RELA,
                           DT_JMPREL,     DT_HASH,         DT_INIT_ARRAY,
                           DT_FINI_ARRAY, DT_PREINIT_ARRAY};
      for (size_t i = 0; i < sizeof(tables) / sizeof(*tables); i++) {
        uintptr_t table = object->tags[tables[i]];
        if (table > symbols && table < limit)
          limit = table;
      }
      if (address > symbols && address < limit)
        limit = address;
      if (limit == UINTPTR_MAX || limit <= symbols)
        fail(object->path, "invalid dynamic symbol table");
      count = (limit - symbols) / sizeof(Elf_Sym);
    }
    if (object->symbol_count && count > object->symbol_count)
      fail(object->path, "inconsistent symbol hash tables");
    if (!object->symbol_count)
      object->symbol_count = count;
  }
  if (!object->symbol_count ||
      object->symbol_count > SIZE_MAX / sizeof(Elf_Sym))
    fail(object->path, "missing or invalid symbol hash table");
  object->symbols = object_at(object, object->tags[DT_SYMTAB],
                              object->symbol_count * sizeof(Elf_Sym), PF_R);
  if (object->dependency_count > SIZE_MAX / sizeof(object_t *))
    fail(object->path, "dependency count overflow");
  object->dependencies =
      allocate(object->dependency_count * sizeof(object_t *));
}

object_t *object_load(int fd, const char *path, object_t *parent) {
  linker.loading_fd = fd;
  struct stat status;
  Elf_Ehdr header;
  if (fstat(fd, &status) || !S_ISREG(status.st_mode) ||
      status.st_size < sizeof(header))
    fail(path, "invalid ELF file");
  size_t file_size = status.st_size;
  read_at(fd, 0, &header, sizeof(header), path);
  if (memcmp(header.e_ident, "\177ELF", 4) ||
      header.e_ident[EI_CLASS] != ELF_NATIVE_CLASS ||
      header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_ident[EI_VERSION] != EV_CURRENT ||
      header.e_version != EV_CURRENT ||
      header.e_machine != ELF_NATIVE_MACHINE || header.e_type != ET_DYN ||
      header.e_ehsize != sizeof(header) ||
      header.e_phentsize != sizeof(Elf_Phdr) || !header.e_phnum ||
      header.e_phoff > file_size ||
      header.e_phnum > (file_size - header.e_phoff) / sizeof(Elf_Phdr))
    fail(path, "expected a native PIE or shared object");
  object_t *object = allocate(sizeof(*object));
  object->path = path;
  object->parent = parent;
  if (linker.last)
    linker.last->next = object;
  else
    linker.first = object;
  linker.last = object;
  linker.count++;
  object->segment_count = header.e_phnum;
  size_t phsize = header.e_phnum * sizeof(Elf_Phdr);
  object->segments = allocate(phsize);
  read_at(fd, header.e_phoff, object->segments, phsize, path);
  uintptr_t first = UINTPTR_MAX, end = 0, alignment = VM_PAGE_SIZE;
  const Elf_Phdr *dynamic = NULL;
  bool entry_valid = false;
  for (size_t i = 0; i < header.e_phnum; i++) {
    const Elf_Phdr *p = object->segments + i;
    if (p->p_type == PT_GNU_STACK && (p->p_flags & PF_X))
      fail(path, "executable stacks are not supported");
    if (p->p_type == PT_TLS) {
      if (object->tls || p->p_filesz > p->p_memsz ||
          (p->p_align && (p->p_align & (p->p_align - 1))))
        fail(path, "invalid TLS segment");
      object->tls = p;
    }
    if (parent && p->p_type == PT_INTERP)
      fail(path, "a shared library cannot have an interpreter");
    if (p->p_type == PT_DYNAMIC) {
      if (dynamic)
        fail(path, "duplicate dynamic segment");
      dynamic = p;
    }
    if (p->p_type != PT_LOAD)
      continue;
    if (p->p_filesz > p->p_memsz || p->p_offset > file_size ||
        p->p_filesz > file_size - p->p_offset ||
        p->p_memsz > UINTPTR_MAX - p->p_vaddr ||
        p->p_vaddr + p->p_memsz > UINTPTR_MAX - (VM_PAGE_SIZE - 1) ||
        !(p->p_flags & PF_R) || (p->p_flags & ~(PF_R | PF_W | PF_X)) ||
        (p->p_flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
        (p->p_align > 1 && ((p->p_align & (p->p_align - 1)) ||
                            ((p->p_vaddr - p->p_offset) & (p->p_align - 1)))))
      fail(path, "invalid load segment");
    if (!p->p_memsz)
      continue;
    uintptr_t begin = p->p_vaddr & ~(uintptr_t)(VM_PAGE_SIZE - 1);
    uintptr_t limit = (p->p_vaddr + p->p_memsz + VM_PAGE_SIZE - 1) &
                      ~(uintptr_t)(VM_PAGE_SIZE - 1);
    for (size_t j = 0; j < i; j++) {
      const Elf_Phdr *q = object->segments + j;
      if (q->p_type == PT_LOAD && q->p_memsz &&
          begin < ((q->p_vaddr + q->p_memsz + VM_PAGE_SIZE - 1) &
                   ~(uintptr_t)(VM_PAGE_SIZE - 1)) &&
          (q->p_vaddr & ~(uintptr_t)(VM_PAGE_SIZE - 1)) < limit)
        fail(path, "overlapping load pages");
    }
    if (begin < first)
      first = begin;
    if (limit > end)
      end = limit;
    if (p->p_align > alignment)
      alignment = p->p_align;
    if ((p->p_flags & PF_X) && header.e_entry >= p->p_vaddr &&
        header.e_entry - p->p_vaddr < p->p_memsz)
      entry_valid = true;
  }
  if (!dynamic || end <= first || (!parent && !entry_valid) ||
      end - first > SIZE_MAX - alignment)
    fail(path, "missing entry or dynamic load segments");
  size_t span = end - first;
  size_t reservation_size = span + alignment - VM_PAGE_SIZE;
  void *reservation = vm_map(NULL, reservation_size);
  object->mapping = (thread_region_t){(uintptr_t)reservation, reservation_size};
  if (!reservation || (uintptr_t)reservation < first ||
      (uintptr_t)reservation - first > UINTPTR_MAX - alignment + 1)
    fail(path, "cannot map ELF image");
  object->bias =
      ((uintptr_t)reservation - first + alignment - 1) & ~(alignment - 1);
  uintptr_t start = object->bias + first;
  size_t prefix = start - (uintptr_t)reservation;
  if (prefix && vm_unmap(reservation, prefix))
    fail(path, "cannot trim ELF mapping");
  size_t suffix = reservation_size - prefix - span;
  if (suffix && vm_unmap((void *)(start + span), suffix))
    fail(path, "cannot trim ELF mapping");
  object->mapping = (thread_region_t){start, span};
  for (size_t i = 0; i < header.e_phnum; i++) {
    const Elf_Phdr *p = object->segments + i;
    if (p->p_type == PT_LOAD && p->p_filesz)
      read_at(fd, p->p_offset, (void *)(object->bias + p->p_vaddr), p->p_filesz,
              path);
  }
  close(fd);
  linker.loading_fd = -1;
  object->entry = object->bias + header.e_entry;
  dynamic_parse(object, dynamic);
  if (parent && object->tags[DT_PREINIT_ARRAYSZ])
    fail(path, "shared library has a preinit array");
  return object;
}

static object_t *dependency_open(object_t *parent, const char *path) {
  path = absolute_path(path);
  for (object_t *object = linker.first; object; object = object->next) {
    if (!strcmp(object->path, path))
      return object;
  }
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    if (errno != ENOENT && errno != ENOTDIR)
      fail(path, "cannot open shared library");
    return NULL;
  }
  return object_load(fd, path, parent);
}

object_t *object_open_path(object_t *parent, const char *path) {
  return dependency_open(parent, path);
}

static object_t *search_path(object_t *parent, object_t *origin,
                             const char *paths, const char *name) {
  const char *slash = strrchr(origin->path, '/');
  size_t origin_size = slash ? (size_t)(slash - origin->path) : 0;
  size_t name_size = strlen(name);
  while (paths) {
    const char *next = strchr(paths, ':');
    /* A leading drive designator belongs to the path, not the list delimiter.
     */
    if (next == paths + 1 && paths[0] && next[1] == '/')
      next = strchr(next + 1, ':');
    size_t length = next ? (size_t)(next - paths) : strlen(paths);
    size_t capacity = name_size + 2;
    for (size_t i = 0; i < length; i++) {
      size_t token = !strncmp(paths + i, "$ORIGIN", 7)     ? 7
                     : !strncmp(paths + i, "${ORIGIN}", 9) ? 9
                                                           : 0;
      size_t extra = token ? origin_size : 1;
      if (extra > SIZE_MAX - capacity)
        fail(parent->path, "search path size overflow");
      capacity += extra;
      if (token)
        i += token - 1;
    }
    char *path = allocate(capacity);
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
      size_t token = !strncmp(paths + i, "$ORIGIN", 7)     ? 7
                     : !strncmp(paths + i, "${ORIGIN}", 9) ? 9
                                                           : 0;
      if (token) {
        memcpy(path + used, origin->path, origin_size);
        used += origin_size;
        i += token - 1;
      } else if (paths[i] == '$') {
        fail(parent->path, "unsupported search path token");
      } else {
        path[used++] = paths[i];
      }
    }
    if (used)
      path[used++] = '/';
    memcpy(path + used, name, name_size + 1);
    object_t *object = dependency_open(parent, path);
    if (object)
      return object;
    paths = next ? next + 1 : NULL;
  }
  return NULL;
}

object_t *object_search_path(object_t *parent, const char *paths,
                             const char *name) {
  return search_path(parent, parent, paths, name);
}

object_t *object_dependency(object_t *parent, const char *name) {
  if (!*name)
    fail(parent->path, "empty shared library name");
  object_t *result;
  if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) {
    result = dependency_open(parent, name);
  } else {
    for (object_t *object = linker.first; object; object = object->next) {
      if (object->soname && !strcmp(object->soname, name))
        return object;
    }
    result = NULL;
    if (!(parent->present & ((uint64_t)1 << DT_RUNPATH))) {
      for (object_t *source = parent; source && !result;
           source = source->parent) {
        if (source->present & ((uint64_t)1 << DT_RPATH))
          result =
              search_path(parent, source,
                          object_string(source, source->tags[DT_RPATH]), name);
      }
    } else {
      result =
          search_path(parent, parent,
                      object_string(parent, parent->tags[DT_RUNPATH]), name);
    }
    if (!result)
      result = search_path(parent, parent, linker.library_path, name);
  }
  if (!result)
    fail(name, "shared library not found");
  return result;
}

void object_resolve_dependencies(object_t *first) {
  for (object_t *object = first; object; object = object->next) {
    size_t needed = 0;
    for (size_t i = 0; i < object->dynamic_count; i++) {
      const Elf_Dyn *d = object->dynamic + i;
      if (d->d_tag == DT_NEEDED)
        object->dependencies[needed++] =
            object_dependency(object, object_string(object, d->d_un.d_val));
    }
  }
}
