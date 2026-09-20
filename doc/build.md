# 构建与打包

[开发指南](development.md)

需要 GNU make、GCC/G++、binutils、NASM、mtools 和 QEMU；i386 工具链须支持 `-m32`/`elf_i386`。LiveCD 另需 `curl`、`tar`、`gzip` 和 `xorriso` 或 `genisoimage`。
原生 C++ 运行库还需 Clang、CMake 和 Ninja；x86_64 默认包含 Mesa lavapipe/llvmpipe，另需 Meson、llvm-ar、匹配 LLVM 源码的宿主 TableGen、glslangValidator、bison、flex、m4 和 Python 生成器模块。版本、缓存与构建说明见 [lavapipe](lavapipe.md)。

根目录 `./init.py` 下载并校验 LLVM、Mesa、OpenJDK、LWJGL、OpenAL、终端库及原版
Minecraft 的固定依赖；也可指定分组，如 `./init.py mesa openjdk`。
版本与 SHA-256 维护在各模块的 `sources.json`，源码缓存统一放在
`apps/out/sources/`，补丁随仓库维护。重复初始化复用缓存，不能把缓存中的
手工修改当作移植源代码。终端库还需要 Rust nightly 与 `rust-src`，见 [终端](terminal.md)。
LLVM TableGen 可使用匹配的系统版本，否则构建脚本从同一份 LLVM 源码生成
宿主工具到 `apps/out/host/llvm/`。

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
- x86_64 LiveCD 设置 `PLANT_OPENJDK_DIR` 时，会额外生成同名 `-jdk.img` FAT32 磁盘；可用 `PLANT_OPENJDK_DISK` 指定路径，或用 `PLANT_OPENJDK_DISK_MIN_MIB` 为需要生成运行数据的集成测试保留最小容量。JDK 不进入 initramfs，启动后由内核挂载附加盘并以 `C:/java/bin/java` 访问。
- x86_64 LWJGL 调试 LiveCD 使用 `make -C kernel ARCH=x86_64 lwjgl-livecd`；该目标将 LWJGL 的 class、JAR、native 和 Lua 启动脚本自动放入同一附加 JDK 盘，`lwjgl-run` 直接以 GTK 窗口启动 QEMU。
- LiveCD 按构建图生成的 `applications.list` 收录应用，不能扫描残留 `.bin` 或手写第二份应用清单；i386 TCC SDK 只按构建图生成的 `sdk-libraries.list` 收录静态自举库，不能扫描残留归档或混入应用私有 PIC 库；磁盘镜像仍由 `kernel/Makefile` 的显式 `mcopy`/`mmd` 控制。改名或新增资源时同步打包规则。
- i386 LiveCD 保留 FAT/PFS 引导模板和 `DOSLDR.bin` 安装资源。`setup.mst` 从已填充镜像生成，使用 `mshortname` 得到真实 FAT 别名，覆盖全部目录和文件，`DOSLDR.bin` 为首个文件；安装器继续支持 FAT/PFS。布局见 [LiveCD](livecd.md)，应用清单以当前脚本为准。
- 日常交付保持默认 `USB_DEBUG=0`，诊断时显式启用 `USB_DEBUG=1`。`KASAN=1`、`PERF=1` 仅支持 i386；`BENCH=1` 才启用启动计时基准，`MEMTEST=0` 必须同时指定 `MEMSIZE_MB`。这些选项及 `VT100` 必须纳入构建配置指纹。

## Minecraft 与 JDK 镜像

```sh
./init.py
make -C kernel ARCH=x86_64 minecraft-image -j4 MESA_JOBS=2 OPENJDK_JOBS=2
make -C kernel ARCH=x86_64 minecraft-run
```

`minecraft-image` 构建完整 x86_64 应用、内核及 C1/C2 Server JDK，生成
`kernel/plant-os-x86_64-minecraft.iso` 和 `kernel/plant-os-x86_64-minecraft.img`。
ISO 自动启动服务端，IMG 保存 JDK、原版 JAR、配置和世界；默认是普通地形、
离线模式，宿主通过 `127.0.0.1:25565` 连接。原版捆绑依赖经 SHA-256 验证后
预解包，减少第一次启动的客户机写盘时间。镜像包含 `eula=true`，使用此目标
表示接受 Minecraft EULA。

`minecraft-run` 默认使用 KVM、四核和 4 GiB 内存，已有镜像直接运行，
不会重建或清除世界。打包目标拒绝覆盖已有磁盘；重新生成可指定
`MINECRAFT_DISK` 与 `MINECRAFT_ISO`，或明确删除不再需要的旧磁盘。
`MINECRAFT_DISK_MIB`（默认 1536）、`MINECRAFT_MEMORY`、`MINECRAFT_PORT`、
`MINECRAFT_ACCEL` 和 `MINECRAFT_CPU` 可覆盖容量及运行参数。

Minecraft Java 1.20.1 客户端使用独立的 `minecraft-client-image` /
`minecraft-client-run` 目标；输入 ZIP、私有 STB 原生库、持久磁盘和支持边界
见 [客户端构建](minecraft-client.md)。
