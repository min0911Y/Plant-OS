#ifndef __ELF_H__
#define __ELF_H__
#include <ctypes.h>
// elf documentation: http://www.sco.com/developers/devspecs/gabi41.pdf

// ELF HEADER

// identification indexes
#define EI_MAG0 0     // file identification
#define EI_MAG1 1     // file identification
#define EI_MAG2 2     // file identification
#define EI_MAG3 3     // file identification
#define EI_CLASS 4    // file class
#define EI_DATA 5     // data encoding
#define EI_VERSION 6  // file version
#define EI_PAD 7      // start of padding bytes
#define EI_NIDENT 16

// magic numbers
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// file class
#define ELFCLASSNONE 0  // invalid class
#define ELFCLASS32 1    // 32-bit objects
#define ELFCLASS64 2    // 64-bit objects

// data encoding
#define ELFDATANONE 0  // invalid data encoding
#define ELFDATA2LSB 1  // little endian
#define ELFDATA2MSB 2  // big endian

// object file type
#define ET_NONE 0         // no file type
#define ET_ELF 1          // Relocatable file
#define ET_EXEC 2         // Executable file
#define ET_DYN 3          // Shared object file
#define ET_CORE 4         // Core file
#define ET_LOPROC 0xff00  // Processor-specfic
#define ET_HIPROC 0xffff  // Processor-specfic

// machine architecture
#define EM_NONE 0   // No machine
#define EM_M32 1    // AT&T WE 32100
#define EM_SPARC 2  // SPARC
#define EM_386 3    // Intel 80386
#define EM_68K 4    // Motorola 68000
#define EM_88K 5    // Motorola 88000
#define EM_860 7    // Intel 80860
#define EM_MIPS 8   // MIPS RS3000

// object file version
#define EV_NONE 0     // Invalid version
#define EV_CURRENT 1  // Current version

typedef unsigned int Elf32_Addr;
typedef unsigned short Elf32_Half;
typedef unsigned int Elf32_Off;
typedef unsigned int Elf32_Sword;
typedef unsigned int Elf32_Word;
typedef int Elf32_SwordSigned;

typedef struct {
  unsigned char e_ident[EI_NIDENT];  // elf identification
  Elf32_Half e_type;                 // object file type
  Elf32_Half e_machine;              // required architecture
  Elf32_Word e_version;              // object file version
  Elf32_Addr e_entry;                // first transfer control virtual address
  Elf32_Off e_phoff;                 // program header table offset
  Elf32_Off e_shoff;                 // section header table offset
  Elf32_Word e_flags;                // processor-specific flags
  Elf32_Half e_ehsize;               // elf header size
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct {
  Elf32_Word st_name;
  Elf32_Addr st_value;
  Elf32_Word st_size;
  unsigned char st_info;
  unsigned char st_other;
  Elf32_Half st_shndx;
} Elf32_Sym;

typedef struct {
  Elf32_Addr r_offset;
  Elf32_Word r_info;
} Elf32_Rel;

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info)&0xf)
#define ELF32_ST_INFO(bind, type) (((bind) << 4) + ((type)&0xf))
#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((unsigned char)(info))

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1

// SECTION HEADER

// section type
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_SHLIB 10
#define SHT_DYNSYM 11
#define SHT_LOPROC 0x70000000
#define SHT_HIPROP 0x7fffffff
#define SHT_LOUSER 0x80000000
#define SHT_HIUSER 0xffffffff

// section attribute flags
#define SHF_WRITE 0x01
#define SHF_ALLOC 0x02
#define SHF_EXECINSTR 0x04
#define SHF_MASKPROC 0xf0000000

typedef struct {
  Elf32_Word sh_name;
  Elf32_Word sh_type;
  Elf32_Word sh_flags;
  Elf32_Addr sh_addr;
  Elf32_Off sh_offset;
  Elf32_Word sh_size;
  Elf32_Word sh_link;
  Elf32_Word sh_info;
  Elf32_Word sh_addralign;
  Elf32_Word sh_entsize;
} Elf32_Shdr;

#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2

// PROGRAM HEADER

// segment type
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4
#define LOPROC 0x70000000
#define HIPROC 0x7fffffff

typedef struct {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
} Elf32_Phdr;
bool elf32_validate_executable(const void* image, size_t image_size,
                               uint32_t* entry_out, uint32_t* image_end_out);
bool elf32_load_executable(const void* image, size_t image_size,
                           uint32_t* entry_out);
bool elf32_validate_relocatable(const void* image, size_t image_size);
Elf32_Shdr* elf32_section(Elf32_Ehdr* hdr, int index);
const char* elf32_section_name(Elf32_Ehdr* hdr, int index);
Elf32_Shdr* elf32_find_section(Elf32_Ehdr* hdr, const char* name);
Elf32_Sym* elf32_symtab(Elf32_Ehdr* hdr, Elf32_Shdr** symtab_shdr);
const char* elf32_string_table(Elf32_Ehdr* hdr, Elf32_Shdr* strtab_shdr);
#endif
