#ifndef PLANT_ELF_H
#define PLANT_ELF_H
#include <ctypes.h>

enum {
  EI_MAG0,
  EI_MAG1,
  EI_MAG2,
  EI_MAG3,
  EI_CLASS,
  EI_DATA,
  EI_VERSION,
  EI_NIDENT = 16,
  ELFMAG0 = 0x7f,
  ELFMAG1 = 'E',
  ELFMAG2 = 'L',
  ELFMAG3 = 'F',
  ELFCLASS32 = 1,
  ELFCLASS64 = 2,
  ELFDATA2LSB = 1,
  EV_CURRENT = 1,
  ET_REL = 1,
  ET_EXEC = 2,
  ET_DYN = 3,
  EM_386 = 3,
  EM_X86_64 = 62,
  PT_NULL = 0,
  PT_LOAD = 1,
  PT_DYNAMIC = 2,
  PT_INTERP = 3,
  PT_NOTE = 4,
  PT_PHDR = 6,
  PT_TLS = 7,
  PT_GNU_STACK = 0x6474e551,
  PT_GNU_RELRO = 0x6474e552,
  PF_X = 1,
  PF_W = 2,
  PF_R = 4,
  SHT_NULL = 0,
  SHT_PROGBITS = 1,
  SHT_SYMTAB = 2,
  SHT_STRTAB = 3,
  SHT_RELA = 4,
  SHT_HASH = 5,
  SHT_DYNAMIC = 6,
  SHT_NOTE = 7,
  SHT_NOBITS = 8,
  SHT_REL = 9,
  SHT_DYNSYM = 11,
  SHF_WRITE = 1,
  SHF_ALLOC = 2,
  SHF_EXECINSTR = 4,
  SHN_UNDEF = 0,
  SHN_ABS = 0xfff1,
  SHN_COMMON = 0xfff2,
  STB_LOCAL = 0,
  STB_GLOBAL = 1,
  STB_WEAK = 2,
  STT_NOTYPE = 0,
  STT_OBJECT = 1,
  STT_FUNC = 2,
  STT_SECTION = 3,
  STT_FILE = 4,
  STT_TLS = 6,
  STT_GNU_IFUNC = 10,
  STV_DEFAULT = 0,
  STV_INTERNAL = 1,
  STV_HIDDEN = 2,
  STV_PROTECTED = 3,
  DT_NULL = 0,
  DT_NEEDED = 1,
  DT_PLTRELSZ = 2,
  DT_PLTGOT = 3,
  DT_HASH = 4,
  DT_STRTAB = 5,
  DT_SYMTAB = 6,
  DT_RELA = 7,
  DT_RELASZ = 8,
  DT_RELAENT = 9,
  DT_STRSZ = 10,
  DT_SYMENT = 11,
  DT_INIT = 12,
  DT_FINI = 13,
  DT_SONAME = 14,
  DT_RPATH = 15,
  DT_SYMBOLIC = 16,
  DT_REL = 17,
  DT_RELSZ = 18,
  DT_RELENT = 19,
  DT_PLTREL = 20,
  DT_DEBUG = 21,
  DT_TEXTREL = 22,
  DT_JMPREL = 23,
  DT_BIND_NOW = 24,
  DT_INIT_ARRAY = 25,
  DT_FINI_ARRAY = 26,
  DT_INIT_ARRAYSZ = 27,
  DT_FINI_ARRAYSZ = 28,
  DT_RUNPATH = 29,
  DT_FLAGS = 30,
  DT_PREINIT_ARRAY = 32,
  DT_PREINIT_ARRAYSZ = 33,
  DT_RELRSZ = 35,
  DT_RELR = 36,
  DT_RELRENT = 37,
  DT_GNU_HASH = 0x6ffffef5,
  DT_VERSYM = 0x6ffffff0,
  DT_RELACOUNT = 0x6ffffff9,
  DT_RELCOUNT = 0x6ffffffa,
  DT_FLAGS_1 = 0x6ffffffb,
  DT_VERDEF = 0x6ffffffc,
  DT_VERDEFNUM = 0x6ffffffd,
  DT_VERNEED = 0x6ffffffe,
  DT_VERNEEDNUM = 0x6fffffff,
  DF_ORIGIN = 1,
  DF_SYMBOLIC = 2,
  DF_TEXTREL = 4,
  DF_BIND_NOW = 8,
  DF_STATIC_TLS = 16,
  DF_1_NOW = 1,
  DF_1_PIE = 0x08000000,
  R_386_NONE = 0,
  R_386_32 = 1,
  R_386_PC32 = 2,
  R_386_COPY = 5,
  R_386_GLOB_DAT = 6,
  R_386_JMP_SLOT = 7,
  R_386_RELATIVE = 8,
  R_X86_64_NONE = 0,
  R_X86_64_64 = 1,
  R_X86_64_PC32 = 2,
  R_X86_64_COPY = 5,
  R_X86_64_GLOB_DAT = 6,
  R_X86_64_JUMP_SLOT = 7,
  R_X86_64_RELATIVE = 8,
  R_X86_64_32 = 10,
  R_X86_64_32S = 11,
};

