include ../cdef.mk
out_path_src = ../obj/
incpath_src = ../include/
obj_net = $(out_path_src)etherframe.o $(out_path_src)arp.o $(out_path_src)ipv4.o $(out_path_src)icmp.o $(out_path_src)udp.o $(out_path_src)dhcp.o \
          $(out_path_src)tcp.o $(out_path_src)dns.o $(out_path_src)socket.o $(out_path_src)http.o $(out_path_src)net_syscall.o $(out_path_src)ntp.o \
		  $(out_path_src)ftp.o
default : $(obj_net)
$(out_path_src)%.o : ./%.c Makefile
	gcc $(KERNEL_COMMON_CFLAGS) -O0 -w $(KERNEL_PACKED_BITFIELD_FLAGS) -c ./$*.c -o $(out_path_src)$*.o
