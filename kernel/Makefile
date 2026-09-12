ARCH ?= i386
ifeq ($(ARCH),x86_64)
include arch/x86/x86_64/build.mk
else
FONTPATH = ../font/
APPSPATH = ../apps/
APPSOUTPATH = ../apps/out/
BIDPATH = ../apps/build_tools_in_pdos/
FATTOOLSPATH = ../fattools/
BOOTPATH = ./boot/
DOSPATH = ./dos/
CMDPATH = ./cmd/
DRIVERSPATH = ./drivers/
IOPATH = ./io/
STDPATH = ./std/
FSPATH = ./fs/
NETPATH = ./net/
MSTPATH = ./mst/
PLATFORM ?= pc
ifeq ($(PLATFORM),pc)
PLATFORMPATH = ./platform/pc/
else
$(error unsupported PLATFORM '$(PLATFORM)'; supported platforms: pc)
endif
RESPATH = ./res/
INCPATH  = ./include/
OUTPATH = ./obj/
IMGPATH = ./img/
LOADERPATH = ../loader/out/
ARCH ?= i386
ifeq ($(ARCH),i386)
ARCHPATH = ./arch/x86/i386/
ARCH_LD_EMULATION = elf_i386
QEMU_SYSTEM = qemu-system-i386
else
$(error unsupported ARCH '$(ARCH)'; supported architectures: i386 x86_64)
endif
QEMU_CPUS ?= 4
LIVECD_ISO = plant-os-livecd.iso
LIBALLOC_RELEASE = $(DOSPATH)mm/third_party/liballoc/liballoc-i686.a
LIBALLOC_KERNEL = $(OUTPATH)liballoc-kernel.a

MAKE     = make -r
NASM     = nasm
RULEFILE = make.rul
COPY     = copy
DEL      = rm

