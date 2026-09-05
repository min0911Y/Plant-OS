#ifndef KERNEL_X64_ELF_H
#define KERNEL_X64_ELF_H
#include <ctypes.h>

enum {
  ET_ELF = 1,
  ET_EXEC = 2,
  PT_LOAD = 1,
  PT_DYNAMIC = 2,
  PT_INTERP = 3,
  PF_X = 1,
  PF_W = 2,
  SHF_WRITE = 1,
  SHF_ALLOC = 2,
  SHF_EXECINSTR = 4,
  SHT_SYMTAB = 2,
  SHT_STRTAB = 3,
  SHT_RELA = 4,
  SHT_NOBITS = 8,
  SHN_UNDEF = 0,
  SHN_ABS = 0xfff1
};
typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
  uint32_t p_type, p_flags;
  uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;
typedef struct {
  uint32_t sh_name, sh_type;
  uint64_t sh_flags, sh_addr, sh_offset, sh_size;
  uint32_t sh_link, sh_info;
  uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;
typedef struct {
  uint32_t st_name;
  uint8_t st_info, st_other;
  uint16_t st_shndx;
  uint64_t st_value, st_size;
} Elf64_Sym;
typedef struct {
  uint64_t r_offset, r_info;
  int64_t r_addend;
} Elf64_Rela;

bool elf64_validate_relocatable(const void *image, size_t size);
Elf64_Shdr *elf64_section(Elf64_Ehdr *header, int index);
const char *elf64_section_name(Elf64_Ehdr *header, int index);
Elf64_Sym *elf64_symtab(Elf64_Ehdr *header, Elf64_Shdr **section);
const char *elf64_string_table(Elf64_Ehdr *header, Elf64_Shdr *section);
#endif
