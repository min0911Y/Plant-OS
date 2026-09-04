LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk

OBJS_PACK = ../out/curl.obj

default: $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/curl.bin

../out/%.obj: %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
