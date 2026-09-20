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

SOURCES := $(wildcard $(ARCH_DIR)/*.c) arch/x86/common/interrupt_controller.c arch/x86/common/clock.c \
	dos/init/main.c dos/init/init.c dos/init/kernelc.c dos/mm/heap.c \
	$(wildcard dos/task/*.c dos/hal/*.c dos/tools/*.c) dos/syscall/syscall.c \
	dos/perf/perf.c dos/mm/kasan.c dos/mm/user_vm.c dos/mm/user_vm_file.c \
	io/input_device.c io/tty.c io/screen.c io/input.c io/is.c io/log.c io/draw.c io/sheet.c \
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
PLANT_OPENJDK_DIR ?= ../apps/out/x86_64/openjdk/images/jdk
PLANT_OPENJDK_DISK ?= plant-os-x86_64-jdk.img
PLANT_LWJGL_DIR ?= ../apps/out/x86_64/lwjgl
LWJGL_ISO ?= plant-os-x86_64.iso

.PHONY: default all livecd livecd_run full lwjgl-livecd lwjgl-run openjdk minecraft-image minecraft-run
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

# Build a runnable x86_64 image with the Plant JDK and the LWJGL deployment
# tree on the attached FAT disk.  The ordinary livecd target intentionally
# remains JDK-free.
lwjgl-livecd: full
	$(MAKE) -C ../apps ARCH=x86_64 lwjgl
	PLANT_OPENJDK_DIR=$(abspath $(PLANT_OPENJDK_DIR)) \
	PLANT_OPENJDK_DISK=$(abspath $(PLANT_OPENJDK_DISK)) \
	PLANT_LWJGL_DIR=$(abspath $(PLANT_LWJGL_DIR)) \
	../scripts/build-livecd.sh $(abspath $(LWJGL_ISO)) x86_64

QEMU_CPUS ?= 4
livecd_run: livecd
	qemu-system-x86_64 -smp $(QEMU_CPUS) -m 1024 -serial stdio \
		-cdrom plant-os-x86_64.iso -boot d -netdev user,id=net0 -device pcnet,netdev=net0

LWJGL_QEMU_ACCEL ?= tcg
LWJGL_QEMU_CPU ?= $(if $(filter kvm,$(LWJGL_QEMU_ACCEL)),host,max,-xgetbv1)
LWJGL_QEMU_MEMORY ?= 4096
lwjgl-run: lwjgl-livecd
	qemu-system-x86_64 -accel $(LWJGL_QEMU_ACCEL) -cpu $(LWJGL_QEMU_CPU) \
		-smp $(QEMU_CPUS) -m $(LWJGL_QEMU_MEMORY) -display gtk \
		-serial stdio -monitor none -no-reboot \
		-cdrom $(abspath $(LWJGL_ISO)) -boot d \
		-drive file=$(abspath $(PLANT_OPENJDK_DISK)),format=raw,if=ide,index=0

OPENJDK_JOBS ?= 2
MINECRAFT_ISO ?= plant-os-x86_64-minecraft.iso
MINECRAFT_DISK ?= plant-os-x86_64-minecraft.img
MINECRAFT_DISK_MIB ?= 1536
MINECRAFT_PORT ?= 25565
MINECRAFT_MEMORY ?= 4096
MINECRAFT_ACCEL ?= kvm
MINECRAFT_CPU ?= $(if $(filter kvm,$(MINECRAFT_ACCEL)),host,max,-xgetbv1)
openjdk:
	python3 ../scripts/build-openjdk-jit.py --jobs $(OPENJDK_JOBS)

minecraft-image: full
	$(MAKE) ARCH=x86_64 openjdk
	python3 ../scripts/build-minecraft.py --jdk $(abspath $(PLANT_OPENJDK_DIR)) \
		--iso $(abspath $(MINECRAFT_ISO)) --disk $(abspath $(MINECRAFT_DISK)) \
		--disk-mib $(MINECRAFT_DISK_MIB)

# Running an existing server must not reformat its world disk.
minecraft-run:
	@test -f "$(MINECRAFT_ISO)" -a -f "$(MINECRAFT_DISK)" || $(MAKE) ARCH=x86_64 minecraft-image
	qemu-system-x86_64 -accel $(MINECRAFT_ACCEL) -cpu $(MINECRAFT_CPU) \
		-smp $(QEMU_CPUS) -m $(MINECRAFT_MEMORY) -display gtk -serial stdio \
		-cdrom plant-os-x86_64.iso -boot d \
		-drive file=$(abspath $(MINECRAFT_DISK)),format=raw,if=ide,index=0 \
		-netdev user,id=mc,hostfwd=tcp:127.0.0.1:$(MINECRAFT_PORT)-:25565 -device pcnet,netdev=mc

-include $(patsubst %.o,%.d,$(filter %.o,$(OBJECTS)))
