ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/doom.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
include sources.mk
OBJS_PACK = $(patsubst $(DOOM_ROOT)/%.c,%.o,$(DOOM_SOURCES))
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../libs/sdl2.a ../libs/libgui.a $(BASIC_LIB_C) -o ../out/doom.bin
%.o : %.c Makefile
	$(C) -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c $*.c -o $*.o
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj

else
$(error unsupported ARCH)
endif
