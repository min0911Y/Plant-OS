#include "ldso.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <futex.h>
#include <limits.h>
#include <runtime_args.h>

void abi_alloc_init(void);

typedef struct {
  object_t *object;
  const Elf_Sym *symbol;
  uintptr_t address;
} definition_t;

/* The interpreter owns one recursive lock, independent of libp's allocator and
 * pthread implementation. Fork takes it before the runtime's other locks. */
static uint32_t loader_busy;
static uintptr_t loader_owner;
static size_t loader_depth;

static void loader_lock(bool acquire) {
  uintptr_t owner = native_thread_pointer() + 1;
  if (!acquire) {
    if (--loader_depth == 0) {
      __atomic_store_n(&loader_owner, 0, __ATOMIC_RELAXED);
      __atomic_store_n(&loader_busy, 0, __ATOMIC_RELEASE);
      os_futex_wake(&loader_busy, 1);
    }
    return;
  }
  if (__atomic_load_n(&loader_owner, __ATOMIC_ACQUIRE) == owner) {
    loader_depth++;
    return;
  }
  for (;;) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&loader_busy, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      break;
    os_futex_wait(&loader_busy, 1, UINT64_MAX);
  }
  __atomic_store_n(&loader_owner, owner, __ATOMIC_RELEASE);
  loader_depth = 1;
}

static object_t *object_containing(const void *pointer) {
  uintptr_t address = (uintptr_t)pointer;
  for (object_t *object = linker.first; object; object = object->next)
    for (size_t i = 0; i < object->segment_count; i++) {
      const Elf_Phdr *p = object->segments + i;
      uintptr_t start = object->bias + p->p_vaddr;
      if (p->p_type == PT_LOAD && address >= start &&
          address - start < p->p_memsz)
        return object;
    }
  return NULL;
}

static void object_scope(object_t *root) {
  if (root->scope)
    return;
  if (linker.count > SIZE_MAX / sizeof(object_t *))
    fail(root->path, "symbol scope overflow");
  object_t **scope = allocate(linker.count * sizeof(*scope));
  size_t count = 1;
  scope[0] = root;
  for (size_t i = 0; i < count; i++)
    for (size_t d = 0; d < scope[i]->dependency_count; d++) {
      object_t *dependency = scope[i]->dependencies[d];
      size_t j = 0;
      while (j < count && scope[j] != dependency)
        j++;
      if (j == count)
        scope[count++] = dependency;
    }
  root->scope = scope;
  root->scope_count = count;
}

static void symbol_hash(const char *name, uint32_t *sysv, uint32_t *gnu) {
  *sysv = 0;
  *gnu = 5381;
  for (const unsigned char *s = (const void *)name; *s; s++) {
    *sysv = (*sysv << 4) + *s;
    uint32_t high = *sysv & 0xf0000000;
    if (high)
      *sysv ^= high >> 24;
    *sysv &= ~high;
    *gnu = *gnu * 33 + *s;
  }
}

