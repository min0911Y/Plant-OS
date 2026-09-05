ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/sdl2_image.a
include ../native.mk
else ifeq ($(ARCH),i386)


CFLAGS	+= -DSDL_IMAGE_USE_COMMON_BACKEND \
			-DLOAD_BMP -DLOAD_GIF -DLOAD_LBM -DLOAD_PCX -DLOAD_PNM \
			-DLOAD_SVG -DLOAD_TGA -DLOAD_XCF -DLOAD_XPM -DLOAD_XV \
			-DLOAD_JPG -DLOAD_PNG

SRC			:= *.c

default : 
	gcc -m32 $(CFLAGS) -I../include -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -I../jpeg -I../libpng -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O2 -w -c $(SRC)
	ar rv ../libs/sdl2_image.a *.o
else
$(error unsupported ARCH)
endif
