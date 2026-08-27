include ../cdef.mk

out_path_src := ../obj/
incpath_src := ../include/
lwip_root := third_party/lwip/src
lwip_port := lwip_port/include
lwip_cflags := -I./$(lwip_port) -I./$(lwip_root)/include \
	$(KERNEL_COMMON_CFLAGS) -O2 -w $(KERNEL_PACKED_BITFIELD_FLAGS)

lwip_core_objects := \
	$(out_path_src)lwip_core_init.o \
	$(out_path_src)lwip_core_def.o \
	$(out_path_src)lwip_core_dns.o \
	$(out_path_src)lwip_core_inet_chksum.o \
	$(out_path_src)lwip_core_ip.o \
	$(out_path_src)lwip_core_mem.o \
	$(out_path_src)lwip_core_memp.o \
	$(out_path_src)lwip_core_netif.o \
	$(out_path_src)lwip_core_pbuf.o \
	$(out_path_src)lwip_core_raw.o \
	$(out_path_src)lwip_core_sys.o \
	$(out_path_src)lwip_core_tcp.o \
	$(out_path_src)lwip_core_tcp_in.o \
	$(out_path_src)lwip_core_tcp_out.o \
	$(out_path_src)lwip_core_timeouts.o \
	$(out_path_src)lwip_core_udp.o \
	$(out_path_src)lwip_ipv4_dhcp.o \
	$(out_path_src)lwip_ipv4_etharp.o \
	$(out_path_src)lwip_ipv4_icmp.o \
	$(out_path_src)lwip_ipv4_ip4.o \
	$(out_path_src)lwip_ipv4_ip4_addr.o \
	$(out_path_src)lwip_ipv4_ip4_frag.o \
	$(out_path_src)lwip_netif_ethernet.o

obj_net := $(out_path_src)net_stack.o $(out_path_src)lwip_port.o \
	$(lwip_core_objects)

default: $(obj_net)

$(out_path_src)net_stack.o: net_stack.c Makefile
	gcc $(lwip_cflags) -c $< -o $@

$(out_path_src)lwip_port.o: lwip_port/lwip_port.c Makefile
	gcc $(lwip_cflags) -c $< -o $@

$(out_path_src)lwip_core_%.o: $(lwip_root)/core/%.c Makefile
	gcc $(lwip_cflags) -c $< -o $@

$(out_path_src)lwip_ipv4_%.o: $(lwip_root)/core/ipv4/%.c Makefile
	gcc $(lwip_cflags) -c $< -o $@

$(out_path_src)lwip_netif_%.o: $(lwip_root)/netif/%.c Makefile
	gcc $(lwip_cflags) -c $< -o $@
