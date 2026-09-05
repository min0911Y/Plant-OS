.DEFAULT_GOAL := default
PLATFORM ?= pc
ifneq ($(PLATFORM),pc)
$(error unsupported PLATFORM '$(PLATFORM)')
endif
ifneq ($(filter 1,$(KASAN) $(PERF)),)
$(error KASAN and PERF instrumentation currently require ARCH=i386)
endif

incpath_src := include
include cflags.def
BUILD := obj/x86_64
ARCH_DIR := arch/x86/x86_64
FLANTERM := io/third_party/flanterm/src
LWIP := net/third_party/lwip/src
CFLAGS := $(KERNEL_COMMON_CFLAGS) -O2 -Wall -Wextra \
	-ffunction-sections -fdata-sections \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-I$(ARCH_DIR)/third_party/limine -I$(FLANTERM) \
	-Inet/lwip_port/include -I$(LWIP)/include

SOURCES := $(wildcard $(ARCH_DIR)/*.c) arch/x86/common/interrupt_controller.c \
	dos/init/main.c dos/init/init.c dos/init/kernelc.c dos/mm/heap.c \
	$(wildcard dos/task/*.c dos/hal/*.c dos/tools/*.c) dos/syscall/syscall.c \
	dos/perf/perf.c dos/mm/kasan.c \
	io/tty.c io/screen.c io/input.c io/is.c io/log.c io/draw.c io/sheet.c \
	io/fartty.c io/flanterm.c $(FLANTERM)/flanterm.c $(FLANTERM)/flanterm_backends/fb.c \
	$(filter-out fs/iniReader.c,$(wildcard fs/*.c)) $(wildcard mst/*.c cmd/*.c drivers/*.c) modules/loader.c \
	std/std.c std/format.c std/linux_style_file.c \
	$(filter-out platform/pc/vbe.c platform/pc/vga.c,$(wildcard platform/pc/*.c)) \
	net/net_stack.c net/socket.c net/lwip_port/lwip_port.c \
	$(wildcard $(LWIP)/core/*.c $(LWIP)/core/ipv4/*.c) $(LWIP)/netif/ethernet.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) \
	$(BUILD)/std/libcpp.o $(patsubst %.asm,$(BUILD)/%.obj,$(wildcard $(ARCH_DIR)/*.asm))
ALLOCATOR := dos/mm/third_party/liballoc/liballoc-x86_64.a
ALLOCATOR_PRIVATE := $(BUILD)/liballoc-kernel.a

.PHONY: default all livecd livecd_run full
default all: $(BUILD)/kernel.bin $(BUILD)/hello.mod

$(BUILD)/%.o: %.c $(KERNEL_CONFIG_STAMP) cflags.def $(ARCH_DIR)/build.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp $(KERNEL_CONFIG_STAMP) cflags.def $(ARCH_DIR)/build.mk
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit -c $< -o $@

$(BUILD)/%.obj: %.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@

$(ALLOCATOR_PRIVATE): $(ALLOCATOR) $(ARCH_DIR)/build.mk
	@mkdir -p $(dir $@)
	objcopy $(foreach name,malloc free realloc calloc aligned_alloc posix_memalign usable_size heap_init heap_extend heap_onerror heap_set_oom_handler,--redefine-sym $(name)=liballoc_$(name)) $< $@

$(BUILD)/kernel.bin: $(OBJECTS) $(ALLOCATOR_PRIVATE) $(ARCH_DIR)/kernel.ld
	ld -m elf_x86_64 -static --gc-sections -z max-page-size=4096 -T $(ARCH_DIR)/kernel.ld \
		-o $@ $(OBJECTS) $(ALLOCATOR_PRIVATE)

$(BUILD)/modules/hello.o: modules/hello/hello.c $(KERNEL_CONFIG_STAMP) $(ARCH_DIR)/build.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -mcmodel=large -c $< -o $@
$(BUILD)/hello.mod: $(BUILD)/modules/hello.o
	ld -m elf_x86_64 -r $< -o $@

full: default
	$(MAKE) -C ../apps ARCH=x86_64

livecd: full
	../scripts/build-livecd.sh $(abspath plant-os-x86_64.iso) x86_64

QEMU_CPUS ?= 4
livecd_run: livecd
	qemu-system-x86_64 -smp $(QEMU_CPUS) -m 1024 -serial stdio \
		-cdrom plant-os-x86_64.iso -boot d -netdev user,id=net0 -device pcnet,netdev=net0

-include $(patsubst %.o,%.d,$(filter %.o,$(OBJECTS)))
