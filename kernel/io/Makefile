include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_io = $(out_path_src)input_device.o  $(out_path_src)draw.o $(out_path_src)httm.o $(out_path_src)input.o \
	     $(out_path_src)is.o $(out_path_src)log.o $(out_path_src)screen.o \
	     $(out_path_src)tty.o $(out_path_src)sheet.o $(out_path_src)tm.o $(out_path_src)fartty.o
-include $(call kernel_depfiles,$(obj_io))
default : $(obj_io)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
