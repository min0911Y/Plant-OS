


SRC			:= *.c


default : 
	gcc -m32 -I../include -I./include -I./SDL2 -I./src/video/yuv2rgb -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O2 -w -c $(SRC)
	ar rv ../lib/libjpg.a *.o