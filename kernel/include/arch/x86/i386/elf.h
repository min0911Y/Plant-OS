#ifndef KERNEL_ELF32_HELPERS_H
#define KERNEL_ELF32_HELPERS_H
#include <elf.h>

bool elf32_validate_relocatable(const void* image, size_t image_size);
Elf32_Shdr* elf32_section(Elf32_Ehdr* hdr, int index);
const char* elf32_section_name(Elf32_Ehdr* hdr, int index);
Elf32_Shdr* elf32_find_section(Elf32_Ehdr* hdr, const char* name);
Elf32_Sym* elf32_symtab(Elf32_Ehdr* hdr, Elf32_Shdr** symtab_shdr);
const char* elf32_string_table(Elf32_Ehdr* hdr, Elf32_Shdr* strtab_shdr);
#endif
