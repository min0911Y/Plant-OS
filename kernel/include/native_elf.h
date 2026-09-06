#ifndef KERNEL_NATIVE_ELF_H
#define KERNEL_NATIVE_ELF_H

#if defined(KERNEL_ARCH_I386)
#include <arch/x86/i386/elf.h>
typedef Elf32_Shdr Elf_Shdr;
typedef Elf32_Rel Elf_Rel;
#define elf_validate_relocatable elf32_validate_relocatable
#define elf_section elf32_section
#define elf_section_name elf32_section_name
#define elf_symtab elf32_symtab
#define elf_string_table elf32_string_table
#define ELF_RELOCATION_SECTION SHT_REL
#define ELF_R_SYM ELF32_R_SYM
#define ELF_R_TYPE ELF32_R_TYPE
#define ELF_RELOCATION_WIDTH(type) 4u
#elif defined(KERNEL_ARCH_X86_64)
#include <arch/x86/x86_64/elf.h>
typedef Elf64_Shdr Elf_Shdr;
typedef Elf64_Rela Elf_Rel;
#define elf_validate_relocatable elf64_validate_relocatable
#define elf_section elf64_section
#define elf_section_name elf64_section_name
#define elf_symtab elf64_symtab
#define elf_string_table elf64_string_table
#define ELF_RELOCATION_SECTION SHT_RELA
#define ELF_R_SYM ELF64_R_SYM
#define ELF_R_TYPE ELF64_R_TYPE
#define ELF_RELOCATION_WIDTH(type) ((type) == 1 ? 8u : 4u)
#endif
#endif
