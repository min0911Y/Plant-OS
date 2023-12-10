NASM = nasm
OBJS_PACK = ../out/adler32.obj ../out/compress.obj ../out/crc32.obj ../out/deflate.obj ../out/gzclose.obj ../out/gzlib.obj ../out/gzread.obj ../out/gzwrite.obj ../out/infback.obj ../out/inffast.obj ../out/inflate.obj ../out/inftrees.obj ../out/trees.obj ../out/uncompr.obj ../out/zutil.obj
default : $(OBJS_PACK)
	ar rv ../libs/libz.a $(OBJS_PACK)
../out/%.obj : %.c Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.c -o ../out/$*.obj
../out/%.obj : %.cpp Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
