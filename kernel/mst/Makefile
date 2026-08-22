include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_net = $(out_path_src)build.o $(out_path_src)mst.o $(out_path_src)str.o
default : $(obj_net)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
