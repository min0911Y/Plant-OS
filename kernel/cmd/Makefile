include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_cmd = $(out_path_src)command.o
default : $(obj_cmd)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
