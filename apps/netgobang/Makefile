LIBS_PATH := ../lib
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/netgobang.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/netgobang.bin
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
