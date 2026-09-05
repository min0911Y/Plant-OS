ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/libft.a
include ../native.mk
else ifeq ($(ARCH),i386)
INCDIRS		+= 	-Iinclude \
				-Isrc/autofit \
				-Isrc/base \
				-Isrc/cache \
				-Isrc/cff \
				-Isrc/cid \
				-Isrc/gxvalid \
				-Isrc/otvalid \
				-Isrc/pfr \
				-Isrc/psaux \
				-Isrc/pshinter \
				-Isrc/psnames \
				-Isrc/raster \
				-Isrc/sfnt \
				-Isrc/smooth \
				-Isrc/truetype \
				-Isrc/type1 \
				-Isrc/type42 \
				-Isrc/winfonts

SRC			:=	src/autofit/*.c \
				src/base/*.c \
				src/cache/*.c \
				src/cff/*.c \
				src/cid/*.c \
				src/gxvalid/*.c \
				src/otvalid/*.c \
				src/pfr/*.c \
				src/psaux/*.c\
				src/pshinter/*.c \
				src/psnames/*.c \
				src/raster/*.c \
				src/sfnt/*.c \
				src/smooth/*.c \
				src/truetype/*.c \
				src/type1/*.c \
				src/type42/*.c \
				src/winfonts/*.c

default : 
	gcc -m32 -I../include $(INCDIRS) -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O2 -w -c $(SRC)
	ar rv ../libs/libft.a *.o
else
$(error unsupported ARCH)
endif
