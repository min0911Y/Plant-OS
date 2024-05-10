LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/lite_main.obj ../out/api.obj ../out/renderer_font.obj ../out/api_renderer.obj ../out/system.obj ../out/stb_truetype.obj \
../out/rencache.obj ../out/renderer.obj
CFLAGS := -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=GB2312 -march=pentium -Qn -O2 -w
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../libs/liblua.a ../libs/sdl2.a ../libs/libgui.a $(BASIC_LIB_C) -o ../out/lite.bin
../out/%.obj : src/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c $*.c -o ../out/$*.obj
../out/%.obj : src/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c src/$*.c -o ../out/$*.obj
../out/%.obj : src/api/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c src/api/$*.c -o ../out/$*.obj
../out/%.obj : src/lib/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c src/lib/$*.c -o ../out/$*.obj	
../out/%.obj : src/lib/stb/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c src/lib/stb/$*.c -o ../out/$*.obj	
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
