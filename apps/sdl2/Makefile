INCLUDE_DIRS := -I../include -I./include -I./SDL2 -I./src/video/yuv2rgb
CFLAGS := -m32 $(INCLUDE_DIRS) -nostdinc -nostdlib -fno-builtin -ffreestanding \
	-fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
	-fomit-frame-pointer -march=pentium -O3 -Wall

OBJDIR := ../out/sdl2
LIB := ../libs/sdl2.a
SOURCES := \
	src/SDL.c \
	src/SDL_assert.c \
	src/SDL_dataqueue.c \
	src/SDL_error.c \
	src/SDL_guid.c \
	src/SDL_hints.c \
	src/SDL_list.c \
	src/SDL_log.c \
	src/SDL_utils.c \
	$(wildcard src/atomic/*.c) \
	$(wildcard src/audio/*.c) \
	$(wildcard src/cpuinfo/*.c) \
	$(wildcard src/events/*.c) \
	$(wildcard src/file/*.c) \
	$(wildcard src/haptic/*.c) \
	$(wildcard src/joystick/*.c) \
	$(wildcard src/power/*.c) \
	$(wildcard src/render/*.c) \
	$(wildcard src/render/software/*.c) \
	$(wildcard src/stdlib/*.c) \
	$(wildcard src/thread/*.c) \
	$(wildcard src/thread/generic/*.c) \
	$(wildcard src/timer/*.c) \
	$(wildcard src/video/*.c) \
	$(wildcard src/video/yuv2rgb/*.c) \
	$(wildcard src/audio/disk/*.c) \
	$(wildcard src/audio/dummy/*.c) \
	$(wildcard src/filesystem/dummy/*.c) \
	$(wildcard src/video/dummy/*.c) \
	$(wildcard src/haptic/dummy/*.c) \
	$(wildcard src/joystick/dummy/*.c) \
	$(wildcard src/main/dummy/*.c) \
	$(wildcard src/timer/dummy/*.c) \
	$(wildcard src/loadso/dummy/*.c) \
	$(wildcard src/timer/plos/*.c) \
	$(wildcard src/video/plos/*.c)
OBJECTS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SOURCES))

default : $(LIB)

$(LIB): $(OBJECTS)
	ar rcs $@ $(OBJECTS)

$(OBJDIR)/%.o: src/%.c Makefile
	mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR)
	rm -f $(LIB)
