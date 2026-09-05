ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/exc_test.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/exc_test.obj

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/exc_test.bin

../out/exc_test.obj : exc_test.c Makefile
	$(C) -c exc_test.c -o ../out/exc_test.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
