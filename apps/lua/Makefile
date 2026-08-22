LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
CFLAGS = -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -march=pentium -Qn -O0 -w
NASM = nasm
OBJS_PACK1 = ../out/lua.obj
OBJS_PACK2 = ../out/luac.obj
OBJS_PACK3 = ../out/lualib.obj
default : $(OBJS_PACK1) $(OBJS_PACK2) $(OBJS_PACK3)
	$(LINK) $(OBJS_PACK1) $(BASIC_LIB_C) -o ../out/lua.bin
	$(LINK) $(OBJS_PACK2) $(BASIC_LIB_C) -o ../out/luac.bin
	ar rv ../libs/liblua.a $(OBJS_PACK3)
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
