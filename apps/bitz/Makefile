LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/bitz.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../libs/sdl2_image.a ../libs/libjpg.a ../libs/libpng.a ../libs/libz.a ../libs/sdl2.a ../libs/libgui.a $(BASIC_LIB_C) -o ../out/bitz.bin
../out/%.obj : %.c Makefile
	$(C) -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -I../SDL2_image -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
