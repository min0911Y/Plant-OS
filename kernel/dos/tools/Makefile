include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_dos := $(out_path_src)arg.o $(out_path_src)bit.o $(out_path_src)fifo.o $(out_path_src)list.o $(out_path_src)md5.o $(out_path_src)other.o $(out_path_src)rand.o $(out_path_src)time.o
default : $(obj_dos)
	
$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	$(TOOLPATH)nasm.exe --prefix _ -f win32 $*.asm -o $(out_path_src)$*.obj
