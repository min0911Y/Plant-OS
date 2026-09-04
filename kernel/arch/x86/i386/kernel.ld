OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(x86_boot_entry)

PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x00280000;

  .text ALIGN(0x1000) :
  {
    __kernel_text_start = .;
    KEEP(*(.multiboot2))
    KEEP(*(.text.boot))
    *(.text .text.* .gnu.linkonce.t.*)
    __kernel_text_end = .;
  } :text

  .rodata ALIGN(0x1000) :
  {
    *(.rodata.boot)
    *(.rodata .rodata.* .gnu.linkonce.r.*)
  } :rodata

  .data ALIGN(0x1000) :
  {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.got .got.*)
    *(.init_array .init_array.*)
    *(.fini_array .fini_array.*)
  } :data

  .bss ALIGN(0x1000) (NOLOAD) :
  {
    *(COMMON)
    *(.bss.boot_stack)
    *(.bss .bss.* .gnu.linkonce.b.*)
  } :data

  __kernel_end = .;
  ASSERT(__kernel_end <= 0x00400000,
         "kernel overlaps the bootstrap paging structures at 0x00400000")

  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame .eh_frame.*)
    *(.note .note.*)
  }
}
