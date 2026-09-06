#include "ldso.h"
#include <fcntl.h>
#include <runtime_args.h>

void abi_alloc_init(void);

typedef struct {
  object_t *object;
  const Elf_Sym *symbol;
  uintptr_t address;
} definition_t;

static bool symbol_exported(object_t *object, size_t index, const char *name) {
  const Elf_Sym *symbol = object->symbols + index;
  unsigned binding = ELF32_ST_BIND(symbol->st_info);
  unsigned visibility = symbol->st_other & 3;
  return symbol->st_shndx != SHN_UNDEF &&
         (binding == STB_GLOBAL || binding == STB_WEAK) &&
         visibility != STV_HIDDEN && visibility != STV_INTERNAL &&
         !strcmp(object_string(object, symbol->st_name), name);
}

static const Elf_Sym *object_lookup(object_t *object, const char *name,
                                    uint32_t sysv, uint32_t gnu) {
  if (object->gnu_hash) {
    uint32_t *hash = object->gnu_hash;
    uintptr_t *bloom = (void *)(hash + 4);
    unsigned bits = sizeof(uintptr_t) * 8;
    uintptr_t mask = ((uintptr_t)1 << (gnu % bits)) |
                     ((uintptr_t)1 << ((gnu >> hash[3]) % bits));
    if ((bloom[(gnu / bits) & (hash[2] - 1)] & mask) != mask)
      return NULL;
    uint32_t *buckets = (void *)(bloom + hash[2]);
    uint32_t *chains = buckets + hash[0];
    size_t index = buckets[gnu % hash[0]];
    if (!index)
      return NULL;
    if (index < hash[1])
      fail(object->path, "invalid GNU hash bucket");
    for (; index < object->symbol_count; index++) {
      uintptr_t address =
          (uintptr_t)chains - object->bias + (index - hash[1]) * 4;
      uint32_t value = *(uint32_t *)object_at(object, address, 4, PF_R);
      if ((value | 1) == (gnu | 1) && symbol_exported(object, index, name))
        return object->symbols + index;
      if (value & 1)
        return NULL;
    }
    fail(object->path, "unterminated GNU hash chain");
  }
  uint32_t *buckets = object->hash + 2;
  uint32_t *chains = buckets + object->hash[0];
  size_t index = buckets[sysv % object->hash[0]];
  for (size_t visited = 0; index && visited < object->symbol_count; visited++) {
    if (index >= object->symbol_count)
      fail(object->path, "invalid SysV hash chain");
    if (symbol_exported(object, index, name))
      return object->symbols + index;
    index = chains[index];
  }
  if (index)
    fail(object->path, "cyclic SysV hash chain");
  return NULL;
}

static definition_t symbol_definition(object_t *object, const Elf_Sym *symbol) {
  unsigned type = ELF32_ST_TYPE(symbol->st_info);
  if (type != STT_NOTYPE && type != STT_OBJECT && type != STT_FUNC)
    fail(object->path, "unsupported symbol type (TLS/IFUNC)");
  uintptr_t address;
  if (symbol->st_shndx == SHN_ABS) {
    address = symbol->st_value;
  } else {
    if (symbol->st_shndx >= 0xff00 || symbol->st_shndx == SHN_UNDEF)
      fail(object->path, "invalid symbol definition");
    address = (uintptr_t)object_at(object, symbol->st_value, symbol->st_size,
                                   type == STT_FUNC ? PF_X : PF_R);
  }
  return (definition_t){object, symbol, address};
}

static definition_t symbol_resolve(object_t *requester, size_t index,
                                   bool copy) {
  if (index >= requester->symbol_count)
    fail(requester->path, "relocation symbol index out of range");
  const Elf_Sym *reference = requester->symbols + index;
  unsigned binding = ELF32_ST_BIND(reference->st_info);
  unsigned visibility = reference->st_other & 3;
  if (binding != STB_LOCAL && binding != STB_GLOBAL && binding != STB_WEAK)
    fail(requester->path, "unsupported symbol binding");
  unsigned type = ELF32_ST_TYPE(reference->st_info);
  if (type != STT_NOTYPE && type != STT_FUNC && type != STT_OBJECT)
    fail(requester->path, "unsupported symbol type (TLS/IFUNC)");
  if (reference->st_shndx == SHN_UNDEF &&
      (binding == STB_LOCAL || visibility != STV_DEFAULT)) {
    if (binding == STB_WEAK && !copy)
      return (definition_t){0};
    fail(requester->path, "undefined local or hidden symbol");
  }
  if (!copy && reference->st_shndx != SHN_UNDEF &&
      (binding == STB_LOCAL || visibility != STV_DEFAULT ||
       requester->symbolic))
    return symbol_definition(requester, reference);
  const char *name = object_string(requester, reference->st_name);
  uint32_t sysv = 0, gnu = 5381;
  for (const unsigned char *s = (const void *)name; *s; s++) {
    sysv = (sysv << 4) + *s;
    uint32_t high = sysv & 0xf0000000;
    if (high)
      sysv ^= high >> 24;
    sysv &= ~high;
    gnu = gnu * 33 + *s;
  }
  for (object_t *object = linker.first; object; object = object->next) {
    if (copy && object == requester)
      continue;
    const Elf_Sym *symbol = object_lookup(object, name, sysv, gnu);
    /* ELF runtime lookup binds to the first global-scope definition, including
     * weak definitions; it does not reproduce the static linker's selection. */
    if (symbol)
      return symbol_definition(object, symbol);
  }
  if (binding == STB_WEAK && !copy)
    return (definition_t){0};
  fail(name, "undefined symbol");
}

