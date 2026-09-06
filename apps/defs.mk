ARCH ?= i386
ifeq ($(ARCH),i386)
ARCH_CFLAGS = -m32 -march=pentium -DPLANT_ARCH_I386
ARCH_LD_EMULATION = elf_i386
ARCH_ASM_FORMAT = elf32
else
$(error this application Makefile has no ARCH '$(ARCH)' backend; ported x86_64 applications use apps/native.mk)
endif

CFLAGS = $(ARCH_CFLAGS) -std=gnu17 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fPIC -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -Qn -O0 -w
CPPFLAGS = $(ARCH_CFLAGS) -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-exceptions -fno-stack-protector -Qn -fPIC -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -Qn -O3 -fno-rtti -w
CC = gcc

C = $(CC) $(CFLAGS)
CPP = $(CC) $(CPPFLAGS)
LD = ld
LD_FLAGS = -m $(ARCH_LD_EMULATION) -pie -z max-page-size=4096 -z noexecstack -z relro -z now -z text -e Main --dynamic-linker /lib/ld.so
LINK = $(LD) $(LD_FLAGS)
BASIC_LIB_C = ../out/dynamic/libp/entry.o ../out/lib/libp.so
BASIC_LIB_CPP = ../out/dynamic/libp/entry.o ../out/lib/libcpp.so ../out/lib/libp.so
