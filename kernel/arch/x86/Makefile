include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
obj_arch := $(out_path_src)descriptor_tables.o $(out_path_src)exceptions.o \
	$(out_path_src)irq.o $(out_path_src)smp.o $(out_path_src)task_context.o \
	$(out_path_src)bios_interrupt.obj $(out_path_src)exceptions.obj \
	$(out_path_src)interrupt_entries.obj $(out_path_src)smp_trampoline.obj \
	$(out_path_src)task_context.obj \
	$(out_path_src)user_return.obj

default: $(obj_arch)

$(out_path_src)%.o: %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj: %.asm Makefile
	nasm -f elf $*.asm -o $(out_path_src)$*.obj
