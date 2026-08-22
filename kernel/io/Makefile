include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_io = $(out_path_src)Draw.o $(out_path_src)HighTextMode.o $(out_path_src)input.o \
	     $(out_path_src)Input_Stack.o $(out_path_src)log.o $(out_path_src)screen.o \
	     $(out_path_src)tty.o $(out_path_src)sheet.o $(out_path_src)TextMode.o $(out_path_src)fartty.o
default : $(obj_io)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
