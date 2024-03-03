LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK1 = ../out/lua.obj
OBJS_PACK2 = ../out/luac.obj
default : $(OBJS_PACK1) $(OBJS_PACK2)
	$(LINK) $(OBJS_PACK1) $(BASIC_LIB_C) -o ../out/lua.bin
	$(LINK) $(OBJS_PACK2) $(BASIC_LIB_C) -o ../out/luac.bin
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
