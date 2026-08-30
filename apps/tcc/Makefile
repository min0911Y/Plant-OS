LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/tcc.obj
CRT_OBJ = ../out/crti.obj
default : $(OBJS_PACK) $(CRT_OBJ)
	$(LINK) $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/tcc.bin
$(CRT_OBJ) : tcc/crti.c Makefile
	$(C) -c $< -o $@
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
