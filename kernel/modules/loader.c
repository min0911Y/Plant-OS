#include <dos.h>
#include <limits.h>
#include <module.h>
#include <native_elf.h>

typedef struct module_section {
  char name[32];
  void *addr;
  uint32_t size;
  uint16_t shndx;
  uint32_t flags;
} module_section_t;

typedef struct module_loaded {
  uint32_t used;
  uint32_t id;
  char name[MODULE_NAME_MAX + 1];
  char path[MODULE_PATH_MAX + 1];
  module_info_t *info;
  void *image;
  uint32_t image_size;
  module_section_t *sections;
  uint32_t section_count;
  module_exported_symbol_t *exports;
  uint32_t export_count;
} module_loaded_t;

static module_loaded_t modules[MAX_MODULES];
static module_exported_symbol_t kernel_symbols[MAX_MODULE_EXPORTS];
static uint32_t kernel_symbol_count;
static uint32_t next_module_id = 1;

static module_loaded_t *module_find_by_name(const char *name) {
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].used && strcmp(modules[i].name, name) == 0) {
      return &modules[i];
    }
  }
  return NULL;
}

static module_loaded_t *module_alloc_slot(void) {
  for (int i = 0; i < MAX_MODULES; i++) {
    if (!modules[i].used) {
      memset(&modules[i], 0, sizeof(modules[i]));
      modules[i].used = 1;
      modules[i].id = next_module_id++;
      return &modules[i];
    }
  }
  return NULL;
}

static void module_free_slot(module_loaded_t *module) {
  if (!module) {
    return;
  }
  memset(module, 0, sizeof(*module));
}

static uintptr_t module_find_export_in_module(const module_loaded_t *module,
                                              const char *name) {
  if (!module || !module->exports) {
    return 0;
  }
  for (uint32_t i = 0; i < module->export_count; i++) {
    if (strcmp(module->exports[i].name, name) == 0) {
      return module->exports[i].addr;
    }
  }
  return 0;
}

uintptr_t module_resolve_symbol(const char *name) {
  if (!name || !name[0]) {
    return 0;
  }
  for (uint32_t i = 0; i < kernel_symbol_count; i++) {
    if (strcmp(kernel_symbols[i].name, name) == 0) {
      return kernel_symbols[i].addr;
    }
  }
  for (int i = 0; i < MAX_MODULES; i++) {
    uintptr_t addr = module_find_export_in_module(&modules[i], name);
    if (addr) {
      return addr;
    }
  }
  return 0;
}

bool module_register_kernel_symbol(const char *name, uintptr_t addr) {
  if (!name || !name[0] || !addr) {
    return false;
  }
  for (uint32_t i = 0; i < kernel_symbol_count; i++) {
    if (strcmp(kernel_symbols[i].name, name) == 0) {
      kernel_symbols[i].addr = addr;
      return true;
    }
  }
  if (kernel_symbol_count >= MAX_MODULE_EXPORTS) {
    return false;
  }
  kernel_symbols[kernel_symbol_count].name = name;
  kernel_symbols[kernel_symbol_count].addr = addr;
  kernel_symbol_count++;
  return true;
}

static bool module_register_exports(module_loaded_t *module) {
  if (!module || !module->info) {
    return false;
  }
  if (module->info->export_count == 0) {
    module->exports = NULL;
    module->export_count = 0;
    return true;
  }
  if (!module->info->exports ||
      module->info->export_count >
          (size_t)INT_MAX / sizeof(module_exported_symbol_t))
    return false;
  module->exports =
      malloc(sizeof(module_exported_symbol_t) * module->info->export_count);
  if (!module->exports) {
    return false;
  }
  memset(module->exports, 0,
         sizeof(module_exported_symbol_t) * module->info->export_count);
  module->export_count = 0;
  for (uint32_t i = 0; i < module->info->export_count; i++) {
    const char *name = module->info->exports[i].name;
    uintptr_t addr = module->info->exports[i].addr;
    if (!name || !name[0] || !addr) {
      module->export_count = 0;
      return false;
    }
    if (module_resolve_symbol(name)) {
      printk("module: duplicate export %s\n", name);
      module->export_count = 0;
      return false;
    }
    module->exports[i].name = name;
    module->exports[i].addr = addr;
    module->export_count++;
  }
  return true;
}

static module_section_t *module_find_section(module_loaded_t *module,
                                             uint16_t shndx) {
  if (!module || !module->sections) {
    return NULL;
  }
  for (uint32_t i = 0; i < module->section_count; i++) {
    if (module->sections[i].shndx == shndx) {
      return &module->sections[i];
    }
  }
  return NULL;
}

