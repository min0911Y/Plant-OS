LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK1 = ../out/ioapi.obj ../out/minizip.obj ../out/mztools.obj ../out/unzip.obj ../out/zip.obj
OBJS_PACK2 = ../out/ioapi.obj ../out/miniunz.obj ../out/mztools.obj ../out/unzip.obj ../out/zip.obj
default : $(OBJS_PACK1) $(OBJS_PACK2)
	$(LINK) $(OBJS_PACK1) $(LIBS_PATH)/libz.a $(BASIC_LIB_C) -o ../out/minizip.bin
	$(LINK) $(OBJS_PACK2) $(LIBS_PATH)/libz.a $(BASIC_LIB_C) -o ../out/miniunz.bin
../out/%.obj : %.c Makefile
	$(C) -DMINIZIP_FOPEN_NO_64 -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
