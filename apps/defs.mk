CFLAGS := -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -march=pentium -Qn -O0 -w
CPPFLAGS := -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-exceptions -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -Qn -O3 -march=pentium -fno-rtti -w

CC := gcc

C := $(CC) $(CFLAGS)
CPP := $(CC) $(CPPFLAGS)
ENTRYPOINT := 0x70000000
LD := ld
LD_FLAGS := -m elf_i386 -static -e Main -Ttext $(ENTRYPOINT)
LINK := $(LD) $(LD_FLAGS)
BASIC_LIB_C := $(LIBS_PATH)/libp.a $(LIBS_PATH)/libtcc1.a $(LIBS_PATH)/libabi.a
BASIC_LIB_CPP := $(LIBS_PATH)/libcpps.a $(LIBS_PATH)/libtcc1.a $(LIBS_PATH)/libabi.a