static void relocate_one(object_t *object, uintptr_t offset, uint64_t info,
                         intptr_t addend, bool explicit_addend, bool copies) {
#if __SIZEOF_POINTER__ == 8
  unsigned type = ELF64_R_TYPE(info);
  size_t index = ELF64_R_SYM(info);
  size_t width =
      type == R_X86_64_PC32 || type == R_X86_64_32 || type == R_X86_64_32S ? 4
                                                                           : 8;
#else
  unsigned type = ELF32_R_TYPE(info);
  size_t index = ELF32_R_SYM(info);
  size_t width = 4;
#endif
  /* NONE, COPY, GLOB_DAT, JUMP_SLOT and RELATIVE use the same numbers on x86.
   */
  if (!type || (type == R_386_COPY) != copies)
    return;
  if (copies) {
    if (object != linker.first || index >= object->symbol_count)
      fail(object->path, "invalid COPY relocation");
    const Elf_Sym *target = object->symbols + index;
    definition_t source = symbol_resolve(object, index, true);
    if (ELF32_ST_TYPE(target->st_info) != STT_OBJECT ||
        target->st_value != offset || target->st_size != source.symbol->st_size)
      fail(object->path, "COPY relocation size mismatch");
    memcpy(object_at(object, offset, target->st_size, PF_W),
           (const void *)source.address, target->st_size);
    return;
  }
  void *target = object_at(object, offset, width, PF_W);
  if (!explicit_addend) {
    addend = 0;
    memcpy(&addend, target, width);
  }
  uintptr_t value;
  if (type == R_386_RELATIVE) {
    if (index)
      fail(object->path, "RELATIVE relocation has a symbol");
    value = object->bias + (uintptr_t)addend;
  } else {
    definition_t definition =
        index ? symbol_resolve(object, index, false) : (definition_t){0};
    uintptr_t symbol = definition.address;
    switch (type) {
    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT:
      value = symbol;
#if __SIZEOF_POINTER__ == 8
      value += addend;
#endif
      break;
    case R_386_32: /* R_X86_64_64 */
      value = symbol + addend;
      break;
    case R_386_PC32:
      value = symbol + addend - (uintptr_t)target;
#if __SIZEOF_POINTER__ == 8
      if ((int64_t)value != (int64_t)(int32_t)value)
        fail(object->path, "PC32 relocation overflow");
#endif
      break;
#if __SIZEOF_POINTER__ == 8
    case R_X86_64_32:
    case R_X86_64_32S:
      value = symbol + addend;
      if ((type == R_X86_64_32 && value != (uint32_t)value) ||
          (type == R_X86_64_32S && (int64_t)value != (int64_t)(int32_t)value))
        fail(object->path, "32-bit relocation overflow");
      break;
#endif
    default:
      fail(object->path, "unsupported relocation type");
    }
  }
  memcpy(target, &value, width);
}

static void relocate_table(object_t *object, uintptr_t address, size_t size,
                           bool rela, bool copies) {
#if __SIZEOF_POINTER__ == 8
  typedef Elf64_Rel Rel;
  typedef Elf64_Rela Rela;
#else
  typedef Elf32_Rel Rel;
  typedef Elf32_Rela Rela;
#endif
  size_t entry_size = rela ? sizeof(Rela) : sizeof(Rel);
  if (!size)
    return;
  if (!address || size % entry_size)
    fail(object->path, "invalid relocation table");
  const char *table = object_at(object, address, size, PF_R);
  for (size_t offset = 0; offset < size; offset += entry_size) {
    /* memcpy permits unaligned ELF tables and takes a snapshot before a write.
     */
    if (rela) {
      Rela entry;
      memcpy(&entry, table + offset, sizeof(entry));
      relocate_one(object, entry.r_offset, entry.r_info, entry.r_addend, true,
                   copies);
    } else {
      Rel entry;
      memcpy(&entry, table + offset, sizeof(entry));
      relocate_one(object, entry.r_offset, entry.r_info, 0, false, copies);
    }
  }
}

