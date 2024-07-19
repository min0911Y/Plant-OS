LIBS_PATH := ../libs
INCLUDE_PATH := ../include
include ../defs.mk
NASM = nasm
OBJS_PACK = ../out/lite_main.obj ../out/api.obj ../out/renderer_font.obj ../out/api_renderer.obj ../out/system.obj ../out/stb_truetype.obj \
../out/rencache.obj ../out/renderer.obj lapi.obj      lfunc.obj     lparser.obj lauxlib.obj   lgc.obj       lstate.obj lbaselib.obj  linit.obj     lstring.obj lbitlib.obj   liolib.obj    lstrlib.obj lcode.obj     llex.obj      ltable.obj lcorolib.obj  lmathlib.obj  ltablib.obj lctype.obj    lmem.obj      ltm.obj ldblib.obj    loadlib.obj   lundump.obj ldebug.obj    lobject.obj   lvm.obj ldo.obj       lopcodes.obj  lzio.obj ldump.obj     loslib.obj
CFLAGS = -m32 -I$(INCLUDE_PATH) -nostdinc -nolibc -nostdlib -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -finput-charset=UTF-8 -fexec-charset=UTF-8 -march=pentium -Qn -O2 -w
default : $(OBJS_PACK)
	$(LINK) $(OBJS_PACK) ../libs/sdl2_old.a ../libs/libgui.a $(BASIC_LIB_C) -o ../out/lite.bin
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
%.obj : lua52/%.c Makefile
	$(C) -I./src -I../sdl2/include -I../sdl2/SDL2 -I../sdl2/src/video/yuv2rgb -c lua52/$*.c -o $*.obj	
../out/%.obj : %.cpp Makefile
	$(CPP) -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
