ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/timetest.bin
include ../native.mk
else
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
default: ../out/timetest.bin
../out/timetest.obj: timetest.c Makefile
	$(C) -I../sdl2/include -I../SDL2_ttf -c $< -o $@
../out/timetest.bin: ../out/timetest.obj
	$(LINK) $< --start-group $(BASIC_LIB_C) --end-group -o $@
endif
