# Shared runtime, standalone interpreter and ELF integration fixtures. Included
# from the architecture build, also usable as make -C apps -f dynamic.mk.
ifeq ($(.DEFAULT_GOAL),)
.DEFAULT_GOAL := dynamic
endif
ARCH ?= i386
DYN_OUT := out$(if $(filter x86_64,$(ARCH)),/x86_64)
DYN_BUILD := $(DYN_OUT)/dynamic
DYN_LIB := $(DYN_OUT)/lib
ifeq ($(ARCH),i386)
DYN_ARCH := -m32 -march=pentium -mno-mmx -mno-sse -mno-sse2 -mfpmath=387 -DPLANT_ARCH_I386
DYN_EMULATION := elf_i386
DYN_FORMAT := elf32
DYN_BASE := 0x70000000
else ifeq ($(ARCH),x86_64)
DYN_ARCH := -m64 -mcmodel=small -mno-red-zone -mno-mmx -msse2 -mfpmath=sse -mlong-double-64 -DPLANT_ARCH_X86_64
DYN_EMULATION := elf_x86_64
DYN_FORMAT := elf64
DYN_BASE := 0x100000000
else
$(error unsupported dynamic linker architecture '$(ARCH)')
endif
DYN_CFLAGS := $(DYN_ARCH) -std=gnu17 -Iinclude -nostdinc -isystem $(shell gcc -print-file-name=include) \
  -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
  -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -MMD -MP \
  -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration
