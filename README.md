[English] | [中文](doc/README_zh-cn.md)

# Plant OS

Plant OS is an educational, freestanding operating system for i386 and x86_64 PCs. It uses its own kernel, userspace ABI, runtime libraries and applications; it is not a Linux distribution or a daily-use OS. The project started as Powerint in 2020 and was renamed Plant OS in 2021.

## What works

- **Boot and processes:** i386 disk boot through DOSLDR, i386 Limine LiveCD, and x86_64 BIOS/UEFI Limine LiveCD; multitasking, virtual memory, threads, signals, syscalls and native ELF PIE/dynamic linking.
- **Storage and I/O:** FAT12/16/32 (including long filenames), PFS, a writable RAM-backed FAT initramfs, VFS, pipes and poll; PCI, USB HID/storage and PC storage controllers. Hardware support depends on the device and controller; see [USB](doc/usb.md) and [AHCI](doc/ahci.md).
- **Networking and desktop:** TCP/IP via lwIP and a socket API; a shell, Lua, graphical desktop and terminal, keyboard/mouse input, SDL3 and native applications.
- **x86_64 graphics and Java:** Mesa's software Vulkan (lavapipe) and OpenGL/EGL (llvmpipe), GLFW, OpenJDK 17 with JIT, and native LWJGL 3 bindings. Optional images run the original Minecraft 1.20.1 server or a local, windowed client. The client's OpenAL backend currently has **no speaker output**.

These are project capabilities, not a claim of complete hardware, POSIX or application compatibility. See the [development guide](doc/development.md), [architecture](doc/architecture.md) and [subsystem notes](doc/subsystems.md) for supported configurations and limitations.

## Build from source

Build on a Linux host with Python 3, GNU make, GCC/G++, binutils, NASM, mtools, QEMU, curl, tar, gzip and `xorriso` (or `genisoimage`). The i386 toolchain must support `-m32` and `elf_i386`. Native C++ libraries additionally require Clang, CMake and Ninja. The default x86_64 Mesa build also requires Meson, `llvm-ar`, matching LLVM/TableGen tools, `glslangValidator`, bison, flex, m4 and Python generator dependencies. The terminal library requires Rust nightly and `rust-src`; LWJGL needs a host JDK 17 for `javac`/`jar`. See [build prerequisites](doc/build.md) and [graphics dependencies](doc/lavapipe.md) for details.

From the repository root, prepare verified upstream sources once (cached under `apps/out/sources/`):

```sh
git clone -b ai-slop https://github.com/min0911Y/Plant-OS.git
cd Plant-OS
./init.py
```

`./init.py` downloads all optional dependency groups too, including Java, graphics and Minecraft. To prepare only selected groups, use e.g. `./init.py mesa openjdk`; repeat runs reuse the verified cache. Building these ports can require substantial disk space and time.

**i386 (applications → DOSLDR → kernel/disk → LiveCD):**

```sh
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386
make -C kernel ARCH=i386 livecd
```

The disk boot image is `kernel/boot.img`; the ISO is `kernel/plant-os-livecd.iso`. For the disk boot smoke test, use `make -C kernel ARCH=i386 img_run` (requires KVM as written in the Makefile). The older `run` and `full_run` targets use obsolete floppy-image paths; do not use them as the main boot instructions.

**x86_64 (kernel → applications → BIOS/UEFI LiveCD):**

```sh
make -C kernel ARCH=x86_64 livecd
make -C kernel ARCH=x86_64 livecd_run
```

The ISO is `kernel/plant-os-x86_64.iso`. The `livecd` target builds the kernel and x86_64 applications itself; it does not need the i386 loader or include a JDK. `livecd_run` starts QEMU with BIOS defaults. See [LiveCD details](doc/livecd.md) for firmware and image layout.

**Optional x86_64 Java and Minecraft images:**

```sh
make -C kernel ARCH=x86_64 openjdk
make -C kernel ARCH=x86_64 lwjgl-livecd
make -C kernel ARCH=x86_64 minecraft-image
make -C kernel ARCH=x86_64 minecraft-client-image MINECRAFT_CLIENT_ARCHIVE=/path/to/mc.zip
```

The LWJGL image uses a separate JDK disk; Minecraft server/client images use persistent disks. The server image includes `eula=true`: build it only if you accept the Minecraft EULA. The client requires your own 1.20.1 ZIP containing the version, libraries and assets. See [OpenJDK](doc/openjdk.md), [LWJGL](doc/lwjgl.md), [server image](doc/build.md#minecraft-与-jdk-镜像) and [client image](doc/minecraft-client.md) for deployment, QEMU run targets and prerequisites.

Generated objects, downloaded sources and images live in ignored output directories; they are not part of the source tree to commit. For regression commands and delivery checks, see [testing](doc/testing.md).

## Contributors

- Zhou Zhihao ([ZhouZhihaos](https://github.com/ZhouZhihaos))
- Qiu Chenjun (Simple OS; early 32-bit transition)
- min0911_ ([min0911Y](https://github.com/min0911Y))

Thanks also to TheFlySong, yywd_123, Oildum-was-ejected, wenxuanjun and duoduo70.
