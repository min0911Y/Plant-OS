include ../../cdef.mk

out_path_src := ../../obj/
incpath_src := ../../include/

pc_objects := $(out_path_src)acpi.o $(out_path_src)beep.o \
	$(out_path_src)cmos.o $(out_path_src)dma.o $(out_path_src)floppy.o \
	$(out_path_src)init_platform.o \
	$(out_path_src)keyboard.o \
	$(out_path_src)mouse.o $(out_path_src)pci.o $(out_path_src)rtc.o \
	$(out_path_src)sb16.o $(out_path_src)serial.o $(out_path_src)timer.o \
	$(out_path_src)vga.o $(out_path_src)vbe.o

-include $(call kernel_depfiles,$(pc_objects))

default: $(pc_objects)

$(out_path_src)%.o: %.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) \
		-c $*.c -o $(out_path_src)$*.o

$(out_path_src)init_platform.o: init.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall -Wextra -Werror \
		$(KERNEL_PACKED_BITFIELD_FLAGS) -c $< -o $@
