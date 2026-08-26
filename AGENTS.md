# Plant OS 仓库协作指南

## 注意
你应该思考让你的代码精简优化，改动小，简洁优雅，符合最佳实践。不要创建无意义的自由函数，思考尽量不要或减少新增无意义的成员变量。不要兼容旧的遗留的实现，而是删除替换旧的实现。你的实现应当有通用性与扩展性，避免硬编码与限制（比如各种固定大小的静态数组）。显式使用三个 subagent，先让一个只读 explorer 探索仓库、总结最小实现路径和测试方式；再让唯一可改代码的 worker 根据该总结实现并修复 bug；最后让只读 reviewer 审查 diff 是否正确且符合上述要求，若有问题则把反馈交回 worker 修复后复查，不要让多个 agent 同时改代码。完成后请根据实际情况更新 AGENTS.md，同时标记并用 HTML 注释来注释掉文档中过时的内容，并让 writer 进行 git commit。

<!-- 过时：通过修改 process_spawn_first_user 注入自动测试命令。当前统一临时修改 kernel/res/init.mst，并在测试后恢复；禁止使用 sendkey。 -->



## 项目定位

- 本文件适用于整个仓库。
- Plant OS 是用于学习操作系统原理的 32 位 x86（i386）操作系统，运行于保护模式，采用 BIOS/磁盘镜像启动方式。它不是宿主系统上的普通应用。
- 内核、加载器和用户程序都是 freestanding 代码：不能默认使用宿主 libc、线程库、文件系统语义或现代 CPU 运行时。
- 当前仓库的有效实现是 `kernel/` 下的 32 位内核。仓库中没有 `kernel64/`；`.gitignore`、`doc/README_zh-cn.md` 和 `scripts/build_rootfs.sh` 中残留的 `kernel64` 内容属于旧版本痕迹，不要据此创建或修改 64 位实现。
- 构建文件和当前源码比 README 中的历史描述更可信。发现两者不一致时，以实际 `Makefile` 和调用链为准，并在修改中指出差异。

## 启动与运行模型

- `kernel/boot/` 生成启动扇区；`Loader/` 生成 `Loader/out/dosldr.bin`，链接地址为 `0x100000`，入口为 `loader_main`；加载器再寻找并装载 `kernel.bin`。
- 内核生成 `kernel/obj/kernel.bin`，链接地址为 `0x280000`，入口为 `KernelMain`（`kernel/dos/init/main.c`）。
- 用户程序统一链接到 `0x70000000`，链接入口为 `Main`。`apps/libp/entry.c` 或 `apps/libp/cppstart.cpp` 完成运行时初始化后调用应用自己的 `main(argc, argv)`。
- 内核与应用共享的是项目自定义 ABI，不是 Linux ABI。地址、结构体布局、寄存器约定和中断号都可能是兼容性边界；不要随意改成宿主平台惯例。

## 目录地图

- `kernel/boot/`：启动汇编。
- `kernel/dos/`：内核初始化、内存管理、任务、IPC、系统调用和底层 CPU 支持。
- `kernel/drivers/`：存储、网络、输入、显示、声音、PCI、时钟等驱动。
- `kernel/fs/`：FAT、PFS、ISO9660、VFS、ELF 加载及路径/文件实现。
- `kernel/io/`：文本/图形显示、TTY、输入栈和日志。
- `kernel/net/`：以太网到 TCP/UDP、DHCP、DNS、HTTP、FTP 等协议实现。
- `kernel/cmd/`、`kernel/mst/`、`kernel/std/`、`kernel/modules/`：内核命令、MST 脚本、基础运行库和可加载模块。
- `kernel/include/`：内核公共声明；很多模块通过 `dos.h`、`define.h` 等大头文件耦合。
- `kernel/include/arch/x86/`：x86 专属的中断帧、入口和其他架构 ABI 声明；通用内核头文件不应重新定义这些布局。
- `kernel/res/`：打包进镜像的资源；资源是否进入镜像由 `kernel/Makefile` 中显式的 `mcopy` 命令决定。
- `Loader/`：独立的加载器，包含自己的驱动、文件系统和基础库实现。不要假设它能直接复用内核实现。
- `apps/include/`：用户态头文件和公开 ABI。
- `apps/libp/`：用户态 C/C++ 启动代码、系统调用、内存/标准库、IPC/RPC 等基础库。
- `apps/<name>/`：各个用户程序；通常每个目录有自己的 `Makefile`，产物写入 `apps/out/`。
- `apps/sdl2*`、`apps/freetype/`、`apps/libpng/`、`apps/jpeg/`、`apps/doomgeneric/`、`apps/lite-1.11/`、`apps/nasm-master/` 等包含大量移植或第三方代码。除非任务直接涉及，不要做全目录格式化或机械重写。
- `font/`、`kernel/res/` 和 `kernel/iso/` 中包含运行时二进制资源；不要把它们当作可随意重新生成的临时文件。
- `chat/`、`netgobang/`、`fattools/` 是宿主侧辅助/演示程序，不属于内核或 Plant OS 用户态 ABI。
- `scripts/kernel-perf.py` 用于把串口性能采样转换为 folded stacks；`scripts/build_rootfs.sh` 当前引用已不存在的 `kernel64`，不是 32 位主构建流程的一部分。

