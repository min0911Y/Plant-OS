include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
TOOLPATH := ../../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_dos := $(out_path_src)ipc.o $(out_path_src)lock.o $(out_path_src)mtask.o $(out_path_src)basetask.o $(out_path_src)execute.o $(out_path_src)signal.o
TASK_O0 := execute
obj_dos += $(out_path_src)futex.o
obj_dos += $(out_path_src)user_thread.o
-include $(call kernel_depfiles,$(obj_dos))
default : $(obj_dos)
	
$(out_path_src)%.o : %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) $(if $(filter $*,$(TASK_O0)),-O0,-O3) -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj : %.asm Makefile
	$(TOOLPATH)nasm.exe --prefix _ -f win32 $*.asm -o $(out_path_src)$*.obj
