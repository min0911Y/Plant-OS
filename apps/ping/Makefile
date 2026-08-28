LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/ping.obj

default: $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/ping.bin

../out/%.obj: %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
