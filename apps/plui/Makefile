LIBS_PATH := ../lib
INCLUDE_PATH := ../include
include ../defs.mk
CFLAGS = -nostdlib -std=gnu2x -O1 -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -fno-exceptions
CPPFLAGS = -nostdlib -std=gnu++2b -O1 -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -fno-exceptions
default : Makefile
	mkdir -p build && cd build && cmake .. && make -j && cd ..
	gcc -c $(CFLAGS) -I../include -I./include main.c -o ../out/plui.c.obj
	g++ -c $(CPPFLAGS) -I../include -I./include main.cpp -o ../out/plui.cpp.obj
	$(LINK) ../out/plui.c.obj ../out/plui.cpp.obj ../lib/libplui.a ../lib/libplds.a ../lib/libpl2d.a ../lib/libplui-cpp.a ../lib/libplui-c.a $(BASIC_LIB_C) -o ../out/plui.bin
clean :
	rm -r build
