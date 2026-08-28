LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/guitest.obj

default: $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(LIBS_PATH)/libgui.a $(BASIC_LIB_C) -o ../out/guitest.bin

../out/guitest.obj: guitest.c Makefile
	$(C) -c guitest.c -o ../out/guitest.obj
