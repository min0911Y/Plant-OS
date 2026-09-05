#include <arch/x86/x86_64/cpu.h>
#include <arch/x86/x86_64/elf.h>
#include <dos.h>
#include <executable.h>
#include <user_space.h>

static bool header_valid(const Elf64_Ehdr *header, size_t size, unsigned type) {
  return size >= sizeof(*header) &&
         !memcmp(header->e_ident, "\177ELF\2\1\1", 7) &&
         header->e_type == type && header->e_machine == 62 &&
         header->e_version == 1 && header->e_ehsize == sizeof(*header);
}
bool arch_executable_validate(const void *image, size_t size, uintptr_t *entry,
                              uintptr_t *end) {
  const Elf64_Ehdr *header = image;
  if (!image || !entry || !end || !header_valid(header, size, ET_EXEC) ||
      !header->e_phnum || header->e_phentsize != sizeof(Elf64_Phdr) ||
      header->e_phoff > size ||
      header->e_phnum > (size - header->e_phoff) / sizeof(Elf64_Phdr))
    return false;
  const Elf64_Phdr *segments =
      (const void *)((const uint8_t *)image + header->e_phoff);
  bool entry_found = false;
  *end = USER_SPACE_START;
  for (size_t i = 0; i < header->e_phnum; i++) {
    const Elf64_Phdr *p = &segments[i];
    if (p->p_type == PT_INTERP || p->p_type == PT_DYNAMIC)
      return false;
    if (p->p_type != PT_LOAD || !p->p_memsz)
      continue;
    if (p->p_filesz > p->p_memsz || p->p_offset > size ||
        p->p_filesz > size - p->p_offset || p->p_vaddr < USER_SPACE_START ||
        p->p_vaddr >= USER_HEAP_END ||
        p->p_memsz > USER_HEAP_END - 4096 - p->p_vaddr ||
        (p->p_flags & (PF_W | PF_X)) == (PF_W | PF_X))
      return false;
    if (p->p_align > 1 && ((p->p_align & (p->p_align - 1)) ||
                           ((p->p_vaddr - p->p_offset) & (p->p_align - 1))))
      return false;
    uintptr_t start = p->p_vaddr & ~4095ull;
    uintptr_t limit = (p->p_vaddr + p->p_memsz + 4095) & ~4095ull;
    for (size_t j = 0; j < i; j++) {
      const Elf64_Phdr *previous = &segments[j];
      if (previous->p_type == PT_LOAD && previous->p_memsz &&
          start < ((previous->p_vaddr + previous->p_memsz + 4095) & ~4095ull) &&
          (previous->p_vaddr & ~4095ull) < limit)
        return false;
    }
    if (limit > *end)
      *end = limit;
    if ((p->p_flags & PF_X) && header->e_entry >= p->p_vaddr &&
        header->e_entry < p->p_vaddr + p->p_memsz)
      entry_found = true;
  }
  *entry = header->e_entry;
  return entry_found;
}
bool arch_executable_load(const void *image, size_t size, uintptr_t *entry) {
  uintptr_t end;
  if (!arch_executable_validate(image, size, entry, &end))
    return false;
  const Elf64_Ehdr *header = image;
  const Elf64_Phdr *segments =
      (const void *)((const uint8_t *)image + header->e_phoff);
  for (size_t i = 0; i < header->e_phnum; i++) {
    const Elf64_Phdr *p = &segments[i];
    if (p->p_type != PT_LOAD || !p->p_memsz)
      continue;
    uintptr_t start = p->p_vaddr & ~4095ull;
    uintptr_t limit = (p->p_vaddr + p->p_memsz + 4095) & ~4095ull;
    for (uintptr_t address = start; address < limit; address += 4096) {
      if (!page_link(address))
        return false;
    }
    memcpy((void *)p->p_vaddr, (const uint8_t *)image + p->p_offset,
           p->p_filesz);
    if (!x64_user_protect(start, limit - start, p->p_flags & PF_W,
                          p->p_flags & PF_X))
      return false;
  }
  return true;
}

Elf64_Shdr *elf64_section(Elf64_Ehdr *header, int index) {
  if (index < 0 || index >= header->e_shnum)
    return NULL;
  return (void *)((uint8_t *)header + header->e_shoff +
                  (size_t)index * sizeof(Elf64_Shdr));
}
const char *elf64_string_table(Elf64_Ehdr *header, Elf64_Shdr *section) {
  return section && section->sh_type == SHT_STRTAB
             ? (char *)header + section->sh_offset
             : NULL;
}
const char *elf64_section_name(Elf64_Ehdr *header, int index) {
  Elf64_Shdr *names = elf64_section(header, header->e_shstrndx);
  Elf64_Shdr *section = elf64_section(header, index);
  const char *strings = elf64_string_table(header, names);
  return strings && section && section->sh_name < names->sh_size
             ? strings + section->sh_name
             : NULL;
}
Elf64_Sym *elf64_symtab(Elf64_Ehdr *header, Elf64_Shdr **section) {
  for (size_t i = 0; i < header->e_shnum; i++) {
    Elf64_Shdr *candidate = elf64_section(header, i);
    if (candidate->sh_type == SHT_SYMTAB) {
      *section = candidate;
      return (void *)((uint8_t *)header + candidate->sh_offset);
    }
  }
  *section = NULL;
  return NULL;
}
bool elf64_validate_relocatable(const void *image, size_t size) {
  Elf64_Ehdr *header = (void *)image;
  if (!image || !header_valid(header, size, ET_ELF) || !header->e_shnum ||
      header->e_shoff > size || header->e_shentsize != sizeof(Elf64_Shdr) ||
      header->e_shnum > (size - header->e_shoff) / sizeof(Elf64_Shdr) ||
      header->e_shstrndx >= header->e_shnum)
    return false;
  for (size_t i = 0; i < header->e_shnum; i++) {
    Elf64_Shdr *s = elf64_section(header, i);
    if (s->sh_size > 0x7fffffff ||
        (s->sh_type != SHT_NOBITS &&
         (s->sh_offset > size || s->sh_size > size - s->sh_offset)))
      return false;
    if (s->sh_type == SHT_STRTAB && s->sh_size &&
        *((const char *)image + s->sh_offset + s->sh_size - 1))
      return false;
    if (s->sh_type == SHT_SYMTAB &&
        (s->sh_entsize != sizeof(Elf64_Sym) || s->sh_link >= header->e_shnum))
      return false;
    if (s->sh_type == SHT_RELA &&
        (s->sh_entsize != sizeof(Elf64_Rela) || s->sh_info >= header->e_shnum))
      return false;
  }
  return true;
}