static uintptr_t module_symbol_value(module_loaded_t *module, Elf_Ehdr *hdr,
                                     Elf_Sym *sym, const char *strtab) {
  (void)strtab;
  if (sym->st_shndx == SHN_UNDEF) {
    return module_resolve_symbol((const char *)strtab + sym->st_name);
  }
  if (sym->st_shndx == SHN_ABS) {
    return sym->st_value;
  }
  module_section_t *section = module_find_section(module, sym->st_shndx);
  if (!section) {
    return 0;
  }
  return (uintptr_t)section->addr + sym->st_value;
}

static bool module_apply_relocations(module_loaded_t *module, Elf_Ehdr *hdr) {
  Elf_Shdr *symtab_shdr;
  Elf_Sym *symtab = elf_symtab(hdr, &symtab_shdr);
  if (!symtab || !symtab_shdr) {
    printk("module: no symtab in %s\n", module->path);
    return false;
  }
  Elf_Shdr *strtab_shdr = elf_section(hdr, symtab_shdr->sh_link);
  const char *strtab = elf_string_table(hdr, strtab_shdr);
  if (!strtab) {
    printk("module: no strtab in %s\n", module->path);
    return false;
  }

  for (int i = 0; i < hdr->e_shnum; i++) {
    Elf_Shdr *relsec = elf_section(hdr, i);
    if (!relsec || relsec->sh_type != ELF_RELOCATION_SECTION) {
      continue;
    }
    module_section_t *target = module_find_section(module, relsec->sh_info);
    if (!target) {
      printk("module: relocation target missing for section %d\n",
             relsec->sh_info);
      return false;
    }

    Elf_Rel *rels = (Elf_Rel *)((uint8_t *)hdr + relsec->sh_offset);
    uint32_t rel_count = relsec->sh_size / sizeof(Elf_Rel);
    for (uint32_t j = 0; j < rel_count; j++) {
      Elf_Rel *rel = &rels[j];
      uint32_t sym_idx = ELF_R_SYM(rel->r_info);
      uint32_t type = ELF_R_TYPE(rel->r_info);
      if (type == 0)
        continue;
      if (sym_idx >= symtab_shdr->sh_size / sizeof(Elf_Sym))
        return false;
      Elf_Sym *sym = &symtab[sym_idx];
      size_t width = ELF_RELOCATION_WIDTH(type);
      if (sym->st_name >= strtab_shdr->sh_size ||
          rel->r_offset > target->size || width > target->size - rel->r_offset)
        return false;
      uintptr_t S = module_symbol_value(module, hdr, sym, strtab);
      uintptr_t P = (uintptr_t)target->addr + rel->r_offset;
#if defined(KERNEL_ARCH_X86_64)
      int64_t A = rel->r_addend;
#else
      uint32_t A = *(uint32_t *)P;
#endif

      if (!S && sym->st_shndx == SHN_UNDEF && (sym->st_info >> 4) != 2) {
        const char *name = strtab + sym->st_name;
        printk("module: unresolved symbol %s\n", name);
        return false;
      }

#if defined(KERNEL_ARCH_X86_64)
      uint64_t value = S + A;
      switch (type) {
      case 0:
        break;
      case 1:
        *(uint64_t *)P = value;
        break;
      case 2:
      case 4:
        if ((int64_t)(value - P) < INT_MIN || (int64_t)(value - P) > INT_MAX)
          return false;
        *(uint32_t *)P = value - P;
        break;
      case 10:
        if (value > UINT_MAX)
          return false;
        *(uint32_t *)P = value;
        break;
      case 11:
        if ((int64_t)value != (int32_t)value)
          return false;
        *(int32_t *)P = value;
        break;
      default:
        return false;
      }
#else
      switch (type) {
      case R_386_NONE:
        break;
      case R_386_32:
        *(uint32_t *)P = S + A;
        break;
      case R_386_PC32:
        *(uint32_t *)P = S + A - P;
        break;
      default:
        return false;
      }
#endif
    }
  }
  return true;
}

