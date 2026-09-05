include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_std = $(out_path_src)std.o $(out_path_src)format.o $(out_path_src)libcpp.o $(out_path_src)linux_style_file.o
-include $(call kernel_depfiles,$(obj_std))
default : $(obj_std)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
$(out_path_src)%.o : ./%.cpp Makefile
	g++ $(KERNEL_COMMON_CXXFLAGS) -O3 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.cpp -o $(out_path_src)$*.o