OBJS_BOOTPACK = $(OUTPATH)boot_entry.obj $(OUTPATH)boot.o \
				$(OUTPATH)kernelc.o $(OUTPATH)screen.o $(OUTPATH)command.o $(OUTPATH)timer.o \
				$(OUTPATH)memory.o $(OUTPATH)heap.o $(LIBALLOC_KERNEL) $(OUTPATH)user_vm.o $(OUTPATH)user_vm_file.o $(OUTPATH)file.o $(OUTPATH)bios_interrupt.obj \
				$(OUTPATH)interrupt_entries.obj $(OUTPATH)exceptions.obj $(OUTPATH)vga.o $(OUTPATH)tm.o  \
				$(OUTPATH)mouse.o $(OUTPATH)acpi.o $(OUTPATH)is.o $(OUTPATH)descriptor_tables.o $(OUTPATH)arg.o \
				$(OUTPATH)other.o $(OUTPATH)floppy.o $(OUTPATH)dma.o $(OUTPATH)init.o $(OUTPATH)main.o \
				$(OUTPATH)init_platform.o \
				$(OUTPATH)usb.o $(OUTPATH)xhci.o $(OUTPATH)usb_hub.o $(OUTPATH)usb_hid.o $(OUTPATH)usb_storage.o $(OUTPATH)input_device.o \
				$(OUTPATH)std.o $(OUTPATH)format.o $(OUTPATH)sheet.o $(OUTPATH)vbe.o $(OUTPATH)cmos.o $(OUTPATH)pci.o $(OUTPATH)fat.o \
				$(OUTPATH)beep.o $(OUTPATH)syscall.o $(OUTPATH)fifo.o $(OUTPATH)list.o $(OUTPATH)input.o $(OUTPATH)keyboard.o \
				$(OUTPATH)pcnet.o $(OUTPATH)ipc.o $(OUTPATH)futex.o $(OUTPATH)tty.o $(OUTPATH)page.o $(OUTPATH)serial.o $(OUTPATH)log.o \
				$(OUTPATH)path.o $(OUTPATH)draw.o $(OUTPATH)httm.o $(OUTPATH)interrupt_controller.o $(OUTPATH)x86_clock.o $(OUTPATH)bmp.o \
				$(OUTPATH)lock.o $(OUTPATH)executable.o $(OUTPATH)md5.o $(OUTPATH)libcpp.o $(OUTPATH)rand.o \
				$(OUTPATH)time.o $(OUTPATH)net_stack.o $(OUTPATH)socket.o $(OUTPATH)lwip_port.o $(OUTPATH)lwip_core_init.o $(OUTPATH)lwip_core_def.o \
				$(OUTPATH)lwip_core_dns.o $(OUTPATH)lwip_core_inet_chksum.o $(OUTPATH)lwip_core_ip.o $(OUTPATH)lwip_core_mem.o \
				$(OUTPATH)lwip_core_memp.o $(OUTPATH)lwip_core_netif.o $(OUTPATH)lwip_core_pbuf.o $(OUTPATH)lwip_core_raw.o \
				$(OUTPATH)lwip_core_sys.o $(OUTPATH)lwip_core_tcp.o $(OUTPATH)lwip_core_tcp_in.o $(OUTPATH)lwip_core_tcp_out.o \
				$(OUTPATH)lwip_core_timeouts.o $(OUTPATH)lwip_core_udp.o $(OUTPATH)lwip_ipv4_dhcp.o $(OUTPATH)lwip_ipv4_etharp.o \
				$(OUTPATH)lwip_ipv4_icmp.o $(OUTPATH)lwip_ipv4_ip4.o $(OUTPATH)lwip_ipv4_ip4_addr.o $(OUTPATH)lwip_ipv4_ip4_frag.o \
				$(OUTPATH)lwip_netif_ethernet.o $(OUTPATH)bit.o $(OUTPATH)linux_style_file.o $(OUTPATH)ide.o \
				$(OUTPATH)vdisk.o $(OUTPATH)rootfs.o $(OUTPATH)interrupt.o $(OUTPATH)mount.o \
				$(OUTPATH)mtask.o $(OUTPATH)user_thread.o $(OUTPATH)basetask.o $(OUTPATH)execute.o $(OUTPATH)network.o $(OUTPATH)rtl8139.o \
				$(OUTPATH)vfs.o $(OUTPATH)build.o $(OUTPATH)mst.o $(OUTPATH)str.o $(OUTPATH)pfs.o $(OUTPATH)signal.o \
				$(OUTPATH)rtc.o $(OUTPATH)ahci.o $(OUTPATH)initramfs.o $(OUTPATH)fartty.o $(OUTPATH)iso9660.o \
				$(OUTPATH)module.o $(OUTPATH)kasan.o $(OUTPATH)perf.o $(OUTPATH)fpu.o $(OUTPATH)irq.o $(OUTPATH)exceptions.o \
				$(OUTPATH)cpu.o $(OUTPATH)smp.o $(OUTPATH)task_context.o $(OUTPATH)smp_trampoline.obj \
				$(OUTPATH)syscall_adapter.o $(OUTPATH)x86_irq_dispatch.o \
				$(OUTPATH)task_context.obj \
				$(OUTPATH)user_return.obj