static bool module_copy_sections(module_loaded_t *module, Elf_Ehdr *hdr) {
  uint32_t alloc_count = 0;
  for (int i = 0; i < hdr->e_shnum; i++) {
    Elf_Shdr *section = elf_section(hdr, i);
    if (section && (section->sh_flags & SHF_ALLOC) && section->sh_size) {
      alloc_count++;
    }
  }

  module->sections = malloc(sizeof(module_section_t) * alloc_count);
  if (!module->sections && alloc_count) {
    return false;
  }
  module->section_count = 0;

  for (int i = 0; i < hdr->e_shnum; i++) {
    Elf_Shdr *section = elf_section(hdr, i);
    if (!section || !(section->sh_flags & SHF_ALLOC) || !section->sh_size) {
      continue;
    }

    void *buffer = arch_module_allocate(section->sh_size);
    if (!buffer) {
      printk("module: no memory for section %d\n", i);
      return false;
    }
    memset(buffer, 0, section->sh_size);
    if (section->sh_type != SHT_NOBITS) {
      memcpy(buffer, (uint8_t *)hdr + section->sh_offset, section->sh_size);
    }

    module_section_t *dst = &module->sections[module->section_count++];
    memset(dst, 0, sizeof(*dst));
    const char *section_name = elf_section_name(hdr, i);
    if (section_name) {
      strncpy(dst->name, section_name, sizeof(dst->name) - 1);
    }
    dst->addr = buffer;
    dst->size = section->sh_size;
    dst->shndx = (uint16_t)i;
    dst->flags = section->sh_flags;
  }
  return true;
}

static module_info_t *module_find_info(module_loaded_t *module) {
  module_section_t *info_section = NULL;
  for (uint32_t i = 0; i < module->section_count; i++) {
    if (strcmp(module->sections[i].name, ".modinfo") == 0) {
      info_section = &module->sections[i];
      break;
    }
  }
  if (!info_section || info_section->size < sizeof(module_info_t)) {
    return NULL;
  }
  return (module_info_t *)info_section->addr;
}

static void module_release_memory(module_loaded_t *module) {
  if (!module) {
    return;
  }
  if (module->sections) {
    for (uint32_t i = 0; i < module->section_count; i++) {
      if (module->sections[i].addr) {
        arch_module_free(module->sections[i].addr, module->sections[i].size);
      }
    }
    free(module->sections);
    module->sections = NULL;
  }
  if (module->exports) {
    free(module->exports);
    module->exports = NULL;
  }
  if (module->image) {
    page_free(module->image, module->image_size);
    module->image = NULL;
  }
}

static bool module_load_image(module_loaded_t *module, const char *path) {
  vfs_stat_t status;
  int size = vfs_stat(current_task()->fs_context, path, &status) < 0
                 ? -1
                 : status.size;
  if (size <= 0) {
    printk("module: %s not found\n", path);
    return false;
  }

  module->image = page_malloc(size);
  if (!module->image) {
    printk("module: no memory for %s\n", path);
    return false;
  }
  module->image_size = size;
  FILE *stream = fopen(path, "rb");
  if (stream == NULL || fread(module->image, 1, size, stream) != (size_t)size) {
    if (stream != NULL) {
      fclose(stream);
    }
    printk("module: failed to read %s\n", path);
    return false;
  }
  fclose(stream);

  Elf_Ehdr *hdr = (Elf_Ehdr *)module->image;
  if (!elf_validate_relocatable(hdr, module->image_size)) {
    printk("module: %s is not relocatable ELF\n", path);
    return false;
  }
  if (!module_copy_sections(module, hdr)) {
    return false;
  }
  if (!module_apply_relocations(module, hdr)) {
    return false;
  }
  for (uint32_t i = 0; i < module->section_count; i++) {
    module_section_t *section = &module->sections[i];
    if (!arch_module_protect(section->addr, section->size,
                             section->flags & SHF_WRITE,
                             section->flags & SHF_EXECINSTR))
      return false;
  }
  module->info = module_find_info(module);
  if (!module->info || module->info->magic != MODULE_INFO_MAGIC ||
      module->info->api_version != MODULE_API_VERSION || !module->info->name ||
      !module->info->init || !module->info->exit) {
    printk("module: invalid metadata in %s\n", path);
    return false;
  }
  return true;
}

