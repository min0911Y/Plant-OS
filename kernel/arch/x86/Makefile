include ../../cdef.mk
out_path_src := ../../obj/
incpath_src := ../../include/
obj_arch := $(out_path_src)boot.o $(out_path_src)descriptor_tables.o $(out_path_src)exceptions.o \
	$(out_path_src)fpu.o $(out_path_src)irq.o $(out_path_src)smp.o \
	$(out_path_src)task_context.o \
	$(out_path_src)boot_entry.obj $(out_path_src)bios_interrupt.obj $(out_path_src)exceptions.obj \
	$(out_path_src)interrupt_entries.obj $(out_path_src)smp_trampoline.obj \
	$(out_path_src)task_context.obj \
	$(out_path_src)user_return.obj

-include $(call kernel_depfiles,$(obj_arch))

default: $(obj_arch)

$(out_path_src)boot.o: boot.c Makefile
	gcc $(KERNEL_NOKASAN_CFLAGS) -O3 -Wall -Wextra -Werror $(KERNEL_PACKED_BITFIELD_FLAGS) -c boot.c -o $(out_path_src)boot.o

$(out_path_src)%.o: %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c $*.c -o $(out_path_src)$*.o

$(out_path_src)%.obj: %.asm Makefile
	nasm -f elf $*.asm -o $(out_path_src)$*.obj
