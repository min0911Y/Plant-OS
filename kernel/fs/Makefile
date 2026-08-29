include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_fs = $(out_path_src)fat.o $(out_path_src)file.o $(out_path_src)bmp.o \
	     $(out_path_src)path.o $(out_path_src)elf.o $(out_path_src)rootfs.o \
		 $(out_path_src)vfs.o $(out_path_src)pfs.o $(out_path_src)iso9660.o
-include $(call kernel_depfiles,$(obj_fs))
default : $(obj_fs)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
