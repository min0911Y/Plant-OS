include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_dos := $(out_path_src)heap.o $(out_path_src)kasan.o $(out_path_src)user_vm.o $(out_path_src)user_vm_file.o
-include $(call kernel_depfiles,$(obj_dos))
default : $(obj_dos)
	
$(out_path_src)heap.o : heap.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O3 -Wall -Wextra -Werror $(KERNEL_PACKED_BITFIELD_FLAGS) -c heap.c -o $(out_path_src)heap.o

$(out_path_src)kasan.o : kasan.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O2 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c kasan.c -o $(out_path_src)kasan.o

$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	$(TOOLPATH)nasm.exe --prefix _ -f win32 $*.asm -o $(out_path_src)$*.obj
