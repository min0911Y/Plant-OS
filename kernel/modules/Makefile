include ../cdef.mk

OUTDIR := ../obj/modules
MODDIR := ../obj/
incpath_src := ../include

MODULE_CFLAGS := $(KERNEL_COMMON_CFLAGS) -O0 -Wall -Wno-unused-function

default: $(MODDIR)/hello.mod

dirs:
	mkdir -p $(OUTDIR)
	mkdir -p $(MODDIR)

$(OUTDIR)/hello.o: hello/hello.c Makefile | dirs
	gcc $(MODULE_CFLAGS) -c hello/hello.c -o $(OUTDIR)/hello.o

$(MODDIR)/hello.mod: $(OUTDIR)/hello.o | dirs
	ld -m elf_i386 -r $(OUTDIR)/hello.o -o $(MODDIR)/hello.mod

clean:
	rm -rf $(OUTDIR) $(MODDIR)
