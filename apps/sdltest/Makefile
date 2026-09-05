ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/sdltest.bin
include ../native.mk
else
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
default: ../out/sdltest.bin
../out/sdltest.obj: sdltest.c Makefile
	$(C) -I../sdl2/include -I../SDL2_ttf -c $< -o $@
../out/sdltest.bin: ../out/sdltest.obj
	$(LINK) $< --start-group $(LIBS_PATH)/sdl2_ttf.a $(LIBS_PATH)/libft.a $(LIBS_PATH)/libz.a $(LIBS_PATH)/sdl2.a $(LIBS_PATH)/libgui.a $(BASIC_LIB_C) --end-group -o $@
endif
