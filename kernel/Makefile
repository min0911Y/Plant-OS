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
RESPATH = ./res/
INCPATH  = ./include/
OUTPATH = ./obj/
IMGPATH = ./img/
LOADERPATH = ../Loader/out/
ARCHX86PATH = ./arch/x86/

MAKE     = make -r
NASM     = nasm
RULEFILE = make.rul
COPY     = copy
DEL      = rm

OBJS_BOOTPACK = $(OUTPATH)kernelc.o $(OUTPATH)screen.o $(OUTPATH)command.o $(OUTPATH)timer.o \
				$(OUTPATH)mem.o $(OUTPATH)file.o $(OUTPATH)bios_interrupt.obj \
				$(OUTPATH)interrupt_entries.obj $(OUTPATH)exceptions.obj $(OUTPATH)vga.o $(OUTPATH)harddisk.o $(OUTPATH)TextMode.o  \
				$(OUTPATH)mouse.o $(OUTPATH)acpi.o $(OUTPATH)Input_Stack.o $(OUTPATH)descriptor_tables.o $(OUTPATH)arg.o \
				$(OUTPATH)other.o $(OUTPATH)floppy.o $(OUTPATH)dma.o $(OUTPATH)init.o $(OUTPATH)main.o \
				$(OUTPATH)std.o $(OUTPATH)sheet.o $(OUTPATH)vbe.o $(OUTPATH)cmos.o $(OUTPATH)pci.o $(OUTPATH)fat.o \
				$(OUTPATH)beep.o $(OUTPATH)syscall.o $(OUTPATH)fifo.o $(OUTPATH)list.o $(OUTPATH)input.o $(OUTPATH)keyboard.o \
				$(OUTPATH)pcnet.o $(OUTPATH)ipc.o $(OUTPATH)tty.o $(OUTPATH)page.o $(OUTPATH)serial.o $(OUTPATH)log.o \
				$(OUTPATH)path.o $(OUTPATH)Draw.o $(OUTPATH)HighTextMode.o $(OUTPATH)pic.o $(OUTPATH)bmp.o $(OUTPATH)sb16.o \
				$(OUTPATH)lock.o $(OUTPATH)elf.o $(OUTPATH)driver.o $(OUTPATH)md5.o $(OUTPATH)libcpp.o $(OUTPATH)rand.o \
				$(OUTPATH)time.o $(OUTPATH)net_stack.o $(OUTPATH)lwip_port.o $(OUTPATH)lwip_core_init.o $(OUTPATH)lwip_core_def.o \
				$(OUTPATH)lwip_core_dns.o $(OUTPATH)lwip_core_inet_chksum.o $(OUTPATH)lwip_core_ip.o $(OUTPATH)lwip_core_mem.o \
				$(OUTPATH)lwip_core_memp.o $(OUTPATH)lwip_core_netif.o $(OUTPATH)lwip_core_pbuf.o $(OUTPATH)lwip_core_raw.o \
				$(OUTPATH)lwip_core_sys.o $(OUTPATH)lwip_core_tcp.o $(OUTPATH)lwip_core_tcp_in.o $(OUTPATH)lwip_core_tcp_out.o \
				$(OUTPATH)lwip_core_timeouts.o $(OUTPATH)lwip_core_udp.o $(OUTPATH)lwip_ipv4_dhcp.o $(OUTPATH)lwip_ipv4_etharp.o \
				$(OUTPATH)lwip_ipv4_icmp.o $(OUTPATH)lwip_ipv4_ip4.o $(OUTPATH)lwip_ipv4_ip4_addr.o $(OUTPATH)lwip_ipv4_ip4_frag.o \
				$(OUTPATH)lwip_netif_ethernet.o $(OUTPATH)bit.o $(OUTPATH)linux_style_file.o $(OUTPATH)ide.o \
				$(OUTPATH)vdisk.o $(OUTPATH)rootfs.o $(OUTPATH)mount.o \
				$(OUTPATH)mtask.o $(OUTPATH)basetask.o $(OUTPATH)execute.o $(OUTPATH)network.o $(OUTPATH)rtl8139.o \
				$(OUTPATH)vfs.o $(OUTPATH)build.o $(OUTPATH)mst.o $(OUTPATH)str.o $(OUTPATH)pfs.o $(OUTPATH)signal.o \
				$(OUTPATH)rtc.o $(OUTPATH)ahci.o $(OUTPATH)farcall.o $(OUTPATH)fartty.o $(OUTPATH)iso9660.o \
				$(OUTPATH)module.o $(OUTPATH)kasan.o $(OUTPATH)perf.o $(OUTPATH)irq.o $(OUTPATH)exceptions.o \
				$(OUTPATH)task_context.o $(OUTPATH)task_context.obj $(OUTPATH)user_return.obj

