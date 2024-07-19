LIBS_PATH := ../lib
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/minewep.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../lib/sdl2_image.a ../lib/libjpg.a ../lib/libpng.a ../lib/libz.a ../lib/sdl2.a ../lib/libgui.a $(BASIC_LIB_C) -o ../out/minewep.bin
../out/%.obj : %.c Makefile
	$(C) -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -I../SDL2_image -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj