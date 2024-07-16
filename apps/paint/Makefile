LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/paint.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(LIBS_PATH)/libgui.a $(BASIC_LIB_C) -o ../out/paint.bin
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
