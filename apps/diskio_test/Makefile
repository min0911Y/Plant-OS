LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/dktest.obj

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/dktest.bin

../out/dktest.obj : diskio_test.c Makefile
	$(C) -c diskio_test.c -o ../out/dktest.obj
