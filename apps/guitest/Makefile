ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/guitest.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/guitest.obj

default: $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(LIBS_PATH)/libgui.a $(BASIC_LIB_C) -o ../out/guitest.bin

../out/guitest.obj: guitest.c Makefile
	$(C) -c guitest.c -o ../out/guitest.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