# 默认动作
default : make_dir
	$(MAKE) -C $(ARCHPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(PLATFORMPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) -C modules
	$(MAKE) Mimg

.PHONY: livecd livecd_run

livecd: default
	../scripts/build-livecd.sh $(LIVECD_ISO)

livecd_run: livecd
	$(QEMU_SYSTEM) -enable-kvm -cpu host -smp $(QEMU_CPUS) \
		-netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio \
		-drive file=./boot.img,format=raw,if=ide \
		-cdrom ./$(LIVECD_ISO) -boot d -m 4096

# 镜像文件生成
$(OUTPATH)boot.bin : $(BOOTPATH)boot.asm Makefile
	$(NASM) $(BOOTPATH)boot.asm -o $(OUTPATH)boot.bin
$(OUTPATH)boot_pfs.bin : $(BOOTPATH)boot_pfs.asm Makefile
	$(NASM) $(BOOTPATH)boot_pfs.asm -o $(OUTPATH)boot_pfs.bin
$(OUTPATH)boot32.bin : $(BOOTPATH)boot32.asm Makefile
	$(NASM) $(BOOTPATH)boot32.asm -o $(OUTPATH)boot32.bin

$(LIBALLOC_KERNEL) : $(LIBALLOC_RELEASE) Makefile
	objcopy --redefine-sym malloc=liballoc_malloc \
		--redefine-sym free=liballoc_free \
		--redefine-sym realloc=liballoc_realloc \
		--redefine-sym calloc=liballoc_calloc \
		--redefine-sym aligned_alloc=liballoc_aligned_alloc \
		--redefine-sym posix_memalign=liballoc_posix_memalign \
		--redefine-sym usable_size=liballoc_usable_size \
		--redefine-sym heap_init=liballoc_heap_init \
		--redefine-sym heap_extend=liballoc_heap_extend \
		--redefine-sym heap_onerror=liballoc_heap_onerror \
		--redefine-sym heap_set_oom_handler=liballoc_heap_set_oom_handler \
		$(LIBALLOC_RELEASE) $(LIBALLOC_KERNEL)

$(OUTPATH)kernel.bin : $(OBJS_BOOTPACK) $(ARCHPATH)kernel.ld Makefile
	ld -m $(ARCH_LD_EMULATION) -static -T $(ARCHPATH)kernel.ld $(OBJS_BOOTPACK) ./libubscan.a ../apps/libs/libtcc1.a -o $(OUTPATH)kernel.bin

make_dir:
	mkdir -p img
	mkdir -p obj

$(OUTPATH)irq.o: $(ARCHPATH)irq.c $(INCPATH)irq.h $(ARCHPATH)Makefile
	$(MAKE) -C $(ARCHPATH) ARCH=$(ARCH)

make_img: make_dir
	qemu-img create boot.img 64M
	qemu-img create $(IMGPATH)Disk2.img 1474560
	qemu-img create $(IMGPATH)Disk3.img 1474560
	qemu-img create $(IMGPATH)res.img 1474560
	qemu-img create $(IMGPATH)tcc.img 1474560
	qemu-img create $(IMGPATH)nasm.img 1474560
	qemu-img create $(IMGPATH)ndisasm.img 1474560
	qemu-img create $(IMGPATH)doom1.img 1474560
	qemu-img create $(IMGPATH)doom2.img 1474560
	qemu-img create disk.img 128M
	qemu-img create big_disk.img 128M
# 	mformat -f 1440 -B $(OUTPATH)boot.bin -i boot.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)Disk2.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)Disk3.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)res.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)tcc.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)nasm.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)ndisasm.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)doom1.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)doom2.img
	mformat -t 64 -h 64 -s 16 -B $(OUTPATH)boot.bin -i ./disk.img
	mformat -t 64 -h 64 -s 16 -B $(OUTPATH)boot.bin -i ./boot.img
	mformat -t 64 -h 64 -i ./big_disk.img
Mimg : make_dir $(OUTPATH)kernel.bin $(OUTPATH)boot.bin $(OUTPATH)boot_pfs.bin $(OUTPATH)boot32.bin Makefile
	$(MAKE) make_img
		mcopy -i ./disk.img $(LOADERPATH)dosldr.bin ::
		mcopy -i ./disk.img $(OUTPATH)kernel.bin ::
		mcopy -i ./disk.img $(FONTPATH)font.bin ::
		mcopy -i ./disk.img $(FONTPATH)HZK16 ::
		mcopy -i ./disk.img $(BIDPATH)nasm.zip ::
		mcopy -i ./disk.img $(APPSOUTPATH)duktape.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)doom.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)ttf.bin ::
		mcopy -i ./disk.img $(RESPATH)doom1.wad ::
		mcopy -i ./disk.img $(RESPATH)font.ttf ::
		mcopy -i ./disk.img $(RESPATH)123.png ::
		mcopy -i ./disk.img $(RESPATH)g.nes ::
		mcopy -i ./disk.img $(APPSOUTPATH)gui.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)term.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)rendertm.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)renderhd.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)editor.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)psh.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)nasm.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)ndisasm.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)bitz.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)lite.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)nk.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)init.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)hexview.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)kcube.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)invader.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)chat.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)netgobang.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)minewep.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)copydir.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)dktest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)rpctest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)dynmain.bin $(APPSOUTPATH)dyncopy.bin $(APPSOUTPATH)dynbad.bin $(APPSOUTPATH)dynempty.bin $(APPSOUTPATH)dyntest.bin ::
		mcopy -s -i ./disk.img $(APPSOUTPATH)lib ::/
		mcopy -i ./disk.img $(APPSOUTPATH)guitest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)timetest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)futest.bin $(APPSOUTPATH)schbench.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)thrdtest.bin $(APPSOUTPATH)libctest.bin $(APPSOUTPATH)cxxcheck.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)sdltest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)exc_test.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)fputest.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)lua.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)ping.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)curl.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)ps.bin ::

		mcopy -i ./disk.img $(RESPATH)init.mst ::

		mmd -i ./disk.img data
		mmd -i ./disk.img data/core
		mmd -i ./disk.img data/fonts
		mmd -i ./disk.img data/plugins
		mmd -i ./disk.img data/user
		mmd -i ./disk.img modules
		mmd -i ./disk.img pngs
		mcopy -i ./disk.img ./iso/modules/* ::/modules
		mcopy -i ./disk.img ../apps/lite-1.11/data/core/* ::/data/core
		mcopy -i ./disk.img ../apps/lite-1.11/data/fonts/* ::/data/fonts
		mcopy -i ./disk.img ../apps/lite-1.11/data/plugins/* ::/data/plugins
		mcopy -i ./disk.img ../apps/lite-1.11/data/user/* ::/data/user
#		mmd -i ./disk.img stamon
#		mmd -i ./disk.img stamon/bin
#		mmd -i ./disk.img stamon/bin/include
#		mmd -i ./disk.img stamon/demos
#		mcopy -i ./disk.img $(APPSOUTPATH)stamon.bin ::/stamon/bin
#		mcopy -i ./disk.img ../apps/stamon/bin/include/* ::/stamon/bin/include
#		mcopy -i ./disk.img ../apps/stamon/demos/* ::/stamon/demos


		mcopy -i boot.img $(LOADERPATH)dosldr.bin ::
		mcopy -i boot.img $(OUTPATH)kernel.bin ::
		mcopy -i boot.img $(OUTPATH)hello.mod ::
		mcopy -i boot.img $(RESPATH)autoexec.bat ::
		mcopy -i boot.img $(RESPATH)env.cfg ::
		mcopy -i boot.img $(RESPATH)sys.cfg ::
		mcopy -i boot.img $(RESPATH)init.mst ::
		mcopy -i boot.img $(OUTPATH)boot.bin ::
		mcopy -i boot.img $(OUTPATH)boot32.bin ::
		mcopy -i boot.img $(OUTPATH)boot_pfs.bin ::
		mcopy -i boot.img $(RESPATH)setup.mst ::
		mcopy -i boot.img $(APPSOUTPATH)setup1.bin ::
		mcopy -i boot.img $(APPSOUTPATH)init.bin ::
		mcopy -i boot.img $(APPSOUTPATH)psh.bin ::
		mcopy -i boot.img $(APPSOUTPATH)ps.bin ::
		mcopy -i boot.img $(APPSOUTPATH)my_basic.bin ::
		mcopy -i boot.img $(APPSOUTPATH)copydir.bin ::
		mcopy -i boot.img $(APPSOUTPATH)nettest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)usbtest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)ping.bin ::
		mcopy -i boot.img $(APPSOUTPATH)curl.bin ::
		mcopy -i boot.img $(APPSOUTPATH)dktest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)rpctest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)dynmain.bin $(APPSOUTPATH)dyncopy.bin $(APPSOUTPATH)dynbad.bin $(APPSOUTPATH)dynempty.bin $(APPSOUTPATH)dyntest.bin ::
		mcopy -s -i boot.img $(APPSOUTPATH)lib ::/
		mcopy -i boot.img $(APPSOUTPATH)guitest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)timetest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)futest.bin $(APPSOUTPATH)schbench.bin ::
		mcopy -i boot.img $(APPSOUTPATH)thrdtest.bin $(APPSOUTPATH)libctest.bin $(APPSOUTPATH)cxxcheck.bin ::
		mcopy -i boot.img $(APPSOUTPATH)sdltest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)exc_test.bin ::
		mcopy -i boot.img $(APPSOUTPATH)fputest.bin ::
		mcopy -i boot.img $(FONTPATH)font.bin ::
		mcopy -i boot.img $(FONTPATH)HZK16 ::
		mcopy -i boot.img $(BIDPATH)nasm.zip ::
		mcopy -i boot.img $(APPSOUTPATH)duktape.bin ::
		mcopy -i boot.img $(APPSOUTPATH)doom.bin ::
		mcopy -i boot.img $(APPSOUTPATH)ttf.bin ::
		mcopy -i boot.img $(RESPATH)doom1.wad ::
		mcopy -i boot.img $(RESPATH)font.ttf ::
		mcopy -i boot.img $(RESPATH)123.png ::
		mcopy -i boot.img $(RESPATH)g.nes ::
		mcopy -i boot.img $(APPSOUTPATH)gui.bin ::
		mcopy -i boot.img $(APPSOUTPATH)term.bin ::
		mcopy -i boot.img $(APPSOUTPATH)rendertm.bin ::
		mcopy -i boot.img $(APPSOUTPATH)renderhd.bin ::
		mcopy -i boot.img $(APPSOUTPATH)editor.bin ::


		mmd -i boot.img other
		mcopy -i boot.img $(FONTPATH)font.bin ::/other
		mcopy -i boot.img $(FONTPATH)HZK16 ::/other
		mcopy -i boot.img $(APPSOUTPATH)copy.bin ::/other
		mcopy -i boot.img $(APPSOUTPATH)uname.bin ::/other
		mcopy -i boot.img $(RESPATH)coin.wav ::/other


# 运行程序

img_run :
	$(MAKE) -C $(ARCHPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(PLATFORMPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) -C modules
	$(MAKE) Mimg
	$(QEMU_SYSTEM) -enable-kvm -cpu host -smp $(QEMU_CPUS) -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device floppy -hda ./boot.img  \
	 -hdb disk.img \
	 -boot c -m 4096
full : make_dir
	$(MAKE) -C $(ARCHPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(PLATFORMPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(APPSPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) Mimg
run:
	$(QEMU_SYSTEM) -d in_asm -no-reboot -smp $(QEMU_CPUS) -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device floppy -fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb ./img/Disk2.img -hdc ./img/Disk3.img -hdd ./img/res.img -boot a -m 512
full_run:
	$(MAKE) -C $(ARCHPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(PLATFORMPATH) ARCH=$(ARCH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(APPSPATH)
	$(MAKE) Mimg
	$(QEMU_SYSTEM) -smp $(QEMU_CPUS) -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device floppy -fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb ./img/Disk2.img -hdc ./img/Disk3.img -hdd ./img/res.img -boot a -m 512 -enable-kvm -cpu host
clean :
	rm -r ./obj
	rm -r ./img
	rm -f ./$(LIVECD_ISO)
	$(MAKE) -C $(APPSPATH) clean
endif
