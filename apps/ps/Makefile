ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/ps.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/ps.obj

default: $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/ps.bin

../out/%.obj: %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
