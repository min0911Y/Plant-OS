ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/rpctest.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/rpctest.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/rpctest.bin
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