static void object_relocate(object_t *object, bool copies) {
  uintptr_t *d = object->tags;
  if ((d[DT_RELSZ] && d[DT_RELENT] != 2 * sizeof(uintptr_t)) ||
      (d[DT_RELASZ] && d[DT_RELAENT] != 3 * sizeof(uintptr_t)) ||
      (d[DT_PLTRELSZ] && d[DT_PLTREL] != DT_REL && d[DT_PLTREL] != DT_RELA))
    fail(object->path, "invalid relocation entry size");
  uintptr_t addresses[] = {d[DT_REL], d[DT_RELA], d[DT_JMPREL]};
  size_t sizes[] = {d[DT_RELSZ], d[DT_RELASZ], d[DT_PLTRELSZ]};
  for (size_t i = 0; i < 3; i++) {
    if (!sizes[i])
      continue;
    object_at(object, addresses[i], sizes[i], PF_R);
    for (size_t j = 0; j < i; j++) {
      if (sizes[j] && addresses[i] < addresses[j] + sizes[j] &&
          addresses[j] < addresses[i] + sizes[i])
        fail(object->path, "overlapping relocation tables");
    }
  }
  relocate_table(object, d[DT_REL], d[DT_RELSZ], false, copies);
  relocate_table(object, d[DT_RELA], d[DT_RELASZ], true, copies);
  relocate_table(object, d[DT_JMPREL], d[DT_PLTRELSZ], d[DT_PLTREL] == DT_RELA,
                 copies);
}

static void object_protect(object_t *object) {
  for (size_t i = 0; i < object->segment_count; i++) {
    const Elf_Phdr *p = object->segments + i;
    if (p->p_type != PT_LOAD || !p->p_memsz)
      continue;
    uintptr_t start =
        (object->bias + p->p_vaddr) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
    uintptr_t end =
        (object->bias + p->p_vaddr + p->p_memsz + VM_PAGE_SIZE - 1) &
        ~(uintptr_t)(VM_PAGE_SIZE - 1);
    unsigned protection = VM_READ | (p->p_flags & PF_W ? VM_WRITE : 0) |
                          (p->p_flags & PF_X ? VM_EXEC : 0);
    if (vm_protect((void *)start, end - start, protection))
      fail(object->path, "cannot protect load segment");
  }
  for (size_t i = 0; i < object->segment_count; i++) {
    const Elf_Phdr *p = object->segments + i;
    if (p->p_type != PT_GNU_RELRO || !p->p_memsz)
      continue;
    if (p->p_memsz > UINTPTR_MAX - p->p_vaddr)
      fail(object->path, "RELRO range overflow");
    uintptr_t start =
        (object->bias + p->p_vaddr) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
    uintptr_t end = (object->bias + p->p_vaddr + p->p_memsz) &
                    ~(uintptr_t)(VM_PAGE_SIZE - 1);
    /* GNU ld may extend RELRO into zero padding at the end of a LOAD page.
     * Validate ownership of the pages being protected, not just file bytes. */
    for (uintptr_t page = start; page < end; page += VM_PAGE_SIZE) {
      bool covered = false;
      for (size_t j = 0; j < object->segment_count; j++) {
        const Elf_Phdr *load = object->segments + j;
        uintptr_t first =
            (object->bias + load->p_vaddr) & ~(uintptr_t)(VM_PAGE_SIZE - 1);
        uintptr_t limit =
            (object->bias + load->p_vaddr + load->p_memsz + VM_PAGE_SIZE - 1) &
            ~(uintptr_t)(VM_PAGE_SIZE - 1);
        if (load->p_type == PT_LOAD && load->p_memsz &&
            !(load->p_flags & PF_X) && page >= first && page < limit) {
          covered = true;
          break;
        }
      }
      if (!covered)
        fail(object->path, "RELRO outside load pages");
    }
    if (end > start && vm_protect((void *)start, end - start, VM_READ))
      fail(object->path, "cannot protect RELRO segment");
  }
}

static void call_initializer(uintptr_t address) {
  if (!address || address == UINTPTR_MAX)
    return;
  for (object_t *object = linker.first; object; object = object->next) {
    for (size_t i = 0; i < object->segment_count; i++) {
      const Elf_Phdr *p = object->segments + i;
      uintptr_t start = object->bias + p->p_vaddr;
      if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && address >= start &&
          address - start < p->p_memsz) {
        ((initializer_t)address)();
        return;
      }
    }
  }
  fail(NULL, "initializer is not executable");
}

