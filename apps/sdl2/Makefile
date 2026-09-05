ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/sdl2.a
include ../native.mk
else ifeq ($(ARCH),i386)
INCLUDE_DIRS := -I../include -I./include -I./SDL2 -I./src/video/yuv2rgb
CFLAGS := -m32 $(INCLUDE_DIRS) -nostdinc -nostdlib -fno-builtin -ffreestanding \
	-fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fomit-frame-pointer -march=pentium -O3 -Wall -MMD -MP

OBJDIR := ../out/sdl2
LIB := ../libs/sdl2.a
include sources.mk
SOURCES := $(SDL_SOURCES)
OBJECTS := $(patsubst ./src/%.c,$(OBJDIR)/%.o,$(SOURCES))

default : $(LIB)

$(LIB): $(OBJECTS) Makefile sources.mk
	rm -f $@
	ar rcs $@ $(OBJECTS)

$(OBJDIR)/%.o: src/%.c Makefile sources.mk
	mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(OBJDIR)
	rm -f $(LIB)

else
$(error unsupported ARCH)
endif
