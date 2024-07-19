LIBS_PATH := ../lib
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/nk.obj
CFLAGS = -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=UTF-8 -march=pentium -Qn -O3 -w
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../lib/sdl2.a ../lib/libgui.a $(BASIC_LIB_C) -o ../out/nk.bin
../out/%.obj : %.c Makefile
	$(C) -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
