LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/invader.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../libs/sdl2_old.a ../libs/libgui.a $(BASIC_LIB_C) -o ../out/invader.bin
../out/%.obj : %.c Makefile sdl2.c
	$(C) -c $*.c -o ../out/$*.obj -I. -I../sdl2/SDL2
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