# 默认动作
default : make_dir
	$(MAKE) -C $(ARCHX86PATH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) -C modules
	$(MAKE) Mimg

# 镜像文件生成
$(OUTPATH)boot.bin : $(BOOTPATH)boot.asm Makefile
	$(NASM) $(BOOTPATH)boot.asm -o $(OUTPATH)boot.bin
$(OUTPATH)boot_pfs.bin : $(BOOTPATH)boot_pfs.asm Makefile
	$(NASM) $(BOOTPATH)boot_pfs.asm -o $(OUTPATH)boot_pfs.bin
$(OUTPATH)boot32.bin : $(BOOTPATH)boot32.asm Makefile
	$(NASM) $(BOOTPATH)boot32.asm -o $(OUTPATH)boot32.bin

$(OUTPATH)kernel.bin : $(OBJS_BOOTPACK) Makefile
	ld -m elf_i386 -static -Ttext 0x280000 -e KernelMain $(OBJS_BOOTPACK) ./libubscan.a ../apps/libs/libtcc1.a -o $(OUTPATH)kernel.bin

make_dir:
	mkdir -p img
	mkdir -p obj

$(OUTPATH)irq.o: $(ARCHX86PATH)irq.c $(INCPATH)irq.h $(ARCHX86PATH)Makefile
	$(MAKE) -C $(ARCHX86PATH)

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
		mcopy -i ./disk.img $(APPSOUTPATH)exc_test.bin ::
		mcopy -i ./disk.img $(APPSOUTPATH)lua.bin ::

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
		mcopy -i boot.img $(APPSOUTPATH)my_basic.bin ::
		mcopy -i boot.img $(APPSOUTPATH)copydir.bin ::
		mcopy -i boot.img $(APPSOUTPATH)nettest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)dktest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)rpctest.bin ::
		mcopy -i boot.img $(APPSOUTPATH)exc_test.bin ::
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


		mmd -i boot.img other
		mcopy -i boot.img $(FONTPATH)font.bin ::/other
		mcopy -i boot.img $(FONTPATH)HZK16 ::/other
		mcopy -i boot.img $(APPSOUTPATH)copy.bin ::/other
		mcopy -i boot.img $(APPSOUTPATH)uname.bin ::/other
		mcopy -i boot.img $(RESPATH)coin.wav ::/other


		mmd -i $(IMGPATH)Disk2.img bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)aigobang.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)lua.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)snake.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)randnum.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)bf.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)copy.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)basic.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)cal.bin ::/bin

		mcopy -i $(IMGPATH)Disk2.img $(APPSOUTPATH)editor.bin ::/bin
		mcopy -i $(IMGPATH)Disk2.img $(RESPATH)editor.mst ::
		

		mmd -i $(IMGPATH)Disk3.img bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)minizip.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)miniunz.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)image.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)c4.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)clock.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)luac.bin ::/bin
		mcopy -i $(IMGPATH)Disk3.img $(APPSOUTPATH)mmake.bin ::/bin
		
		mmd -i $(IMGPATH)res.img res
		mcopy -i $(IMGPATH)res.img $(RESPATH)hello.nas ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)coin.wav ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)test.wav ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)desktop.jpg ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)huaji.jpg ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)load.jpg ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)io.bas ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)th.svg ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)shot1.png ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)test.htm ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)test1.htm ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)yee.bmp ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)fib.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)guess.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)gobang.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)bainian.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)student.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)json.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)asm.lua ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)chinese.txt ::/res
		mcopy -i $(IMGPATH)res.img $(RESPATH)bf.txt ::/res
		mcopy -i $(IMGPATH)res.img ../apps/c4/c4.c ::/res
		
		mmd -i $(IMGPATH)tcc.img tcc
		mmd -i $(IMGPATH)tcc.img tcc/lib
		mmd -i $(IMGPATH)tcc.img tcc/crt
		mmd -i $(IMGPATH)tcc.img tcc/include
		mmd -i $(IMGPATH)tcc.img tcc/inst
		mcopy -i $(IMGPATH)tcc.img $(APPSOUTPATH)tcc.bin ::
		mcopy -i $(IMGPATH)tcc.img $(APPSOUTPATH)tccinst.bin ::
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)tcc/tcc/crti.c ::
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)libs/libp.a ::/tcc/lib
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)libs/libtcc1.a ::/tcc/inst
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)libs/libabi.a ::/tcc/lib
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/arg.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/assert.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/ctype.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/ctypes.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/errno.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/fcntl.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/features.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/inttypes.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/limits.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/locale.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/math.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/net.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/rand.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/setjmp.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stdarg.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stdbool.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stddef.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stdint.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stdio.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/stdlib.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/string.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/syscall.h ::/tcc/include
		mcopy -i $(IMGPATH)tcc.img $(APPSPATH)include/time.h ::/tcc/include

		mcopy -i $(IMGPATH)nasm.img $(BIDPATH)nasm.zip ::
		mcopy -i $(IMGPATH)doom1.img $(RESPATH)001doom.bin ::
		mcopy -i $(IMGPATH)doom1.img $(APPSOUTPATH)doomcpy.bin ::
		mcopy -i $(IMGPATH)doom2.img $(RESPATH)002doom.bin ::
