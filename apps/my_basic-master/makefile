LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/copy.obj
default: ../out/my_basic
../out/my_basic: ../out/my_basic.obj ../out/main.obj
	$(LINK) ../out/my_basic.obj ../out/main.obj $(BASIC_LIB_C) -o ../out/my_basic.bin
../out/my_basic.obj: core/my_basic.c core/my_basic.h
	gcc -m32 -O0 -c -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -fno-stack-protector -ffreestanding core/my_basic.c -o ../out/my_basic.obj
../out/main.obj: shell/main.c core/my_basic.h
	gcc -m32 -O0 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -fno-stack-protector -ffreestanding -c shell/main.c -o ../out/main.obj

clean:
	rm ../out/my_basic.obj ../out/main.obj