DYN_LDFLAGS := -m $(DYN_EMULATION) -z max-page-size=4096 -z noexecstack -z relro -z now -z text
DYN_SOURCES := $(filter-out libp/entry.c libp/tinyalloc.c,$(wildcard libp/*.c)) libp/arch/$(ARCH)/math.c
DYN_OBJECTS := $(patsubst %.c,$(DYN_BUILD)/%.o,$(DYN_SOURCES)) \
  $(DYN_BUILD)/libp/arch/$(ARCH)/syscall.obj
DYN_BUILTINS := $(if $(filter i386,$(ARCH)),$(DYN_BUILD)/libtcc1.a)

$(DYN_BUILD)/libp/arch/$(ARCH)/syscall.obj: libp/arch/syscalls.inc

.PHONY: dynamic
DYN_PROGRAMS := dynmain dyncopy dynbad dynempty dyntest
DYN_TARGETS := $(addprefix $(DYN_OUT)/,$(addsuffix .bin,$(DYN_PROGRAMS)))
dynamic: $(DYN_LIB)/ld.so $(DYN_LIB)/libp.so $(DYN_LIB)/libcpp.so $(DYN_TARGETS)

$(DYN_BUILD)/%.o: %.c dynamic.mk
	@mkdir -p $(dir $@)
	gcc $(DYN_CFLAGS) -c $< -o $@
$(DYN_BUILD)/%.obj: %.asm dynamic.mk
	@mkdir -p $(dir $@)
	nasm -I$(dir $<) -f $(DYN_FORMAT) $< -o $@
$(DYN_BUILD)/%.o: %.cpp dynamic.mk
	@mkdir -p $(dir $@)
	g++ $(filter-out -std=gnu17 -Werror=implicit-function-declaration,$(DYN_CFLAGS)) \
	  -std=gnu++17 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -c $< -o $@
$(DYN_BUILD)/libp.a: $(DYN_OBJECTS)
	rm -f $@
	ar rcs $@ $^
$(DYN_BUILD)/libtcc1.a: $(DYN_BUILD)/libtcc1/libtcc1.o
	rm -f $@
	ar rcs $@ $^
$(DYN_LIB)/libp.so: $(DYN_BUILD)/libp.a $(DYN_BUILTINS)
	@mkdir -p $(dir $@)
	ld $(DYN_LDFLAGS) -shared --no-undefined --hash-style=both -soname libp.so -o $@ \
	  --whole-archive $(DYN_BUILD)/libp.a --no-whole-archive $(DYN_BUILTINS)
$(DYN_LIB)/libcpp.so: $(DYN_BUILD)/libp/cxx.o $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -shared --no-undefined --hash-style=both -soname libcpp.so -o $@ $^
$(DYN_LIB)/ld.so: $(DYN_BUILD)/ldso/object.o $(DYN_BUILD)/ldso/link.o $(DYN_BUILD)/libp.a ldso/static.ld $(DYN_BUILTINS)
	@mkdir -p $(dir $@)
	ld $(DYN_LDFLAGS) -static --gc-sections --defsym=USER_BASE=$(DYN_BASE) -T ldso/static.ld -o $@ \
	  $(filter %.o,$^) --start-group $(DYN_BUILD)/libp.a $(DYN_BUILTINS) --end-group

$(DYN_LIB)/libbase.so: $(DYN_BUILD)/dyntest/base.o $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -shared --no-undefined --hash-style=sysv -soname libbase.so -o $@ $< -L$(DYN_LIB) -lp
$(DYN_LIB)/libleaf.so: $(DYN_BUILD)/dyntest/leaf.o $(DYN_LIB)/libbase.so $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -shared --no-undefined --hash-style=gnu -soname libleaf.so -rpath '$$ORIGIN' -o $@ $< -L$(DYN_LIB) -lbase -lp
$(DYN_OUT)/dynmain.bin: $(DYN_BUILD)/dyntest/main.o $(DYN_BUILD)/libp/entry.o $(DYN_LIB)/libleaf.so $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -pie --export-dynamic --hash-style=gnu -e Main --dynamic-linker /lib/ld.so \
	  -rpath '$$ORIGIN/lib' -o $@ $(filter %.o,$^) -L$(DYN_LIB) -lleaf -lp -rpath-link $(DYN_LIB)
$(DYN_BUILD)/dyntest/copy.o: DYN_CFLAGS += -fPIE
$(DYN_OUT)/dyncopy.bin: $(DYN_BUILD)/dyntest/copy.o $(DYN_BUILD)/libp/entry.o $(DYN_LIB)/libleaf.so $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -pie --export-dynamic -e Main --dynamic-linker /lib/ld.so \
	  -o $@ $(filter %.o,$^) -L$(DYN_LIB) -lleaf -lp -rpath-link $(DYN_LIB)
$(DYN_LIB)/libbad.so: $(DYN_BUILD)/dyntest/bad.o
	@mkdir -p $(dir $@)
	ld $(DYN_LDFLAGS) -shared --hash-style=both -soname libbad.so -o $@ $<
$(DYN_OUT)/dynbad.bin: $(DYN_BUILD)/dyntest/empty.o $(DYN_BUILD)/libp/entry.o $(DYN_LIB)/libbad.so $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -pie --allow-shlib-undefined -e Main --dynamic-linker /lib/ld.so \
	  -o $@ $(filter %.o,$^) -L$(DYN_LIB) -lbad -lp
$(DYN_OUT)/dynempty.bin: $(DYN_BUILD)/dyntest/empty.o $(DYN_BUILD)/libp/entry.o $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -pie --hash-style=gnu -e Main --dynamic-linker /lib/ld.so \
	  -o $@ $(filter %.o,$^) -L$(DYN_LIB) -lp
$(DYN_OUT)/dyntest.bin: $(DYN_BUILD)/dyntest/driver.o $(DYN_BUILD)/libp/entry.o $(DYN_LIB)/libp.so
	ld $(DYN_LDFLAGS) -pie -e Main --dynamic-linker /lib/ld.so -o $@ $^

-include $(patsubst %.o,%.d,$(filter %.o,$(DYN_OBJECTS))) $(wildcard $(DYN_BUILD)/ldso/*.d $(DYN_BUILD)/dyntest/*.d) $(DYN_BUILD)/libp/entry.d