static void module_register_builtin_symbols(void) {
  module_register_kernel_symbol("printk", (uintptr_t)printk);
  module_register_kernel_symbol("malloc", (uintptr_t)malloc);
  module_register_kernel_symbol("free", (uintptr_t)free);
  module_register_kernel_symbol("page_malloc", (uintptr_t)page_malloc);
  module_register_kernel_symbol("page_free", (uintptr_t)page_free);
  module_register_kernel_symbol("memcpy", (uintptr_t)memcpy);
  module_register_kernel_symbol("memset", (uintptr_t)memset);
  module_register_kernel_symbol("strcmp", (uintptr_t)strcmp);
  module_register_kernel_symbol("strcpy", (uintptr_t)strcpy);
  module_register_kernel_symbol("strncpy", (uintptr_t)strncpy);
  module_register_kernel_symbol("strlen", (uintptr_t)strlen);
  module_register_kernel_symbol("snprintf", (uintptr_t)snprintf);
  module_register_kernel_symbol("sprintf", (uintptr_t)sprintf);
  module_register_kernel_symbol("logk", (uintptr_t)logk);
  module_register_kernel_symbol("__asan_load1_noabort",
                                (uintptr_t)__asan_load1_noabort);
  module_register_kernel_symbol("__asan_load2_noabort",
                                (uintptr_t)__asan_load2_noabort);
  module_register_kernel_symbol("__asan_load4_noabort",
                                (uintptr_t)__asan_load4_noabort);
  module_register_kernel_symbol("__asan_load8_noabort",
                                (uintptr_t)__asan_load8_noabort);
  module_register_kernel_symbol("__asan_load16_noabort",
                                (uintptr_t)__asan_load16_noabort);
  module_register_kernel_symbol("__asan_loadN_noabort",
                                (uintptr_t)__asan_loadN_noabort);
  module_register_kernel_symbol("__asan_store1_noabort",
                                (uintptr_t)__asan_store1_noabort);
  module_register_kernel_symbol("__asan_store2_noabort",
                                (uintptr_t)__asan_store2_noabort);
  module_register_kernel_symbol("__asan_store4_noabort",
                                (uintptr_t)__asan_store4_noabort);
  module_register_kernel_symbol("__asan_store8_noabort",
                                (uintptr_t)__asan_store8_noabort);
  module_register_kernel_symbol("__asan_store16_noabort",
                                (uintptr_t)__asan_store16_noabort);
  module_register_kernel_symbol("__asan_storeN_noabort",
                                (uintptr_t)__asan_storeN_noabort);
  module_register_kernel_symbol("__asan_handle_no_return",
                                (uintptr_t)__asan_handle_no_return);
}

void module_init_system(void) {
  memset(modules, 0, sizeof(modules));
  kernel_symbol_count = 0;
  next_module_id = 1;
  module_register_builtin_symbols();
}

int module_load(const char *path) {
  module_loaded_t *module;
  int status;

  if (!path || !path[0]) {
    return -1;
  }

  module = module_alloc_slot();
  if (!module) {
    printk("module: no free slot\n");
    return -1;
  }

  strncpy(module->path, path, MODULE_PATH_MAX);
  if (!module_load_image(module, path)) {
    module_release_memory(module);
    module_free_slot(module);
    return -1;
  }

  strncpy(module->name, module->info->name, MODULE_NAME_MAX);
  if (module_find_by_name(module->name) != module) {
    printk("module: name %s already loaded\n", module->name);
    module_release_memory(module);
    module_free_slot(module);
    return -1;
  }

  if (!module_register_exports(module)) {
    module_release_memory(module);
    module_free_slot(module);
    return -1;
  }

  status = module->info->init();
  if (status != 0) {
    printk("module: init of %s failed with %d\n", module->name, status);
    module_release_memory(module);
    module_free_slot(module);
    return -1;
  }

  printk("module: loaded %s from %s\n", module->name, module->path);
  return 0;
}

int module_unload(const char *name) {
  module_loaded_t *module = module_find_by_name(name);
  if (!module) {
    printk("module: %s not loaded\n", name);
    return -1;
  }
  if (module->info->exit() != 0) {
    printk("module: exit of %s failed\n", module->name);
    return -1;
  }
  printk("module: unloaded %s\n", module->name);
  module_release_memory(module);
  module_free_slot(module);
  return 0;
}

int module_list(module_handle_t *out, int max_count) {
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (!modules[i].used) {
      continue;
    }
    if (out && count < max_count) {
      memset(&out[count], 0, sizeof(out[count]));
      out[count].id = modules[i].id;
      out[count].image_size = modules[i].image_size;
      out[count].section_count = modules[i].section_count;
      out[count].export_count = modules[i].export_count;
      strncpy(out[count].name, modules[i].name, MODULE_NAME_MAX);
      strncpy(out[count].path, modules[i].path, MODULE_PATH_MAX);
    }
    count++;
  }
  return count;
}
