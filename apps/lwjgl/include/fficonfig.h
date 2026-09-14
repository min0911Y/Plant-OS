/* Fixed libffi configuration for the Plant OS x86_64 ABI. */
#ifndef PLANT_LIBFFI_CONFIG_H
#define PLANT_LIBFFI_CONFIG_H

#define HAVE_ALLOCA_H 1
#define HAVE_AS_CFI_PSEUDO_OP 1
#define HAVE_AS_X86_64_UNWIND_SECTION_TYPE 1
#define HAVE_AS_X86_PCREL 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#define HAVE_MEMCPY 1
#define HAVE_RO_EH_FRAME 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define STDC_HEADERS 1

#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name) .hidden name
#else
#define FFI_HIDDEN __attribute__((visibility("hidden")))
#endif

#endif