# 运行程序

img_run :
	$(MAKE) -C $(ARCHX86PATH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) -C modules
	$(MAKE) Mimg
	qemu-system-i386 -enable-kvm -cpu host -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device sb16 -device floppy -hda ./boot.img  \
	 -hdb disk.img \
	 -boot c -m 4096
full : make_dir
	$(MAKE) -C $(ARCHX86PATH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(APPSPATH)
	$(MAKE) -C $(MSTPATH)
	$(MAKE) Mimg
run:
	qemu-system-i386 -d in_asm -no-reboot -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device sb16 -device floppy -fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb ./img/Disk2.img -hdc ./img/Disk3.img -hdd ./img/res.img -boot a -m 512
full_run:
	$(MAKE) -C $(ARCHX86PATH)
	$(MAKE) -C $(DOSPATH)
	$(MAKE) -C $(IOPATH)
	$(MAKE) -C $(FSPATH)
	$(MAKE) -C $(DRIVERSPATH)
	$(MAKE) -C $(STDPATH)
	$(MAKE) -C $(CMDPATH)
	$(MAKE) -C $(NETPATH)
	$(MAKE) -C $(APPSPATH)
	$(MAKE) Mimg
	qemu-system-i386 -netdev user,id=net0 -device pcnet,netdev=net0 -serial stdio -device sb16 -device floppy -fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb ./img/Disk2.img -hdc ./img/Disk3.img -hdd ./img/res.img -boot a -m 512 -enable-kvm -cpu host
clean :
	rm -r ./obj
	rm -r ./img
	$(MAKE) -C $(APPSPATH) clean
