#include <arch/x86/i386/elf.h>
#include <dos.h>
#include <executable.h>
#include <user_space.h>

#define ELF32_PAGE_SIZE 0x1000u

static bool elf32_header_valid(const Elf32_Ehdr *header, size_t image_size,
                               uint16_t expected_type) {
  if (header == NULL || image_size < sizeof(Elf32_Ehdr)) {
    return false;
  }
  return header->e_ident[EI_MAG0] == ELFMAG0 &&
         header->e_ident[EI_MAG1] == ELFMAG1 &&
         header->e_ident[EI_MAG2] == ELFMAG2 &&
         header->e_ident[EI_MAG3] == ELFMAG3 &&
         header->e_ident[EI_CLASS] == ELFCLASS32 &&
         header->e_ident[EI_DATA] == ELFDATA2LSB &&
         header->e_ident[EI_VERSION] == EV_CURRENT &&
         header->e_machine == EM_386 && header->e_type == expected_type &&
         header->e_ehsize == sizeof(Elf32_Ehdr);
}

static bool elf32_program_headers(const void *image, size_t image_size,
                                  const Elf32_Phdr **headers_out,
                                  uint16_t *count_out) {
  const Elf32_Ehdr *header = image;
  if (!elf32_header_valid(header, image_size, ET_EXEC) ||
      header->e_phnum == 0 || header->e_phentsize != sizeof(Elf32_Phdr) ||
      header->e_phoff > image_size ||
      header->e_phnum >
          (image_size - header->e_phoff) / sizeof(Elf32_Phdr)) {
    return false;
  }
  *headers_out =
      (const Elf32_Phdr *)((const uint8_t *)image + header->e_phoff);
  *count_out = header->e_phnum;
  return true;
}

static bool elf32_load_segment_valid(const Elf32_Phdr *segment,
                                     size_t image_size,
                                     uint32_t *end_out) {
  if (segment->p_filesz > segment->p_memsz ||
      segment->p_offset > image_size ||
      segment->p_filesz > image_size - segment->p_offset ||
      segment->p_vaddr < USER_SPACE_START ||
      segment->p_vaddr >= USER_HEAP_END ||
      segment->p_memsz > USER_HEAP_END - segment->p_vaddr) {
    return false;
  }
  if (segment->p_align > 1 &&
      ((segment->p_align & (segment->p_align - 1)) != 0 ||
       ((segment->p_vaddr - segment->p_offset) & (segment->p_align - 1)) !=
           0)) {
    return false;
  }
  *end_out = segment->p_vaddr + segment->p_memsz;
  return true;
}

bool arch_executable_validate(const void *image, size_t image_size,
                              uintptr_t *entry_out,
                              uintptr_t *image_end_out) {
  if (entry_out == NULL || image_end_out == NULL) {
    return false;
  }
  const Elf32_Phdr *segments;
  uint16_t segment_count;
  if (!elf32_program_headers(image, image_size, &segments, &segment_count)) {
    return false;
  }
  const Elf32_Ehdr *header = image;
  uint32_t image_end = USER_SPACE_START;
  bool has_load_segment = false;
  bool entry_valid = false;
  for (uint16_t i = 0; i < segment_count; i++) {
    if (segments[i].p_type != PT_LOAD) {
      continue;
    }
    uint32_t segment_end;
    if (!elf32_load_segment_valid(&segments[i], image_size, &segment_end)) {
      return false;
    }
    for (uint16_t j = 0; j < i; j++) {
      if (segments[j].p_type != PT_LOAD || segments[j].p_memsz == 0 ||
          segments[i].p_memsz == 0) {
        continue;
      }
      uint32_t previous_end = segments[j].p_vaddr + segments[j].p_memsz;
      if (segments[i].p_vaddr < previous_end &&
          segments[j].p_vaddr < segment_end) {
        return false;
      }
    }
    has_load_segment = true;
    if (segment_end > image_end) {
      image_end = segment_end;
    }
    if ((segments[i].p_flags & PF_X) != 0 &&
        header->e_entry >= segments[i].p_vaddr &&
        header->e_entry < segment_end) {
      entry_valid = true;
    }
  }
  if (!has_load_segment || !entry_valid || image_end >= USER_HEAP_END) {
    return false;
  }
  *entry_out = header->e_entry;
  *image_end_out = image_end;
  return true;
}

