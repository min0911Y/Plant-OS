[English](../README.md) | 中文

# Plant OS

Plant OS 是面向 i386 和 x86_64 PC 的教学用独立操作系统，拥有自己的内核、用户态 ABI、运行库与应用；它不是 Linux 发行版，也不适合作为日常工作系统。项目于 2020 年以 Powerint 起步，2021 年更名为 Plant OS。

## 当前支持的功能

- **启动与进程：**i386 通过 DOSLDR 从磁盘启动，i386 Limine LiveCD，以及 x86_64 BIOS/UEFI Limine LiveCD；多任务、虚拟内存、线程、信号、系统调用与原生 ELF PIE/动态链接。
- **存储与 I/O：**FAT12/16/32（含长文件名）、PFS、可写的内存 FAT initramfs、VFS、管道与 poll；PCI、USB HID/存储和 PC 存储控制器。设备支持范围见 [USB](usb.md) 和 [AHCI](ahci.md)。
- **网络与桌面：**基于 lwIP 的 TCP/IP 与 socket 接口；Shell、Lua、图形桌面和终端、键鼠输入、SDL3 与原生应用。
- **x86_64 图形与 Java：**Mesa 软件 Vulkan（lavapipe）、OpenGL/EGL（llvmpipe）、GLFW、支持 JIT 的 OpenJDK 17，以及原生 LWJGL 3 绑定。可选镜像运行原版 Minecraft 1.20.1 服务端或本地窗口客户端；客户端的 OpenAL 后端目前**没有扬声器输出**。

以上是项目已实现的能力，不代表完整硬件、POSIX 或应用兼容性；配置与限制见[开发指南](development.md)、[架构](architecture.md)和[子系统说明](subsystems.md)。

## 从源码编译

宿主环境为 Linux，需 Python 3、GNU make、GCC/G++、binutils、NASM、mtools、QEMU、curl、tar、gzip，以及 `xorriso` 或 `genisoimage`。i386 工具链须支持 `-m32` 与 `elf_i386`。原生 C++ 运行库还需 Clang、CMake、Ninja；默认的 x86_64 Mesa 构建还需 Meson、`llvm-ar`、匹配 LLVM 的 TableGen 工具、`glslangValidator`、bison、flex、m4 和 Python 生成模块。终端库还需 Rust nightly 和 `rust-src`；LWJGL 还需宿主 JDK 17 的 `javac`/`jar`。详细要求见[构建指南](build.md)及[图形依赖](lavapipe.md)。

在仓库根目录准备经校验的上游源码（缓存于 `apps/out/sources/`）：

```sh
git clone -b ai-slop https://github.com/min0911Y/Plant-OS.git
cd Plant-OS
./init.py
```

`./init.py` 默认下载全部可选依赖组，包括 Java、图形和 Minecraft；也可仅准备指定组，例如 `./init.py mesa openjdk`。重复执行会复用已校验的缓存。编译这些移植库需要较多磁盘空间和时间。

**i386（应用 → DOSLDR → LiveCD，不生成磁盘镜像）：**

```sh
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386 livecd
```

ISO 为 `kernel/plant-os-livecd.iso`。如需旧磁盘启动镜像 `kernel/boot.img`，再单独运行 `make -C kernel ARCH=i386`；LiveCD 不依赖它。磁盘启动冒烟测试用 `make -C kernel ARCH=i386 img_run`（当前 Makefile 要求 KVM）。旧 `run`、`full_run` 依赖过时软盘路径，不能作为主要启动方法。

**x86_64（内核 → 应用 → BIOS/UEFI LiveCD）：**

```sh
make -C kernel ARCH=x86_64 livecd
make -C kernel ARCH=x86_64 livecd_run
```

ISO 为 `kernel/plant-os-x86_64.iso`。`livecd` 会自动构建内核及该架构的应用，不依赖 i386 DOSLDR，也不携带 JDK；`livecd_run` 默认以 BIOS 配置启动 QEMU。固件及镜像布局见 [LiveCD](livecd.md)。

**可选的 x86_64 Java 和 Minecraft 镜像：**

```sh
make -C kernel ARCH=x86_64 openjdk
make -C kernel ARCH=x86_64 lwjgl-livecd
make -C kernel ARCH=x86_64 minecraft-image
make -C kernel ARCH=x86_64 minecraft-client-image MINECRAFT_CLIENT_ARCHIVE=/path/to/mc.zip
```

LWJGL 镜像使用独立 JDK 磁盘；Minecraft 服务端与客户端使用持久磁盘。服务端镜像写入 `eula=true`，仅在接受 Minecraft EULA 后构建。客户端需要自备包含 1.20.1 版本、依赖库及资源的 ZIP。部署、QEMU 运行目标及附加依赖见 [OpenJDK](openjdk.md)、[LWJGL](lwjgl.md)、[服务端镜像](build.md#minecraft-与-jdk-镜像)和[客户端镜像](minecraft-client.md)。

对象、下载的源码与镜像位于已忽略的输出目录，不应提交到仓库。回归命令与交付检查见[验证指南](testing.md)。

GitHub Actions 在 `ai-slop` 分支构建两种架构的 LiveCD ISO 和原生 x86_64 JDK ZIP，下载方式见[构建产物](build.md#github-actions-构建产物)；Minecraft 客户端镜像仍需自行提供完整客户端 ZIP。

## 贡献者

- Zhou Zhihao（[ZhouZhihaos](https://github.com/ZhouZhihaos)）
- Qiu Chenjun（Simple OS；早期 32 位迁移）
- min0911_（[min0911Y](https://github.com/min0911Y)）

感谢 TheFlySong、yywd_123、Oildum-was-ejected、wenxuanjun 和 duoduo70。
