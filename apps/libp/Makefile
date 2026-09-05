ARCH ?= i386
ifeq ($(ARCH),x86_64)
NATIVE_TARGETS := libs/x86_64/libp.a libs/x86_64/libcpps.a
include ../native.mk
else ifeq ($(ARCH),i386)
NASM = nasm
INCLUDE_PATH := ../include
include ../defs.mk
ARCH_SOURCE := arch/$(ARCH)
OBJS_PACK = ../out/entry.obj ../out/syscall.obj ../out/rand.obj ../out/arg.obj \
			../out/mouse.obj ../out/math.obj ../out/math_arch.obj ../out/libc.obj ../out/socket.obj ../out/socket_syscall.obj ../out/vfs_syscall.obj ../out/time.obj ../out/posix.obj ../out/pow.obj ../out/exit.obj \
			../out/ipc.obj ../out/rpc.obj ../out/runtime_args.obj ../out/runtime_lifecycle.obj ../out/cxxabi.obj ../out/task.obj ../out/video.obj ../out/search.obj
OBJS_PACK_CPP = ../out/cppstart.obj ../out/syscall.obj ../out/rand.obj ../out/arg.obj \
			../out/mouse.obj ../out/math.obj ../out/math_arch.obj ../out/libc.obj ../out/socket.obj ../out/socket_syscall.obj ../out/vfs_syscall.obj ../out/time.obj ../out/posix.obj ../out/pow.obj ../out/exit.obj \
			../out/ipc.obj ../out/rpc.obj ../out/runtime_args.obj ../out/runtime_lifecycle.obj ../out/cxxabi.obj ../out/task.obj ../out/video.obj ../out/search.obj
OBJS_PACK_GUI = ../out/libp_gui_rpc.obj
default : $(OBJS_PACK_GUI) $(OBJS_PACK) $(OBJS_GUI) ../out/abi.obj $(OBJS_PACK_CPP)
	rm -f ../libs/libp.a ../libs/libcpps.a ../libs/libabi.a
	ar rcs ../libs/libp.a $(OBJS_PACK)
	ar rcs ../libs/libcpps.a $(OBJS_PACK_CPP)
	ar rcs ../libs/libabi.a ../out/abi.obj
	rm -f ../libs/libgui.a
	ar rcs ../libs/libgui.a $(OBJS_PACK_GUI)
../out/%.obj : %.c Makefile
	$(C) -fno-builtin -c $*.c -o ../out/$*.obj
../out/libp_gui_rpc.obj : gui_rpc.c Makefile
	$(C) -fno-builtin -c $< -o $@
../out/math_arch.obj: $(ARCH_SOURCE)/math.c Makefile
	$(C) -fno-builtin -c $< -o $@
../out/%.obj : %.cpp Makefile
	$(CPP) -fno-builtin -c $*.cpp -o ../out/$*.obj
../out/syscall.obj: $(ARCH_SOURCE)/syscall.asm Makefile
	$(NASM) -f $(ARCH_ASM_FORMAT) $< -o $@
../out/socket_syscall.obj: $(ARCH_SOURCE)/socket_syscall.asm Makefile
	$(NASM) -f $(ARCH_ASM_FORMAT) $< -o $@
../out/vfs_syscall.obj: $(ARCH_SOURCE)/vfs_syscall.asm Makefile
	$(NASM) -f $(ARCH_ASM_FORMAT) $< -o $@

else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
