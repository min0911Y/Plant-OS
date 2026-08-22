include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := make
obj_dos := $(out_path_src)errors.obj $(out_path_src)handlers.obj $(out_path_src)i386.obj $(out_path_src)int32.obj $(out_path_src)setjmp.obj
default : $(obj_dos)
	
$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -mpreferred-stack-boundary=2 -O0 -w -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	nasm -f elf $*.asm -o $(out_path_src)$*.obj
