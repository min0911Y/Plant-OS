LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/fputest.obj ../out/fputest_asm.obj

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/fputest.bin

../out/fputest.obj : fputest.c Makefile
	$(C) -c fputest.c -o ../out/fputest.obj

../out/fputest_asm.obj : fputest.asm Makefile
	nasm -f elf fputest.asm -o ../out/fputest_asm.obj