static bool symbol_exported(object_t *object, size_t index, const char *name) {
  const Elf_Sym *symbol = object->symbols + index;
  unsigned binding = ELF32_ST_BIND(symbol->st_info);
  unsigned visibility = symbol->st_other & 3;
  return symbol->st_shndx != SHN_UNDEF &&
         (binding == STB_GLOBAL || binding == STB_WEAK ||
          binding == STB_GNU_UNIQUE) &&
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
  if (type != STT_NOTYPE && type != STT_OBJECT && type != STT_FUNC &&
      type != STT_TLS)
    fail(object->path, "unsupported symbol type");
  uintptr_t address;
  if (type == STT_TLS) {
    if (!object->tls || symbol->st_value > object->tls->p_memsz ||
        symbol->st_size > object->tls->p_memsz - symbol->st_value)
      fail(object->path, "TLS symbol outside its segment");
    address = symbol->st_value;
  } else if (symbol->st_shndx == SHN_ABS) {
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
  if (binding != STB_LOCAL && binding != STB_GLOBAL && binding != STB_WEAK &&
      binding != STB_GNU_UNIQUE)
    fail(requester->path, "unsupported symbol binding");
  unsigned type = ELF32_ST_TYPE(reference->st_info);
  if (type != STT_NOTYPE && type != STT_FUNC && type != STT_OBJECT &&
      type != STT_TLS)
    fail(requester->path, "unsupported symbol type");
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
  uint32_t sysv, gnu;
  symbol_hash(name, &sysv, &gnu);
  for (object_t *object = linker.first; object; object = object->next) {
    if (!object->global || (copy && object == requester))
      continue;
    const Elf_Sym *symbol = object_lookup(object, name, sysv, gnu);
    if (symbol)
      return symbol_definition(object, symbol);
  }
  object_t *root = requester->scope_owner;
  for (size_t i = 0; root && i < root->scope_count; i++) {
    object_t *object = root->scope[i];
    if (copy && object == requester)
      continue;
    const Elf_Sym *symbol = object_lookup(object, name, sysv, gnu);
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
    object_t *module = definition.object ? definition.object : object;
    switch (type) {
#if __SIZEOF_POINTER__ == 8
    case R_X86_64_DTPMOD64:
    case R_X86_64_DTPOFF64:
    case R_X86_64_TPOFF64:
#else
    case R_386_TLS_DTPMOD32:
    case R_386_TLS_DTPOFF32:
    case R_386_TLS_TPOFF:
    case R_386_TLS_TPOFF32:
#endif
      if (!module->tls ||
          (index && (!definition.symbol ||
                     ELF32_ST_TYPE(definition.symbol->st_info) != STT_TLS)))
        fail(object->path, "TLS relocation without a TLS definition");
#if __SIZEOF_POINTER__ == 8
      value = type == R_X86_64_DTPMOD64 ? module->tls_module : symbol + addend;
      if (type == R_X86_64_TPOFF64) {
        if (!module->tls_offset)
          fail(object->path, "late initial-exec TLS requires startup loading");
        value -= module->tls_offset;
      }
#else
      value = type == R_386_TLS_DTPMOD32 ? module->tls_module : symbol + addend;
      if ((type == R_386_TLS_TPOFF || type == R_386_TLS_TPOFF32) &&
          !module->tls_offset)
        fail(object->path, "late initial-exec TLS requires startup loading");
      if (type == R_386_TLS_TPOFF)
        value -= module->tls_offset;
      if (type == R_386_TLS_TPOFF32)
        value = module->tls_offset - symbol + addend;
#endif
      break;
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

static void initialize_object_tree(object_t *root) {
  if (!root || root->state != OBJECT_NEW)
    return;
  typedef struct {
    object_t *object;
    size_t dependency;
  } frame_t;
  if (linker.count > SIZE_MAX / sizeof(frame_t))
    fail(NULL, "dependency graph size overflow");
  frame_t *stack = allocate(linker.count * sizeof(*stack));
  size_t depth = 1;
  stack[0].object = root;
  root->state = OBJECT_VISITING;
  if (root == linker.first)
    call_array(root, DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ, false);
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

static void initialize(void) { initialize_object_tree(linker.first); }

static void finalize(void) {
  while (linker.initialized_count) {
    object_t *object = linker.initialized[--linker.initialized_count];
    call_array(object, DT_FINI_ARRAY, DT_FINI_ARRAYSZ, true);
    if (object->tags[DT_FINI])
      call_initializer(object->bias + object->tags[DT_FINI]);
  }
}

static void prepare_tls(object_t *first, bool startup) {
  for (object_t *object = first; object; object = object->next) {
    const Elf_Phdr *tls = object->tls;
    if (!tls || object->tls_module)
      continue;
    size_t alignment = tls->p_align ? tls->p_align : 1;
    size_t skew = tls->p_vaddr & (alignment - 1);
    if (tls->p_memsz > SIZE_MAX - linker.tls_size ||
        linker.tls_size + tls->p_memsz > SIZE_MAX - skew ||
        linker.tls_size + tls->p_memsz + skew > SIZE_MAX - (alignment - 1))
      fail(object->path, "TLS layout overflow");
    if (startup) {
      object->tls_offset =
          ((linker.tls_size + tls->p_memsz + skew + alignment - 1) &
           ~(alignment - 1)) -
          skew;
      linker.tls_size = object->tls_offset;
      if (alignment > linker.tls_alignment)
        linker.tls_alignment = alignment;
    }
    object->tls_module = ++linker.tls_count;
    if (tls->p_filesz) {
      object_at(object, tls->p_vaddr, tls->p_filesz, PF_R);
      bool covered = false;
      for (size_t i = 0; i < object->segment_count; i++) {
        const Elf_Phdr *load = object->segments + i;
        if (load->p_type != PT_LOAD || tls->p_vaddr < load->p_vaddr)
          continue;
        uintptr_t offset = tls->p_vaddr - load->p_vaddr;
        if (offset <= load->p_filesz &&
            tls->p_filesz <= load->p_filesz - offset &&
            tls->p_offset >= load->p_offset &&
            tls->p_offset - load->p_offset == offset)
          covered = true;
      }
      if (!covered)
        fail(object->path, "TLS template outside file-backed load segment");
    }
  }
}

static tls_control_t *tls_allocate(size_t runtime_size,
                                   thread_region_t *region) {
  size_t alignment = linker.tls_alignment;
  size_t count = linker.static_tls_count + 1;
  if (runtime_size > SIZE_MAX - sizeof(tls_control_t) - sizeof(void *) ||
      count > SIZE_MAX / sizeof(void *))
    return NULL;
  size_t control = (sizeof(tls_control_t) + runtime_size + sizeof(void *) - 1) &
                   ~(sizeof(void *) - 1);
  size_t vectors = count * sizeof(void *);
  if (control > SIZE_MAX - vectors ||
      control + vectors > SIZE_MAX - linker.tls_size ||
      control + vectors + linker.tls_size > SIZE_MAX - alignment ||
      control + vectors + linker.tls_size + alignment > SIZE_MAX - VM_PAGE_SIZE)
    return NULL;
  size_t size =
      (control + vectors + linker.tls_size + alignment + VM_PAGE_SIZE - 1) &
      ~(size_t)(VM_PAGE_SIZE - 1);
  char *memory = vm_map(NULL, size);
  if (!memory)
    return NULL;
  tls_control_t *pointer =
      (void *)(((uintptr_t)memory + linker.tls_size + alignment - 1) &
               ~(uintptr_t)(alignment - 1));
  pointer->self = pointer;
  pointer->module_count = linker.static_tls_count;
  pointer->modules = (void **)((char *)pointer + control);
  for (object_t *object = linker.first; object; object = object->next) {
    if (!object->tls_offset)
      continue;
    void *base = (char *)pointer - object->tls_offset;
    pointer->modules[object->tls_module] = base;
    if (object->tls->p_filesz)
      memcpy(base, (const void *)(object->bias + object->tls->p_vaddr),
             object->tls->p_filesz);
  }
  *region = (thread_region_t){(uintptr_t)memory, size};
  return pointer;
}

typedef struct tls_allocation {
  struct tls_allocation *next;
  size_t size;
} tls_allocation_t;

static void *tls_address(const tls_index_t *index) {
  tls_control_t *tls = tls_current();
  loader_lock(true);
  object_t *module = linker.first;
  while (module && module->tls_module != index->module)
    module = module->next;
  if (!index->module || !module || index->offset >= module->tls->p_memsz)
    fail(NULL, "invalid TLS index");
  if (index->module > tls->module_count) {
    if (linker.tls_count > SIZE_MAX / sizeof(void *) - 1)
      fail(NULL, "TLS vector overflow");
    size_t size = (linker.tls_count + 1) * sizeof(void *);
    if (size > SIZE_MAX - VM_PAGE_SIZE + 1)
      fail(NULL, "TLS vector overflow");
    size = (size + VM_PAGE_SIZE - 1) & ~(size_t)(VM_PAGE_SIZE - 1);
    void **vector = vm_map(NULL, size);
    if (!vector)
      fail(NULL, "cannot allocate TLS vector");
    memcpy(vector, tls->modules, (tls->module_count + 1) * sizeof(void *));
    if (tls->dynamic_vector.size)
      vm_unmap((void *)tls->dynamic_vector.address, tls->dynamic_vector.size);
    tls->modules = vector;
    tls->module_count = linker.tls_count;
    tls->dynamic_vector = (thread_region_t){(uintptr_t)vector, size};
  }
  if (!tls->modules[index->module]) {
    size_t alignment = module->tls->p_align ? module->tls->p_align : 1;
    size_t length = module->tls->p_memsz;
    if (alignment > SIZE_MAX - sizeof(tls_allocation_t) - VM_PAGE_SIZE ||
        length > SIZE_MAX - alignment - sizeof(tls_allocation_t) - VM_PAGE_SIZE)
      fail(NULL, "TLS allocation overflow");
    size_t size =
        (sizeof(tls_allocation_t) + alignment + length + VM_PAGE_SIZE - 1) &
        ~(size_t)(VM_PAGE_SIZE - 1);
    tls_allocation_t *block = vm_map(NULL, size);
    if (!block)
      fail(NULL, "cannot allocate dynamic TLS");
    uintptr_t skew = module->tls->p_vaddr & (alignment - 1);
    uintptr_t base = (((uintptr_t)(block + 1) - skew + alignment - 1) &
                      ~(uintptr_t)(alignment - 1)) +
                     skew;
    memcpy((void *)base, (const void *)(module->bias + module->tls->p_vaddr),
           module->tls->p_filesz);
    *block = (tls_allocation_t){tls->dynamic_blocks, size};
    tls->dynamic_blocks = block;
    tls->modules[index->module] = (void *)base;
  }
  void *result = (char *)tls->modules[index->module] + index->offset;
  loader_lock(false);
  return result;
}

static void tls_release(void) {
  tls_control_t *tls = tls_current();
  while (tls->dynamic_blocks) {
    tls_allocation_t *block = tls->dynamic_blocks;
    tls->dynamic_blocks = block->next;
    vm_unmap(block, block->size);
  }
  if (tls->dynamic_vector.size) {
    vm_unmap((void *)tls->dynamic_vector.address, tls->dynamic_vector.size);
    tls->dynamic_vector = (thread_region_t){0};
  }
}

static int lookup_symbol(void *handle, const char *name, const void *caller,
                         void **address) {
  uint32_t sysv, gnu;
  symbol_hash(name, &sysv, &gnu);
  bool scoped = handle && handle != (void *)1 && handle != RTLD_NEXT;
  object_t *origin = object_containing(caller);
  object_t *root = handle == (void *)1 || !origin ? NULL : origin->scope_owner;
  if (scoped) {
    for (root = linker.first; root && root != handle; root = root->next) {
    }
    if (!root || !root->references)
      return -1;
    object_scope(root);
  }
  bool past_caller = handle != RTLD_NEXT;
  if (!past_caller && !origin)
    return -1;
  for (unsigned phase = 0; phase < 2; phase++) {
    if ((!phase && scoped) || (phase && !root))
      continue;
    size_t i = 0;
    object_t *object = phase ? NULL : linker.first;
    while (phase ? i < root->scope_count : object != NULL) {
      object_t *candidate = phase ? root->scope[i++] : object;
      if (!phase)
        object = object->next;
      if ((!phase && !candidate->global) ||
          (phase && !scoped && candidate->global))
        continue;
      if (!past_caller) {
        past_caller = candidate == origin;
        continue;
      }
      const Elf_Sym *symbol = object_lookup(candidate, name, sysv, gnu);
      if (!symbol)
        continue;
      definition_t definition = symbol_definition(candidate, symbol);
      *address = ELF32_ST_TYPE(symbol->st_info) == STT_TLS
                     ? tls_address(&(tls_index_t){candidate->tls_module,
                                                  definition.address})
                     : (void *)definition.address;
      return 0;
    }
  }
  return -1;
}

static int address_info(const void *pointer, Dl_info *information) {
  uintptr_t address = (uintptr_t)pointer;
  for (object_t *object = linker.first; object; object = object->next) {
    bool found = false;
    for (size_t i = 0; i < object->segment_count; i++) {
      const Elf_Phdr *segment = object->segments + i;
      uintptr_t start = object->bias + segment->p_vaddr;
      if (segment->p_type == PT_LOAD && address >= start &&
          address - start < segment->p_memsz)
        found = true;
    }
    if (!found)
      continue;
    *information =
        (Dl_info){.dli_fname = object->path, .dli_fbase = (void *)object->bias};
    for (size_t i = 0; i < object->symbol_count; i++) {
      const Elf_Sym *symbol = object->symbols + i;
      unsigned type = ELF32_ST_TYPE(symbol->st_info);
      if (symbol->st_shndx == SHN_UNDEF || symbol->st_shndx >= 0xff00 ||
          (type != STT_FUNC && type != STT_OBJECT))
        continue;
      uintptr_t value = object->bias + symbol->st_value;
      if (value <= address && value >= (uintptr_t)information->dli_saddr &&
          (!symbol->st_size || address - value < symbol->st_size)) {
        information->dli_saddr = (void *)value;
        information->dli_sname = object_string(object, symbol->st_name);
      }
    }
    return 1;
  }
  return 0;
}

static void *lookup_required(const char *name) {
  void *address = NULL;
  if (lookup_symbol(RTLD_DEFAULT, name, NULL, &address))
    fail(name, "required runtime symbol not found");
  return address;
}

static bool uses_runtime_entry(const object_t *object) {
  if (object->entry < object->bias)
    return false;
  uintptr_t entry = object->entry - object->bias;
  bool runtime_setup = false;
  for (size_t i = 0; i < object->symbol_count; i++) {
    const Elf_Sym *symbol = object->symbols + i;
    const char *name = object_string(object, symbol->st_name);
    if (symbol->st_shndx != SHN_UNDEF && symbol->st_value == entry &&
        !strcmp(name, "Main"))
      return true;
    if (symbol->st_shndx == SHN_UNDEF &&
        !strcmp(name, "runtime_thread_initialize"))
      runtime_setup = true;
  }
  return runtime_setup;
}

static void *load_runtime_object(const char *path, int flags,
                                 const void *caller) {
  if (!path)
    return (void *)1;
  object_t *parent = object_containing(caller);
  if (!parent)
    parent = linker.first;
  /* Snapshot the arena and list before publishing any new object. Failed ELF
   * validation/relocation rolls back mappings, metadata and module IDs. */
  linker_t saved = linker;
  jmp_buf recovery;
  linker.recovery = &recovery;
  linker.loading_fd = -1;
  linker.cwd = NULL;
  if (setjmp(recovery)) {
    const char *error = linker.error;
    if (linker.loading_fd >= 0)
      close(linker.loading_fd);
    for (object_t *o = saved.last->next; o; o = o->next)
      if (o->mapping.address)
        vm_unmap((void *)o->mapping.address, o->mapping.size);
    saved.last->next = NULL;
    arena_restore(&saved);
    linker = saved;
    tls_current()->loader_error = error;
    return NULL;
  }
  object_t *object = NULL;
  bool named = !strchr(path, '/') && !strchr(path, '\\') && !strchr(path, ':');
  char *absolute = named ? NULL : absolute_path(path);
  for (object_t *o = linker.first; o; o = o->next)
    if (named ? o->soname && !strcmp(o->soname, path)
              : !strcmp(o->path, absolute)) {
      object = o;
      break;
    }
  if (!object && !(flags & RTLD_NOLOAD))
    object = named ? object_dependency(parent, path)
                   : object_open_path(parent, path);
  if (!object)
    fail(path, "shared object not found");
  if (object->linked) {
    /* Path resolution for an already loaded object is temporary. Repeated
     * dlopen/NOLOAD calls must not grow the interpreter's metadata arena. */
    arena_restore(&saved);
  } else {
    object_resolve_dependencies(object);
    object_scope(object);
    for (object_t *o = saved.last->next; o; o = o->next)
      o->scope_owner = object;
    prepare_tls(saved.last->next, false);
    for (object_t *o = saved.last->next; o; o = o->next)
      object_relocate(o, false);
    for (object_t *o = saved.last->next; o; o = o->next) {
      object_relocate(o, true);
      object_protect(o);
      o->linked = true;
    }
    if (linker.count > linker.initialized_capacity) {
      if (linker.count > SIZE_MAX / sizeof(*linker.initialized))
        fail(NULL, "dependency graph size overflow");
      object_t **replacement = allocate(linker.count * sizeof(*replacement));
      memcpy(replacement, linker.initialized,
             linker.initialized_count * sizeof(*replacement));
      linker.initialized = replacement;
      linker.initialized_capacity = linker.count;
    }
  }
  object_scope(object);
  if (flags & RTLD_GLOBAL)
    for (size_t i = 0; i < object->scope_count; i++)
      object->scope[i]->global = true;
  object->references++;
  linker.recovery = saved.recovery;
  initialize_object_tree(object);
  return object;
}

static int close_runtime_object(void *handle) {
  if (handle == (void *)1)
    return 0;
  for (object_t *object = linker.first; object; object = object->next)
    if (object == handle && object->references) {
      object->references--;
      /* Keep mappings until process exit: TLS destructors and native callbacks
       * may still refer to code after the application closes its handle. */
      return 0;
    }
  return -1;
}

static void normalize_program_argument(runtime_arguments_t *arguments,
                                       const char *executable_path) {
  if (!arguments->argv || arguments->argc == 0 || !arguments->argv[0])
    return;
  char *program = arguments->argv[0];
  size_t length = strlen(program);
  if (length >= 3 && program[1] == ':' &&
      (program[2] == '/' || program[2] == '\\')) {
    memmove(program, program + 2, strlen(program + 2) + 1);
    return;
  }
  if (!strchr(program, '/') && !strchr(program, '\\') && executable_path) {
    const char *canonical = executable_path;
    if (canonical[1] == ':')
      canonical += 2;
    arguments->argv[0] = (char *)canonical;
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
  bool runtime_entry = uses_runtime_entry(linker.first);
  object_resolve_dependencies(linker.first);
  object_scope(linker.first);
  for (object_t *object = linker.first; object; object = object->next) {
    object->global = true;
    object->scope_owner = linker.first;
  }
  linker.tls_alignment = _Alignof(tls_control_t);
  prepare_tls(linker.first, true);
  linker.static_tls_count = linker.tls_count;
  for (object_t *object = linker.first; object; object = object->next)
    object_relocate(object, false);
  for (object_t *object = linker.first; object; object = object->next)
    object_relocate(object, true);
  for (object_t *object = linker.first; object; object = object->next)
    object_protect(object);
  for (object_t *object = linker.first; object; object = object->next)
    object->linked = true;
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
  linker.initialized_capacity = linker.count;
  const runtime_linker_t hooks = {
      .initialize = initialize,
      .finalize = finalize,
      .tls_allocate = tls_allocate,
      .symbol = lookup_symbol,
      .address_info = address_info,
      .executable_path = linker.first->path,
      .load = load_runtime_object,
      .close = close_runtime_object,
      .tls_address = tls_address,
      .tls_release = tls_release,
      .lock = loader_lock,
  };
  if (runtime_entry) {
    ((void (*)(const runtime_linker_t *))linker.first->entry)(&hooks);
    fail(NULL, "application entry returned");
  }

  typedef int (*thread_initialize_fn)(const runtime_linker_t *);
  typedef void (*runtime_initialize_fn)(const runtime_linker_t *);
  typedef int (*runtime_arguments_load_fn)(runtime_arguments_t *);
  typedef void (*runtime_arguments_destroy_fn)(runtime_arguments_t *);
  if (((thread_initialize_fn)lookup_required("runtime_thread_initialize"))(
          &hooks))
    fail(NULL, "runtime thread initialization failed");
  ((void (*)(uintptr_t))lookup_required("set_rt"))(
      (uintptr_t)lookup_required("return_to_app"));
  ((void (*)(void))lookup_required("abi_alloc_init"))();
  ((void (*)(void))lookup_required("stdio_initialize"))();
  if (((runtime_arguments_load_fn)lookup_required("runtime_arguments_load"))(
          &arguments))
    fail(NULL, "cannot read command line");
  normalize_program_argument(&arguments, linker.first->path);
  ((void (*)(void))lookup_required("init_float"))();
  ((runtime_initialize_fn)lookup_required("runtime_initialize"))(&hooks);
  int status = ((int (*)(int, char **))linker.first->entry)(arguments.argc,
                                                              arguments.argv);
  ((runtime_arguments_destroy_fn)lookup_required("runtime_arguments_destroy"))(
      &arguments);
  ((void (*)(int))lookup_required("exit"))(status);
  fail(NULL, "application entry returned");
}