static bool elf32_page_previously_mapped(const Elf32_Phdr *segments,
                                         uint16_t current,
                                         uint32_t page) {
  for (uint16_t i = 0; i < current; i++) {
    if (segments[i].p_type != PT_LOAD || segments[i].p_memsz == 0) {
      continue;
    }
    uint32_t start = segments[i].p_vaddr & ~(ELF32_PAGE_SIZE - 1);
    uint32_t end = (segments[i].p_vaddr + segments[i].p_memsz +
                    ELF32_PAGE_SIZE - 1) &
                   ~(ELF32_PAGE_SIZE - 1);
    if (page >= start && page < end) {
      return true;
    }
  }
  return false;
}

bool arch_executable_load(const void *image, size_t image_size,
                          uintptr_t *entry_out) {
  uintptr_t image_end;
  if (!arch_executable_validate(image, image_size, entry_out, &image_end)) {
    return false;
  }
  (void)image_end;
  const Elf32_Phdr *segments;
  uint16_t segment_count;
  if (!elf32_program_headers(image, image_size, &segments, &segment_count)) {
    return false;
  }

  for (uint16_t i = 0; i < segment_count; i++) {
    if (segments[i].p_type != PT_LOAD || segments[i].p_memsz == 0) {
      continue;
    }
    uint32_t start = segments[i].p_vaddr & ~(ELF32_PAGE_SIZE - 1);
    uint32_t end = (segments[i].p_vaddr + segments[i].p_memsz +
                    ELF32_PAGE_SIZE - 1) &
                   ~(ELF32_PAGE_SIZE - 1);
    for (uint32_t page = start; page < end; page += ELF32_PAGE_SIZE) {
      if (!elf32_page_previously_mapped(segments, i, page) &&
          !page_link(page)) {
        return false;
      }
    }
  }

  for (uint16_t i = 0; i < segment_count; i++) {
    if (segments[i].p_type != PT_LOAD || segments[i].p_memsz == 0) {
      continue;
    }
    memcpy((void *)(uintptr_t)segments[i].p_vaddr,
           (const uint8_t *)image + segments[i].p_offset,
           segments[i].p_filesz);
    if (segments[i].p_memsz > segments[i].p_filesz) {
      memset((void *)(uintptr_t)(segments[i].p_vaddr + segments[i].p_filesz),
             0, segments[i].p_memsz - segments[i].p_filesz);
    }
  }
  return true;
}

bool elf32_validate_relocatable(const void *image, size_t image_size) {
  const Elf32_Ehdr *header = image;
  if (!elf32_header_valid(header, image_size, ET_ELF) ||
      header->e_shentsize != sizeof(Elf32_Shdr) ||
      header->e_shoff > image_size ||
      header->e_shnum >
          (image_size - header->e_shoff) / sizeof(Elf32_Shdr)) {
    return false;
  }
  return true;
}
Elf32_Shdr *elf32_section(Elf32_Ehdr *hdr, int index) {
  if (index < 0 || index >= hdr->e_shnum) {
    return NULL;
  }
  return (Elf32_Shdr *)((uint8_t *)hdr + hdr->e_shoff + hdr->e_shentsize * index);
}
const char *elf32_section_name(Elf32_Ehdr *hdr, int index) {
  Elf32_Shdr *shstr = elf32_section(hdr, hdr->e_shstrndx);
  Elf32_Shdr *section = elf32_section(hdr, index);
  if (!shstr || !section) {
    return NULL;
  }
  return (const char *)hdr + shstr->sh_offset + section->sh_name;
}
Elf32_Shdr *elf32_find_section(Elf32_Ehdr *hdr, const char *name) {
  for (int i = 0; i < hdr->e_shnum; i++) {
    const char *section_name = elf32_section_name(hdr, i);
    if (section_name && strcmp(section_name, name) == 0) {
      return elf32_section(hdr, i);
    }
  }
  return NULL;
}
Elf32_Sym *elf32_symtab(Elf32_Ehdr *hdr, Elf32_Shdr **symtab_shdr) {
  for (int i = 0; i < hdr->e_shnum; i++) {
    Elf32_Shdr *section = elf32_section(hdr, i);
    if (section && section->sh_type == SHT_SYMTAB) {
      if (symtab_shdr) {
        *symtab_shdr = section;
      }
      return (Elf32_Sym *)((uint8_t *)hdr + section->sh_offset);
    }
  }
  if (symtab_shdr) {
    *symtab_shdr = NULL;
  }
  return NULL;
}
const char *elf32_string_table(Elf32_Ehdr *hdr, Elf32_Shdr *strtab_shdr) {
  if (!strtab_shdr) {
    return NULL;
  }
  return (const char *)hdr + strtab_shdr->sh_offset;
}
