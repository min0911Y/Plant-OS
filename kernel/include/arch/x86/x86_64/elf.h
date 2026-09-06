#ifndef KERNEL_ELF64_HELPERS_H
#define KERNEL_ELF64_HELPERS_H
#include <elf.h>

bool elf64_validate_relocatable(const void *image, size_t size);
Elf64_Shdr *elf64_section(Elf64_Ehdr *header, int index);
const char *elf64_section_name(Elf64_Ehdr *header, int index);
Elf64_Sym *elf64_symtab(Elf64_Ehdr *header, Elf64_Shdr **section);
const char *elf64_string_table(Elf64_Ehdr *header, Elf64_Shdr *section);
#endif
