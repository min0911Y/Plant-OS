#include <ELF.h>
#include <dos.h>
int page_link(unsigned addr);
#define MAX(a, b) a > b ? a : b
bool elf32Validate(Elf32_Ehdr *hdr) {
  return hdr->e_ident[EI_MAG0] == ELFMAG0 && hdr->e_ident[EI_MAG1] == ELFMAG1 &&
         hdr->e_ident[EI_MAG2] == ELFMAG2 && hdr->e_ident[EI_MAG3] == ELFMAG3 &&
         hdr->e_ident[EI_CLASS] == ELFCLASS32 &&
         hdr->e_ident[EI_DATA] == ELFDATA2LSB && hdr->e_machine == EM_386;
}
bool elf32ValidateRelocatable(Elf32_Ehdr *hdr) {
  return elf32Validate(hdr) && hdr->e_type == ET_ELF;
}
uint32_t elf32_get_max_vaddr(Elf32_Ehdr *hdr) {
  Elf32_Phdr *phdr = (Elf32_Phdr *)((uint32_t)hdr + hdr->e_phoff);
  uint32_t max = 0;
  for (int i = 0; i < hdr->e_phnum; i++) {
    uint32_t size = MAX(
        phdr->p_filesz,
        phdr->p_memsz); // 如果memsz大于filesz 说明这是bss段，我们以最大的为准
    max = MAX(max, phdr->p_vaddr + size);
    phdr++;
  }
  return max;
}
unsigned div_round_up(unsigned num, unsigned size) {
  return (num + size - 1) / size;
}
void load_segment(Elf32_Phdr *phdr, void *elf) {
  unsigned int p = div_round_up(phdr->p_memsz, 0x1000);
  int d = phdr->p_paddr;
  if (d & 0x00000fff) {
    unsigned e = d + phdr->p_memsz;
    d = d & 0xfffff000;
    e &= 0xfffff000;
    p = (e - d) / 0x1000 + 1;
  }
  for (unsigned i = 0; i < p; i++) {
    page_link(d + i * 0x1000);
  }
  memcpy((void *)(uintptr_t)phdr->p_vaddr,
         (unsigned char *)elf + phdr->p_offset, phdr->p_filesz);
  if (phdr->p_memsz > phdr->p_filesz) { // 这个是bss段
    memset((void *)(uintptr_t)(phdr->p_vaddr + phdr->p_filesz), 0,
           phdr->p_memsz - phdr->p_filesz);
  }
}
uint32_t load_elf(Elf32_Ehdr *hdr) {
  Elf32_Phdr *phdr = (Elf32_Phdr *)((uint32_t)hdr + hdr->e_phoff);
  for (int i = 0; i < hdr->e_phnum; i++) {
    load_segment(phdr, (void *)hdr);
    phdr++;
  }
  return hdr->e_entry;
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
void elf32LoadData(Elf32_Ehdr *elfhdr, uint8_t *ptr) {
  uint8_t *p = (uint8_t *)elfhdr;
  for (int i = 0; i < elfhdr->e_shnum; i++) {
    Elf32_Shdr *shdr =
        (Elf32_Shdr *)(p + elfhdr->e_shoff + sizeof(Elf32_Shdr) * i);

    if (shdr->sh_type != SHT_PROGBITS || !(shdr->sh_flags & SHF_ALLOC)) {
      continue;
    }

    for (int i = 0; i < shdr->sh_size; i++) {
      ptr[shdr->sh_addr + i] = p[shdr->sh_offset + i];
    }
  }
}
