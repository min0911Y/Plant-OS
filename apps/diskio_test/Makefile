ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := out/x86_64/dktest.bin
include ../native.mk
else ifeq ($(ARCH),i386)
LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/dktest.obj

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/dktest.bin

../out/dktest.obj : diskio_test.c Makefile
	$(C) -c diskio_test.c -o ../out/dktest.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