static void call_array(object_t *object, unsigned address_tag,
                       unsigned size_tag, bool reverse) {
  size_t size = object->tags[size_tag];
  if (!size)
    return;
  if (size % sizeof(uintptr_t) || object->tags[address_tag] % sizeof(uintptr_t))
    fail(object->path, "invalid initializer array");
  uintptr_t *entries = object_at(object, object->tags[address_tag], size, PF_R);
  size_t count = size / sizeof(*entries);
  for (size_t i = 0; i < count; i++)
    call_initializer(entries[reverse ? count - 1 - i : i]);
}

static void initialize(void) {
  typedef struct {
    object_t *object;
    size_t dependency;
  } frame_t;
  if (linker.count > SIZE_MAX / sizeof(frame_t))
    fail(NULL, "dependency graph size overflow");
  frame_t *stack = allocate(linker.count * sizeof(*stack));
  size_t depth = 1;
  stack[0].object = linker.first;
  linker.first->state = OBJECT_VISITING;
  call_array(linker.first, DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ, false);
  while (depth) {
    frame_t *frame = stack + depth - 1;
    object_t *object = frame->object;
    if (frame->dependency < object->dependency_count) {
      object_t *dependency = object->dependencies[frame->dependency++];
      if (dependency->state == OBJECT_NEW) {
        dependency->state = OBJECT_VISITING;
        stack[depth++] = (frame_t){dependency, 0};
      }
      continue;
    }
    /* Mark before calling user code so exit() from a constructor is defined. */
    object->state = OBJECT_INITIALIZED;
    linker.initialized[linker.initialized_count++] = object;
    if (object->tags[DT_INIT])
      call_initializer(object->bias + object->tags[DT_INIT]);
    call_array(object, DT_INIT_ARRAY, DT_INIT_ARRAYSZ, false);
    depth--;
  }
}

static void finalize(void) {
  while (linker.initialized_count) {
    object_t *object = linker.initialized[--linker.initialized_count];
    call_array(object, DT_FINI_ARRAY, DT_FINI_ARRAYSZ, true);
    if (object->tags[DT_FINI])
      call_initializer(object->bias + object->tags[DT_FINI]);
  }
}

void Main(const loader_start_t *start) {
  bool verify = start == NULL;
  loader_start_t request;
  runtime_arguments_t arguments;
  if (verify) {
    /* Standalone verification never enters an application, so it can use its
     * own ordinary heap to parse the command line. Normal startup uses only
     * the linker's independent VM arena. */
    abi_alloc_init();
    if (runtime_arguments_load(&arguments) || arguments.argc != 3 ||
        strcmp(arguments.argv[1], "--verify"))
      fail(NULL, "usage: ld.so --verify program");
    request =
        (loader_start_t){sizeof(request), open(arguments.argv[2], O_RDONLY),
                         arguments.argv[2], arguments.argv[0]};
    if (request.executable_fd < 0)
      fail(arguments.argv[2], "cannot open executable");
    start = &request;
  }
  if (!start || start->size != sizeof(*start) || start->executable_fd < 3)
    fail(NULL, "invoke a program with PT_INTERP=/lib/ld.so");
  char *directory = absolute_path(start->interpreter_path);
  char *separator = strrchr(directory, '/');
  if (!separator)
    fail(directory, "invalid interpreter path");
  separator[1] = 0;
  linker.library_path = directory;
  object_load(start->executable_fd, absolute_path(start->executable_path),
              NULL);
  /* Appending objects while iterating gives breadth-first global symbol scope.
   * Register each object before its dependencies to break dependency cycles. */
  for (object_t *object = linker.first; object; object = object->next) {
    size_t needed = 0;
    for (size_t i = 0; i < object->dynamic_count; i++) {
      const Elf_Dyn *d = object->dynamic + i;
      if (d->d_tag == DT_NEEDED)
        object->dependencies[needed++] =
            object_dependency(object, object_string(object, d->d_un.d_val));
    }
  }
  for (object_t *object = linker.first; object; object = object->next)
    object_relocate(object, false);
  for (object_t *object = linker.first; object; object = object->next)
    object_relocate(object, true);
  for (object_t *object = linker.first; object; object = object->next)
    object_protect(object);
  if (verify) {
    print("ld.so: verified ");
    print((char *)start->executable_path);
    print("\n");
    runtime_arguments_destroy(&arguments);
    _exit(0);
  }
  if (linker.count > SIZE_MAX / sizeof(object_t *))
    fail(NULL, "dependency graph size overflow");
  linker.initialized = allocate(linker.count * sizeof(object_t *));
  static const runtime_linker_t hooks = {initialize, finalize};
  ((void (*)(const runtime_linker_t *))linker.first->entry)(&hooks);
  fail(NULL, "application entry returned");
}
