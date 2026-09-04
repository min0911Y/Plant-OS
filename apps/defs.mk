ARCH ?= i386
ifeq ($(ARCH),i386)
ARCH_CFLAGS = -m32 -march=pentium -DPLANT_ARCH_I386
ARCH_LD_EMULATION = elf_i386
ARCH_ASM_FORMAT = elf32
ENTRYPOINT = 0x70000000
else
$(error unsupported ARCH '$(ARCH)'; supported application architectures: i386)
endif

CFLAGS = $(ARCH_CFLAGS) -std=gnu17 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -Qn -O0 -w
CPPFLAGS = $(ARCH_CFLAGS) -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-exceptions -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -Qn -O3 -fno-rtti -w
CC = gcc

C = $(CC) $(CFLAGS)
CPP = $(CC) $(CPPFLAGS)
LD = ld
LD_FLAGS = -m $(ARCH_LD_EMULATION) -static -N -e Main -Ttext $(ENTRYPOINT)
LINK = $(LD) $(LD_FLAGS)
BASIC_LIB_C = $(LIBS_PATH)/libp.a $(LIBS_PATH)/libtcc1.a $(LIBS_PATH)/libabi.a
BASIC_LIB_CPP = $(LIBS_PATH)/libcpps.a $(LIBS_PATH)/libtcc1.a $(LIBS_PATH)/libabi.a