## 构建环境与命令

需要 GNU make、支持 `-m32`/`elf_i386` 的 GCC/G++ 与 binutils、NASM、mtools（`mformat`、`mcopy`）和 QEMU（`qemu-system-i386`、`qemu-img`）。代码还假定编译器支持 freestanding、无 PIE 的 32 位输出。

干净工作区的标准构建顺序是：

```sh
make -C apps
make -C Loader
make -C kernel
```

顺序很重要：

- `apps` 先生成 `apps/libs/*.a` 和 `apps/out/*.bin`。
- `Loader` 生成内核制镜像时需要的 `Loader/out/dosldr.bin`。
- `kernel` 最后编译内核、模块并创建/填充 `kernel/boot.img`、`kernel/disk.img` 和 `kernel/img/*.img`。
- `make -C kernel full` 会构建应用，但仍假定加载器和部分已有产物可用，不能替代上面的干净构建顺序。

常用的局部构建方式：

```sh
# 已经完成过一次 apps 全量构建后，重编单个应用
make -C apps/rpctest

# 只重编加载器
make -C Loader

# 已存在 kernel/obj 时，快速检查某个内核子系统
make -C kernel/dos/task
make -C kernel/drivers
```

最终仍应运行与改动范围相称的上层构建，因为对象列表、链接和镜像打包都由显式规则控制。

内核构建支持以下诊断开关：

```sh
make -C kernel KASAN=1
make -C kernel PERF=1
make -C kernel BENCH=0
make -C kernel MEMTEST=0 MEMSIZE_MB=512
```

- `PERF=1` 会启用性能采样并保留 frame pointer。
- `MEMTEST=0` 必须同时提供 `MEMSIZE_MB`。
- 这些命令仍依赖已经构建好的应用和加载器产物。

## 运行与验证

- 仓库没有统一的宿主侧自动化测试套件。最低验证要求是：相关局部构建成功；涉及 ABI、链接、启动或镜像内容时再执行完整构建和 QEMU 冒烟测试。
- 当前 `Mimg` 规则生成并填充 `boot.img`/`disk.img`。`make -C kernel img_run` 使用这套镜像并启动 QEMU，是与当前制镜像流程最一致的现成目标。
- `make -C kernel run`、`full_run`、`kernel/run.sh` 和 `bochsrc.txt` 仍引用 `kernel/img/Powerint_DOS_386.img`，但当前 `Mimg` 不生成该文件；除非任务专门修复旧软盘流程，不要把这些目标成功与否当作唯一验证标准。
- 现成运行目标使用 KVM/`-cpu host`。在没有 KVM 的环境中，应基于同一镜像手动运行 QEMU，并去掉 `-enable-kvm -cpu host`，而不是修改内核来迁就宿主环境。
- QEMU 命令使用 `-serial stdio`；启动、崩溃和测试输出优先从串口收集。不要只依赖图形界面现象。
- 需要自动运行系统内命令时，临时修改 `kernel/res/init.mst`，构建并测试后立即恢复该文件。禁止用 QEMU monitor 的 `sendkey` 注入命令。
- IPC/RPC 改动可在系统中运行 `rpctest.bin`；磁盘和任务生命周期相关改动可结合 `dktest.bin`。两者都已由 `kernel/Makefile` 打包进主镜像。

<!-- 过时：旧文档中的 kernel64 构建、运行和 rootfs 流程；当前仓库只有 kernel/ 下的 32 位内核。 -->
- 性能采样后可运行：

```sh
python3 scripts/kernel-perf.py \
  --kernel kernel/obj/kernel.bin \
  --serial path/to/serial.log \
  --out path/to/perf.folded
```

- 仅修改文档时至少运行 `git diff --check`。不要为了文档改动重建大型磁盘镜像。

## 跨层修改规则

### 系统调用、IPC 和 RPC

这部分是最容易产生静默 ABI 不一致的区域。修改编号、错误码、消息结构、任务字段或参数语义时，至少同时检查：

- `kernel/dos/syscall/syscall.c`
- `kernel/dos/task/ipc.c`
- `kernel/dos/task/mtask.c`
- `kernel/include/define.h`
- `kernel/include/dos.h`
- `apps/include/syscall.h`
- `apps/include/ipc.h`
- `apps/include/rpc.h`
- `apps/libp/syscall.asm`
- `apps/libp/ipc.c`
- `apps/libp/rpc.c`

用户指针来自 `0x70000000` 以上的用户地址空间。新增系统调用时必须复用现有的用户地址/长度验证方式，不能直接把用户指针当作可信内核指针。涉及阻塞、超时或任务退出时，还要检查等待原因、唤醒路径、时钟中断和孤儿/资源清理。

