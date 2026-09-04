LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/fputest.obj ../out/fputest_asm.obj
ARCH_SOURCE := arch/$(ARCH)

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/fputest.bin

../out/fputest.obj : fputest.c Makefile
	$(C) -c fputest.c -o ../out/fputest.obj

../out/fputest_asm.obj : $(ARCH_SOURCE)/fputest.asm Makefile
	nasm -f $(ARCH_ASM_FORMAT) $< -o ../out/fputest_asm.obj
