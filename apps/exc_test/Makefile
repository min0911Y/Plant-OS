LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/exc_test.obj

default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/exc_test.bin

../out/exc_test.obj : exc_test.c Makefile
	$(C) -c exc_test.c -o ../out/exc_test.obj
