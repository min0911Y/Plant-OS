include ../cdef.mk
out_path_src := ../obj/
incpath_src := ../include/
TOOLPATH := ../../tools/
MAKE     := $(TOOLPATH)make.exe -r
obj_driver = $(out_path_src)initramfs.o $(out_path_src)pcnet.o \
	$(out_path_src)ide.o $(out_path_src)vdisk.o \
	$(out_path_src)network.o $(out_path_src)rtl8139.o $(out_path_src)ahci.o \
	$(out_path_src)usb.o $(out_path_src)xhci.o $(out_path_src)usb_hub.o $(out_path_src)usb_hid.o $(out_path_src)usb_storage.o
-include $(call kernel_depfiles,$(obj_driver))
default : $(obj_driver)
$(out_path_src)%.o : ./%.c Makefile
			gcc $(KERNEL_COMMON_CFLAGS) -O3 -Wall $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
