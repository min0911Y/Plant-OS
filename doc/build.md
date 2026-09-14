# 构建与打包

[开发指南](development.md)

需要 GNU make、GCC/G++、binutils、NASM、mtools 和 QEMU；i386 工具链须支持 `-m32`/`elf_i386`。LiveCD 另需 `curl`、`tar`、`gzip` 和 `xorriso` 或 `genisoimage`。
原生 C++ 运行库还需 Clang、CMake 和 Ninja；x86_64 默认包含 Mesa lavapipe/llvmpipe，另需 Meson、匹配 LLVM 源码的宿主 TableGen、glslangValidator、bison、flex、m4 和 Python 生成器模块。版本、缓存与构建说明见 [lavapipe](lavapipe.md)。

```sh
# i386：按顺序构建应用、DOSLDR、内核与磁盘镜像
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386
make -C kernel ARCH=i386 livecd

# x86_64：独立生成 BIOS/UEFI LiveCD，不依赖 DOSLDR 或 boot.img
make -C kernel ARCH=x86_64 livecd
```

- 支持 `ARCH=i386|x86_64`、`PLATFORM=pc`；未支持的架构、配置和功能必须明确报错。i386 ISO 为 `kernel/plant-os-livecd.iso`，x86_64 为 `kernel/plant-os-x86_64.iso`；后者的对象、应用和库分别位于 `kernel/obj/x86_64/`、`apps/out/x86_64/`、`apps/libs/x86_64/`，不得混用架构产物。
- 局部构建可用 `make -C apps/gui ARCH=x86_64` 或 `make -C kernel/dos/task ARCH=i386`；首次先完成对应架构构建，交付前按改动范围运行上层链接与打包。`make -C kernel full` 不能替代 i386 干净构建顺序。
- 内核新增源文件须接入对应构建图：i386 同步子目录对象列表与 `OBJS_BOOTPACK`，x86_64 检查 `kernel/arch/x86/x86_64/build.mk`。保留 `.d` 依赖和配置指纹，保证头文件、编译选项变化后自动重编；公共声明放入职责明确的头文件，避免继续扩大 `dos.h`。
- 应用统一使用 `apps/build.mk`、`apps/native-apps.mk` 和 `apps/dynamic.mk`；新增应用通过 `application` 宏注册，子目录 Makefile 只设置 `NATIVE_TARGETS` 并包含 `../native.mk`。两种架构共用源码清单，第三方对象不写回源目录。
- 两种架构的 LiveCD 均将完整 FAT initramfs 压缩为 `initramfs.img.gz`，由 Limine 的 `$` 路径前缀透明解压后交付内核；ISO 不包含未压缩副本，运行时仍为可写 FAT RAM 盘。
- x86_64 LiveCD 设置 `PLANT_OPENJDK_DIR` 时，会额外生成同名 `-jdk.img` FAT32 磁盘；可用 `PLANT_OPENJDK_DISK` 指定路径。JDK 不进入 initramfs，启动后由内核挂载附加盘并以 `C:/java/bin/java` 访问。
- x86_64 LWJGL 调试 LiveCD 使用 `make -C kernel ARCH=x86_64 lwjgl-livecd`；该目标将 LWJGL 的 class、JAR、native 和 Lua 启动脚本自动放入同一附加 JDK 盘，`lwjgl-run` 直接以 GTK 窗口启动 QEMU。
- LiveCD 按构建图生成的 `applications.list` 收录应用，不能扫描残留 `.bin` 或手写第二份应用清单；i386 TCC SDK 只按构建图生成的 `sdk-libraries.list` 收录静态自举库，不能扫描残留归档或混入应用私有 PIC 库；磁盘镜像仍由 `kernel/Makefile` 的显式 `mcopy`/`mmd` 控制。改名或新增资源时同步打包规则。
- i386 LiveCD 保留 FAT/PFS 引导模板和 `DOSLDR.bin` 安装资源。`setup.mst` 从已填充镜像生成，使用 `mshortname` 得到真实 FAT 别名，覆盖全部目录和文件，`DOSLDR.bin` 为首个文件；安装器继续支持 FAT/PFS。布局见 [LiveCD](livecd.md)，应用清单以当前脚本为准。
- 日常交付保持默认 `USB_DEBUG=0`，诊断时显式启用 `USB_DEBUG=1`。`KASAN=1`、`PERF=1` 仅支持 i386；`BENCH=1` 才启用启动计时基准，`MEMTEST=0` 必须同时指定 `MEMSIZE_MB`。这些选项及 `VT100` 必须纳入构建配置指纹。
