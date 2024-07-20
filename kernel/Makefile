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
LOADERPATH = ../loader/out/

MAKE     = make -r
NASM     = nasm
RULEFILE = make.rul
COPY     = copy
DEL      = rm

QEMU_ARGS = -net nic,model=pcnet -net user -serial stdio -device sb16 -device floppy

make_dir:
	mkdir -p img

make_img: make_dir
	qemu-img create $(IMGPATH)Powerint_DOS_386.img 1474560
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
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)Powerint_DOS_386.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)Disk2.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)Disk3.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)res.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)tcc.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)nasm.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)ndisasm.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)doom1.img
	mformat -f 1440 -B $(OUTPATH)boot.bin -i $(IMGPATH)doom2.img
	mformat -t 64 -h 64 -s 16 -B $(OUTPATH)boot.bin -i ./disk.img
	mformat -t 64 -h 64 -i ./big_disk.img
Mimg : make_img
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
# mcopy -i ./disk.img $(APPSOUTPATH)plui.bin ::
	mcopy -i ./disk.img $(APPSOUTPATH)bfpp.bin ::
	mcopy -i ./disk.img $(RESPATH)init.mst ::

	mmd -i ./disk.img data
	mmd -i ./disk.img data/core
	mmd -i ./disk.img data/fonts
	mmd -i ./disk.img data/plugins
	mmd -i ./disk.img data/user
	mmd -i ./disk.img pngs
	mcopy -i ./disk.img ../apps/lite-1.11/data/core/* ::/data/core
	mcopy -i ./disk.img ../apps/lite-1.11/data/fonts/* ::/data/fonts
	mcopy -i ./disk.img ../apps/lite-1.11/data/plugins/* ::/data/plugins
	mcopy -i ./disk.img ../apps/lite-1.11/data/user/* ::/data/user
	mcopy -i ./disk.img ../apps/minewep/pngs/* ::/pngs
#		mmd -i ./disk.img stamon
#		mmd -i ./disk.img stamon/bin
#		mmd -i ./disk.img stamon/bin/include
#		mmd -i ./disk.img stamon/demos
#		mcopy -i ./disk.img $(APPSOUTPATH)stamon.bin ::/stamon/bin
#		mcopy -i ./disk.img ../apps/stamon/bin/include/* ::/stamon/bin/include
#		mcopy -i ./disk.img ../apps/stamon/demos/* ::/stamon/demos

	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(LOADERPATH)dosldr.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(OUTPATH)kernel.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)autoexec.bat ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)env.cfg ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)sys.cfg ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)init.mst ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(OUTPATH)boot.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(OUTPATH)boot32.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(OUTPATH)boot_pfs.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)setup.mst ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(APPSOUTPATH)setup1.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(APPSOUTPATH)init.bin ::
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(APPSOUTPATH)psh.bin ::
	mmd -i $(IMGPATH)Powerint_DOS_386.img other
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(FONTPATH)font.bin ::/other
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(FONTPATH)HZK16 ::/other
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(APPSOUTPATH)copy.bin ::/other
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(APPSOUTPATH)uname.bin ::/other
	mcopy -i $(IMGPATH)Powerint_DOS_386.img $(RESPATH)coin.wav ::/other


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
	mcopy -i $(IMGPATH)tcc.img $(APPSPATH)lib/libp.a ::/tcc/lib
	mcopy -i $(IMGPATH)tcc.img $(APPSPATH)lib/libtcc1.a ::/tcc/inst
	mcopy -i $(IMGPATH)tcc.img $(APPSPATH)lib/libabi.a ::/tcc/lib
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

img_run:
	sudo qemu-system-i386 -enable-kvm $(QEMU_ARGS) \
		-fda ./img/Powerint_DOS_386.img \
		-hda ./img/Disk2.img \
		-hdb ./img/Disk3.img \
		-hdc ./img/res.img \
		-hdd disk.img \
		-boot a -m 4096
run:
	qemu-system-i386 -d in_asm -no-reboot $(QEMU_ARGS) \
		-fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb ./img/Disk2.img -hdc ./img/Disk3.img -hdd ./img/res.img -boot a -m 512
clean:
	rm -r ./img
	$(MAKE) -C $(APPSPATH) clean
