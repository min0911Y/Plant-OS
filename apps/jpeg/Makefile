ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/libjpg.a
include ../native.mk
else ifeq ($(ARCH),i386)



SRC			:= *.c


default : 
	gcc -m32 -I../include -I./include -I./SDL2 -I./src/video/yuv2rgb -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O2 -w -c $(SRC)
	ar rv ../libs/libjpg.a *.o
else
$(error unsupported ARCH)
endif
