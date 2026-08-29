include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_dos := $(out_path_src)syscall.o
-include $(call kernel_depfiles,$(obj_dos))
default : $(obj_dos)
	
$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	$(TOOLPATH)nasm.exe --prefix _ -f win32 $*.asm -o $(out_path_src)$*.obj
