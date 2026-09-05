ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/libmst.a
include ../native.mk
else ifeq ($(ARCH),i386)
NASM = nasm
OBJS_PACK = ../out/list.obj ../out/mst.obj ../out/build.obj ../out/str.obj
default : $(OBJS_PACK)
	ar rv ../libs/libmst.a $(OBJS_PACK)
../out/%.obj : %.c Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