typedef uint32_t Elf32_Addr, Elf32_Off, Elf32_Word;
typedef int32_t Elf32_Sword;
typedef uint16_t Elf32_Half;
typedef uint64_t Elf64_Addr, Elf64_Off, Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint32_t Elf64_Word;
typedef uint16_t Elf64_Half;

typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;
typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version;
  uint64_t e_entry, e_phoff, e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
  uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
      p_align;
} Elf32_Phdr;
typedef struct {
  uint32_t p_type, p_flags;
  uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;
typedef struct {
  uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
  uint32_t sh_link, sh_info, sh_addralign, sh_entsize;
} Elf32_Shdr;
typedef struct {
  uint32_t sh_name, sh_type;
  uint64_t sh_flags, sh_addr, sh_offset, sh_size;
  uint32_t sh_link, sh_info;
  uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;
typedef struct {
  uint32_t st_name, st_value, st_size;
  uint8_t st_info, st_other;
  uint16_t st_shndx;
} Elf32_Sym;
typedef struct {
  uint32_t st_name;
  uint8_t st_info, st_other;
  uint16_t st_shndx;
  uint64_t st_value, st_size;
} Elf64_Sym;
typedef struct {
  uint32_t r_offset, r_info;
} Elf32_Rel;
typedef struct {
  uint32_t r_offset, r_info;
  int32_t r_addend;
} Elf32_Rela;
typedef struct {
  uint64_t r_offset, r_info;
} Elf64_Rel;
typedef struct {
  uint64_t r_offset, r_info;
  int64_t r_addend;
} Elf64_Rela;
typedef struct {
  int32_t d_tag;
  union {
    uint32_t d_val, d_ptr;
  } d_un;
} Elf32_Dyn;
typedef struct {
  int64_t d_tag;
  union {
    uint64_t d_val, d_ptr;
  } d_un;
} Elf64_Dyn;

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info) & 15)
#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((uint8_t)(info))
#define ELF64_R_SYM(info) ((info) >> 32)
#define ELF64_R_TYPE(info) ((uint32_t)(info))

#if __SIZEOF_POINTER__ == 8
typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Phdr Elf_Phdr;
typedef Elf64_Sym Elf_Sym;
typedef Elf64_Dyn Elf_Dyn;
#define ELF_NATIVE_CLASS ELFCLASS64
#define ELF_NATIVE_MACHINE EM_X86_64
#else
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Sym Elf_Sym;
typedef Elf32_Dyn Elf_Dyn;
#define ELF_NATIVE_CLASS ELFCLASS32
#define ELF_NATIVE_MACHINE EM_386
#endif
#endif
