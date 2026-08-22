include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_dos := $(out_path_src)mem.o $(out_path_src)page.o $(out_path_src)kasan.o
default : $(obj_dos)
	
$(out_path_src)mem.o : mem.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c mem.c -o $(out_path_src)mem.o

$(out_path_src)page.o : page.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c page.c -o $(out_path_src)page.o

$(out_path_src)kasan.o : kasan.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O2 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c kasan.c -o $(out_path_src)kasan.o

$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	$(TOOLPATH)nasm.exe --prefix _ -f win32 $*.asm -o $(out_path_src)$*.obj