- 顶层系统调用与 IPC 子操作使用“语义化枚举 + designated initializer 处理表”分派。固定 ABI 编号表以枚举的 `COUNT` 作为容量，保留既有编号但不保留旧的 `if/else` 分派兼容层。
- 表处理函数应对应真实的 API 或同一职责域；不要为了减少函数体行数创建只转发一次的无意义包装。

### 内核架构边界

- x86 中断帧、汇编入口和寄存器约定放在 `kernel/include/arch/x86/` 及对应 x86 实现中。通用任务、系统调用和信号代码通过架构头访问，不在 `define.h` 重复声明布局。
- 汇编保存顺序与 C 结构布局构成内核内部 ABI。修改任一侧时同步检查任务初始栈、fork、signal、IDT 注册和最终 `iret` 恢复路径。
- 继续拆分架构代码时只处理 `kernel/`；`Loader/` 保持当前实现，除非任务明确要求修改。

### 新增或修改内核源文件

- 各子目录 `Makefile` 都使用显式对象列表；新增 `.c`/`.cpp`/`.asm` 文件时必须加入对应列表。
- 要链接进主内核的对象还必须出现在 `kernel/Makefile` 的 `OBJS_BOOTPACK` 中。
- 新公共声明放入合适的 `kernel/include/*.h`，避免继续无条件扩大 `dos.h`；但修改已有共享结构时要检查所有直接依赖者。
- 中断、驱动和调度代码可能在不可阻塞或中断关闭的上下文执行。引入分配、锁、日志或等待前，先确认调用上下文。

### 新增或修改应用

- 应用通常包含 `../defs.mk`，定义 32 位 freestanding 编译参数、`Main` 入口和基础库链接方式。沿用相邻小型应用的 `Makefile`，不要使用宿主默认链接规则。
- 新应用必须加入 `apps/Makefile` 才会进入全量构建。
- 如果应用需要出现在系统镜像中，还必须在 `kernel/Makefile` 的合适镜像段添加显式 `mcopy`；仅生成 `apps/out/<name>.bin` 不会自动打包。
- C/C++ 应用实现常规的 `main`，不要绕过 `apps/libp` 的 `Main` 启动包装，除非任务明确要求自定义运行时。
- 修改用户态库时检查 C 与 C++ 两套归档：`libp.a`、`libcpps.a`，以及 `libabi.a`、`libgui.a` 等相关产物。

### 加载器、文件系统和磁盘格式

- `Loader/` 与 `kernel/` 各自有 FAT/PFS/VFS 和驱动代码。修改磁盘结构或加载协议时必须检查两边的结构定义和读写逻辑，不能只修一侧。
- 启动扇区、加载地址、ELF 入口和分区/文件系统布局属于启动 ABI；任何改动都需要完整构建和冷启动验证。
- `kernel/Makefile` 使用大量显式 `mcopy`/`mmd` 命令。重命名产物、资源或目录时同步更新所有打包位置。

## 代码风格与实现约束

- 项目自有 C/C++ 代码遵循根目录 `.clang-format`：2 空格缩进、不使用 Tab、左大括号同行、指针星号靠变量。只格式化本次触及的项目自有文件。
- 历史代码风格并不完全统一，且部分目录关闭了警告。不要借功能修改之机大面积重排代码；编译成功也不代表没有截断、越界、符号扩展或并发问题。
- 内核编译禁用宿主头文件、内建函数、栈保护、PIE、MMX/SSE，并使用 x87；C++ 禁用异常和 RTTI。新代码必须保持这些限制，不要引入依赖异常、线程局部存储、动态链接或宿主运行时的库。
- 编译参数使用 UTF-8 输入、GB2312 执行字符集。修改中文字符串、字体或终端输出时要考虑转换和字节长度，不能默认 UTF-8 字节序列会原样进入镜像。
- 优先使用仓库已有的整数类型、分配器、字符串/内存函数、锁和日志接口。引入宿主专用 API 前先确认它确实只用于 `scripts/` 或宿主工具。
- 该代码大量依赖 32 位指针和整数互转。新增代码应使用项目已有的 `uintptr_t`/固定宽度类型表达地址，并显式检查溢出、对齐和范围。
- 不要修改或提交 `apps/out/`、`apps/libs/`、`Loader/out/`、`kernel/obj/`、`kernel/img/`、`kernel/*.img`、`kernel/*.log` 等生成物，除非任务明确要求交付镜像或二进制。
- 不要随意执行全量 `clean`：清理规则会递归删除大量产物，之后完整重建耗时且会重新创建大型镜像。需要清理时只清理与任务相关、可确认可重建的目标。

## 提交前检查

1. 确认只改了任务需要的源码/文档，没有顺手格式化第三方目录或提交构建产物。
2. 新文件已加入对应显式对象列表、顶层应用列表或镜像打包规则。
3. ABI 改动已同步内核、用户头文件和 `libp` 包装层。
4. 运行最小相关构建；启动链、驱动、文件系统、调度或 ABI 改动再做完整构建和 QEMU 串口冒烟测试。
5. 运行 `git diff --check`，并在交付说明中列出实际执行的验证以及由于环境限制未执行的验证。
