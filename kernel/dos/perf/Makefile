include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/

obj_perf := $(out_path_src)perf.o

default : $(obj_perf)

$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O2 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o
