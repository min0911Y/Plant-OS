LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/shell_companion.obj ../out/shell_ext.obj ../out/shell_fs.obj ../out/log.obj ../out/shell_passthrough.obj ../out/shell_cmd_group.obj ../out/main.obj ../out/shell_port.obj ../out/shell.obj ../out/telnetd.obj
default : $(OBJS_PACK)
	ld -m elf_i386 -T shell.lds $(OBJS_PACK) $(BASIC_LIB_C) -o ../out/lsh.bin
../out/%.obj : %.c Makefile
	$(C) -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
