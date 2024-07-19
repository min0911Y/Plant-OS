LIBS_PATH := ../lib
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/gui.obj  ../out/list.obj ../out/sheet.obj ../out/desktop.obj ../out/button.obj ../out/super_window.obj ../out/textbox.obj ../out/window.obj ../out/gmouse.obj ../out/console.obj ../out/gttf.obj ../out/fifo.obj ../out/queue.obj
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) $(LIBS_PATH)/libutf.a $(BASIC_LIB_C) -o ../out/gui.bin
../out/%.obj : %.c Makefile
	gcc -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -Qn -O3 -w  -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
