NASM = nasm
OBJS_PACK = ../out/entry.obj ../out/syscall.obj ../out/rand.obj ../out/arg.obj \
			../out/mouse.obj ../out/math.obj ../out/libc.obj ../out/socket.obj ../out/socket_syscall.obj ../out/time.obj ../out/posix.obj ../out/pow.obj ../out/exit.obj \
			../out/ipc.obj ../out/rpc.obj ../out/runtime_args.obj ../out/task.obj
OBJS_PACK_CPP = ../out/cppstart.obj ../out/syscall.obj ../out/rand.obj ../out/arg.obj \
			../out/mouse.obj ../out/math.obj ../out/libc.obj ../out/socket.obj ../out/socket_syscall.obj ../out/time.obj ../out/posix.obj ../out/pow.obj ../out/exit.obj \
			../out/ipc.obj ../out/rpc.obj ../out/runtime_args.obj ../out/task.obj
OBJS_PACK_GUI = ../out/libp_gui_rpc.obj
default : $(OBJS_PACK_GUI) $(OBJS_PACK) $(OBJS_GUI) ../out/abi.obj $(OBJS_PACK_CPP)
	rm -f ../libs/libp.a ../libs/libcpps.a ../libs/libabi.a
	ar rcs ../libs/libp.a $(OBJS_PACK)
	ar rcs ../libs/libcpps.a $(OBJS_PACK_CPP)
	ar rcs ../libs/libabi.a ../out/abi.obj
	rm -f ../libs/libgui.a
	ar rcs ../libs/libgui.a $(OBJS_PACK_GUI)
../out/%.obj : %.c Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.c -o ../out/$*.obj
../out/libp_gui_rpc.obj : gui_rpc.c Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $< -o $@
../out/%.obj : %.cpp Makefile
	gcc -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c $*.cpp -o ../out/$*.obj
../out/%.obj : %.asm Makefile
	nasm -f elf $*.asm -o ../out/$*.obj
