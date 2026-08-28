# Plant OS 仓库协作指南

## 注意
继续重新审视当前代码，还有没有可以优化，整合，删除你新增的和原有的冗余代码。进一步精简你的实现，使得你的实现更加内聚，而不是到处打补丁。你的实现应该是正确的，标准的，高性能的。你应该思考让你的代码精简优化，改动小，简洁优雅，符合最佳实践。你的实现应该是高性能的。不要创建无意义的自由函数，思考尽量不要或减少新增无意义的成员变量。不要兼容旧的遗留的实现，而是删除替换旧的实现。你的实现应当有通用性与扩展性，避免硬编码与限制。消除嵌套，消除强耦合，消除不必要的短小函数，用较为oop的设计，使用struct+enum等进行抽象和封装，禁止考虑向后兼容。适当拆出变量，从而更加清晰且减少嵌套。对于每个功能的实现，优先思考更精简优雅的写法，再考虑适当拆除变量从而提升可读性。你的代码更加精简优雅，规范且标准，效率高，结构清晰整洁，符合最佳实践。完全与各方面解耦。避免到处打补丁的做法，从架构和设计上使用更精简优雅的实现。做完之后，需要重新补充AGENTS.md

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
- `kernel/net/`：lwIP 2.2.1（`third_party/lwip`）、`net_stack.c` 的链路/DHCP 生命周期和 `socket.c` 的用户态 socket 端点实现；网卡帧适配位于 `kernel/drivers/network.c`。
<!-- 过时：`kernel/cmd/` 保存内核命令实现。 -->
- `kernel/cmd/`：系统调用到用户态 `apps/psh` 命令模式的适配层。不得恢复旧内核 `if/else` 命令解析器及其 `chat`、`netgobang` 实现；构造执行请求时 `argv[0]` 必须是实际 shell `psh.bin`。
- 用户可见的磁盘重挂载命令唯一名称是 `remount_drive`，接受单字母盘符或常规 `X:` 写法并在内部规范化为大写盘符；仓库调用方统一使用 `remount_drive X:`，不得保留 `rdrv` 别名。
<!-- 过时：用户态 shell 及安装工具通过 `rdrv` 重挂载磁盘，并由调用方直接传递未经校验的盘符。 -->
- `apps/psh` 中严格无参的简单内建命令（`cls`、`mem`、`pause`、`lsmod`）通过只读命令表分派；`dir` 独立接受零或一个目录参数，零参数传空路径枚举当前目录，有参数则直接传给 `list_directory`。有参命令继续按各自语义解析和校验，不要把不同参数模型硬塞进同一处理表。
<!-- 过时：`apps/psh` 的所有简单无参内建命令都在主命令函数中使用连续 `if/else` 分派。 -->
- `apps/psh -c` 直接把 `argv[2..]` 作为命令 argv 交给统一分派，不拼接后重新解析。交互输入也使用 `apps/libp/runtime_args.c` 的同一 quote/backslash-aware parser；所有内建命令校验 exact argc，外部命令通过共享的可逆 builder 生成执行命令行。
<!-- 过时：`psh -c` 只接受一个不含空格的命令参数。 -->
<!-- 过时：`psh -c` 把 `argv[2..]` 用裸空格拼接成字符串，再交给 shell 的第二套解析器。 -->
- `apps/psh` 的交互行编辑统一使用其 MIT vendored 的 `third_party/pl_readline`；适配层必须把 `KEY_INPUT_*` 方向键及 Enter、Backspace、Tab 映射为库按键，只丢弃 `getch()` 返回的 `0` 与其他无字符控制值，不能再把它们写进命令行或恢复另一套本地编辑器。补全词表只含 psh 内建命令，首词统一用 `PL_COLOR_CYAN` 染色，文件路径暂不参与补全。该移植版将内部 `pl_list_*` 名称空间化以避开 MST 的链表 API，并依赖文本与高文本 TTY 保持标准 CR 和 `CSI K` 语义。
- `kernel/mst/`、`kernel/std/`、`kernel/modules/`：MST 脚本、基础运行库和可加载模块。
- `kernel/include/`：内核公共声明；很多模块通过 `dos.h`、`define.h` 等大头文件耦合。
- `kernel/include/arch/x86/`：x86 专属的中断帧、入口和其他架构 ABI 声明；通用内核头文件不应重新定义这些布局。
- `kernel/arch/x86/`：x86 专属实现，内核里所有 x86 汇编入口（异常、中断/系统调用 stub、BIOS 实模式切换、任务上下文、用户态返回）都在此目录，`kernel/` 其余子目录不再包含 `.asm`（`kernel/boot/` 的启动扇区除外）。GDT、IDT、TSS、selector 和 BIOS 实模式切换所需的临时 descriptor 由此目录私有持有；通用内核代码的新临界区通过 `irq_state_t`、`irq_save()`、`irq_restore()` 接口访问中断状态，其 x86 `pushfl/cli/sti` 实现也放在此目录。
<!-- 过时：`interrupt_disable/get_interrupt_state/set_interrupt_state` 在 `kernel/dos/task/lock.c` 中实现并从 `dos.h` 暴露。 -->
<!-- 过时：GDT/IDT 地址、descriptor/TSS 布局和 `set_segmdesc`/`set_gatedesc`/`load_*` 从 `define.h`、`dos.h` 暴露给通用代码。 -->
- `kernel/res/`：打包进镜像的资源；资源是否进入镜像由 `kernel/Makefile` 中显式的 `mcopy` 命令决定。
- `Loader/`：独立的加载器，包含自己的驱动、文件系统和基础库实现。不要假设它能直接复用内核实现。
- `apps/include/`：用户态头文件和公开 ABI。
- `apps/libp/`：用户态 C/C++ 启动代码、系统调用、内存/标准库、IPC/RPC 等基础库。
- `apps/<name>/`：各个用户程序；通常每个目录有自己的 `Makefile`，产物写入 `apps/out/`。
- `apps/sdl2*`、`apps/freetype/`、`apps/libpng/`、`apps/jpeg/`、`apps/doomgeneric/`、`apps/lite-1.11/`、`apps/nasm-master/` 等包含大量移植或第三方代码。除非任务直接涉及，不要做全目录格式化或机械重写。
<!-- 过时：`kernel/a.iso`、`kernel/cd.iso` 和 `kernel/iso/psh.bin` 属于当前启动链使用的 ISO 镜像与运行时程序。 -->
- `font/`、`kernel/res/` 包含当前镜像使用的运行时二进制资源；`kernel/iso/` 中只有 `modules/*` 被当前 `kernel/Makefile` 打包，其余内容是未接入当前 32 位 `Mimg` 流程的旧 ISO 暂存资源。不要重新提交 `a.iso`、`cd.iso` 或 `iso/psh.bin` 等旧生成物。
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
- GUI RPC 与共享映射改动可运行 `guitest.bin`：它 fork 出 `gui.bin`，验证服务发现、窗口创建、共享 framebuffer、异步刷新、键盘队列控制与关闭回收，并在串口输出 `GUITEST PASS`。自动运行时仍临时修改 `init.mst`，测试后立即恢复。
- x86 异常入口与用户异常退出可在系统中运行 `exc_test.bin`，它依次验证 `#DE`、`#UD`、`#GP`、`#PF` 的子进程退出状态；该程序同样已打包进主镜像。

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

### 网络栈

- 网络协议的唯一实现是 `kernel/net/third_party/lwip`（上游 lwIP 2.2.1）加 `kernel/net/net_stack.c`/`socket.c`。禁止恢复手写 ARP、IPv4、ICMP、UDP、TCP、DHCP、DNS、HTTP、FTP 或第二套协议状态机。
- 当前端口使用 `NO_SYS=1` 和 lwIP raw API：网卡 IRQ 交付完整、无 FCS 的以太网帧，`net_stack_tick()` 在时钟中断中驱动 lwIP timeout 并轮询 lwIP 原生 loopif，`socket.c` 只把 raw callback 封装为用户端点。`net_stack_initialize()` 无条件创建 `lo`（`127.0.0.1`），与以太网/DHCP 生命周期分离；本机 IPv4 输出必须等 lwIP output 调用完全返回后再通过 `net_stack_poll_local()` 立即 drain 回环队列，时钟轮询仅作兜底，不得手写回环包或从 raw-API output callback 直接重入 `ip4_input`。调用 raw API 的普通内核路径必须以 `irq_save()`/`irq_restore()` 串行化，不能引入未受保护的 netconn/socket 线程层。
- `kernel/drivers/network.c` 只负责选择链路驱动；PCnet/RTL8139 只负责 PCI、DMA、寄存器和 IRQ。驱动必须报告实际接收长度，并接收不含 FCS 的发送帧；不得恢复 `Card_Recv_Handler`、`netcard_send`、IP 缓存、DHCP 忙等或驱动内协议解析。
- 用户态网络 ABI 是 `apps/include/socket.h` 的 `socket`/`bind`/`connect`/`listen`/`accept`/`sendto`/`recvfrom`/`socket_close`，句柄按 task group 所有而不是内核指针，并通过 `SYSCALL_SOCKET`（`int 0x36`，编号 `0x5e`）的定长 request 分派；内核与 `libp` 必须同步更新 request 布局、操作枚举和错误码。`AF_INET` 支持 TCP stream、UDP datagram 与 `IPPROTO_ICMP` raw socket；`AF_LOCAL` 支持全局命名的 stream/datagram 端点，accept 出来的服务端句柄归监听者 task group。不得恢复 `Socket_*`、独立 `ping` syscall、DPL3 的 `int 0x30` 网络入口或跨层暴露 PCB 指针。
- socket 阻塞调用通过 `WAIT_REASON_SOCKET` 的 waiter 和 lwIP callback 唤醒，连接超时由 `net_socket_tick()` 检查；不得退化为反复 `task_next()` 轮询。RAW 接收必须复制完整 IPv4 packet 后再让 lwIP 继续处理，不能借用会被协议栈改写的 pbuf。
- `network=enable` 仅启动以太网和异步 lwIP DHCP；`lo` 不依赖网卡或租约，地址可在租约完成前为零，不能把网络启动改回阻塞式 DHCP 或持久化旧的 `ip/gateway/submask/dns` 环境变量。
- 网络验证优先使用 `nettest.bin`：`nettest.bin loopback` 在无需网卡/DHCP 时验证 HPET 单调时间能在一个 10ms tick 内前进、`127.0.0.1` 的 UDP/TCP/ICMP，以及通过 `monotonic_ns()` 测得的 ICMP RTT 小于 10ms；完整模式再验证跨进程 `AF_LOCAL` stream、`AF_LOCAL` datagram、DHCP、QEMU user-net 网关 UDP/ICMP，以及可选 TCP echo。`ping.bin <host-or-ipv4> [count]` 通过 `getaddrinfo` 使用 lwIP DNS，`ping.bin localhost` 和 `ping.bin 127.0.0.1` 不等待 DHCP，适合验证回环 raw ICMP；RTT 使用 HPET 单调纳秒时间，统一按三位小数毫秒显示，低于 1 微秒时显示 `time<0.001 ms`。自动验证时临时修改 `sys.cfg` 与 `init.mst`，结束后立即恢复，仍禁止 `sendkey`。

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
- `clock()`/`SYSCALL_UPTIME` 保持既有毫秒 ABI；高分辨率单调时间使用 `monotonic_ns()`/`SYSCALL_MONOTONIC_NS`，按 i386 64 位返回约定由 `EDX:EAX` 返回纳秒。内核优先稳定读取 64 位 HPET counter 并用完整 femtosecond period 换算，HPET 不可用时才退回 10ms tick；不得把 HPET MMIO 指针暴露给用户态或改变旧 `clock()` 的单位。
- 表处理函数应对应真实的 API 或同一职责域；不要为了减少函数体行数创建只转发一次的无意义包装。
- GUI 进程是名为 `gui` 的 RPC 服务。用户态 GUI API 只使用不透明 `window_t` 句柄和 `gui_rpc.h` 的定长协议，禁止恢复 `int 0x72`、`set_custom_handler`、跨页执行 GUI 函数或向客户端暴露 GUI 内部指针。创建窗口时由 GUI 通过带 tid/generation 校验的共享映射一次性提供 framebuffer 与单生产者/单消费者事件队列；像素写入、事件/键盘轮询必须直接访问该共享区域。`window_refresh` 在共享 damage 状态中合并矩形，只在从空闲变为待处理时发送无应答 RPC；该通知使用 `IPC_DELIVER_NOW`，接收端正因 IPC 等待时内核直接交接时间片，避免等下一个时钟 tick。该标志只用于短小、无锁的低延迟单向通知，不能在中断上下文或持有会阻塞的锁时使用。`0xf0100000..0xf1000000` 是客户端 GUI 映射保留区，关闭窗口前由客户端解除映射。
- 文件读取与 VFS mount/change/unmount 用户态包装必须保留并返回内核 `eax` 状态；内核、`apps/libp` 和 `apps/include/syscall.h` 的返回语义必须一致。
- 目录枚举的用户态 API 是 `list_directory(path, finfo **entries, size_t *count)`：空路径枚举当前目录，非空路径必须按当前文件系统的相对/绝对目录语义解析；先查询条目数，再按容量填充；合法空目录返回成功且 `count == 0`，失败返回负状态。目录变化导致容量不足时由 `libp` 重新查询并安全重试，不得恢复固定 512 项缓冲区、尾部零哨兵或无容量的旧 `listfile` ABI。
<!-- 过时：`listfile(path)` 固定分配 512 个 `finfo_block`，由内核写入零名称哨兵。 -->
- 命令行 syscall 同样使用 query + capacity；`get_command_line` 动态取得一份 mutable storage。C/C++ 启动代码共同调用 `runtime_arguments_load`，argv 指针数组动态分配并指向该 storage，main 返回后统一 destroy；不得恢复 `GetCmdline`、1024 字节 line 或 128 项/字节 argv 上限。
<!-- 过时：C 与 C++ 启动代码分别用 `GetCmdline` 写固定 1024 缓冲，再为每个 argv 固定分配 128 字节。 -->

### 内核架构边界

- x86 中断帧、汇编入口和寄存器约定放在 `kernel/include/arch/x86/` 及对应 x86 实现中。通用任务、系统调用和信号代码通过架构头访问，不在 `define.h` 重复声明布局。
- x86 的 0..31 异常统一通过 `kernel/arch/x86/exceptions.asm` 构造规范化 frame，并由 `x86_exception_dispatch` 按只读描述表分派。只有 `#NM` 的 lazy-FPU 恢复和合法的写时复制 `#PF` 可以返回；普通用户异常依据保存的 `CS.RPL` 终止当前任务，NMI、双重故障、机器检查及所有内核异常必须 fail-stop。
<!-- 过时：异常入口分散在 `kernel/dos/asm/errors.asm`，通过 FS 猜测用户态、改写 CatchEIP 或在汇编中单独处理/自旋。 -->
- x86 控制寄存器访问统一使用 `kernel/include/arch/x86/control.h` 的固定宽度 inline 接口和 `X86_CR0_*` 位定义；写 CR0/CR3 必须带 `memory` clobber，页故障地址通过 `x86_cr2_read` 获取。
<!-- 过时：CR0 位定义放在 `define.h`，并同时保留 `get_cr0/set_cr0` 与 `load_cr0/store_cr0` 多套实现。 -->
- x86 port I/O 统一使用 `kernel/include/arch/x86/io.h` 的 `x86_port_read8/16/32` 与 `x86_port_write8/16/32`；动态 port（特别是 PCI BAR）必须先验证是 I/O 空间、整个寄存器窗口不超过 `uint16_t` 范围，然后才显式收窄；数据宽度必须与硬件寄存器一致。这些 inline asm 都是 `volatile`，使用 `Nd` port/累加器约束并带 `memory` clobber；`x86_io_wait()` 只通过向 `0x80` 写入一个 8-bit 零实现，仅用于 PIC 初始化和明确需要该 legacy delay 的软盘控制器轮询。
<!-- 过时：port I/O、中断开关、EFLAGS 和文本光标通过 `kernel/dos/asm/i386.asm` 的 `io_*`/`ASM_call` 全局 wrapper 访问。 -->
- 普通临界区必须成对使用 `irq_save()`/`irq_restore()` 保留调用者 IF 状态；等待路径要在关中断时发布等待状态并调用 `task_next()`，切回后才 restore。`irq_enable()` 只用于首次启动、明确允许抢占的 syscall 入口、任务终止后等待调度以及需主动开中断等待硬件 IRQ 的路径；不得用它代替临界区 restore。新内核任务可能由中断内的调度切入，因此确实要允许硬件 IRQ 的 bootstrap 入口（例如 `init()`）必须自己显式 enable，不得依赖下游驱动的等待函数偶然开中断。`x86_eflags_read/write` 只用于 AC 位等必须直接修改 EFLAGS 的 CPU 探测，不用于中断保护。
- `kernel/dos/asm/` 已整体退役：x86 中断/系统调用入口 stub 位于 `kernel/arch/x86/interrupt_entries.asm`，其余 legacy 汇编（`memtest_sub`、`gensound`、CPUID `get_cpu*`、`init_page`、`check`、`init_float`、`__init_PIT`、v86 与内核侧 `return_to_app` trampoline、`setjmp`/`longjmp`）已改写为 C 或删除，不得恢复。用户态的信号返回 trampoline 由 `apps/libp` 自己提供并通过 `set_rt` 注册，内核不再复制代码页。`interrupt_entries.asm` 中每个 stub 仍是逐份复制的 push/pop 样板，后续应作为独立任务收敛为宏或统一的 IRQ dispatch。
<!-- 过时：`kernel/dos/asm/i386.asm` 保留 `memtest_sub`、`gensound`、CPUID `get_cpu*`、`init_page`、`check`、v86/返回 trampoline 和 FPU 相关 legacy 实现，后续拆分作为独立任务。 -->
- CPUID 统一使用 `kernel/include/arch/x86/cpuid.h` 的 `x86_cpuid(leaf, subleaf)`，返回值 `x86_cpuid_t` 的字段顺序即 eax/ebx/ecx/edx，可直接按小端字节流复制（brand string 即依赖此性质）。EBX 必须写成输出约束交给编译器保存，不得再出现私有的 `cpuid` 内联包装或破坏 C ABI 的汇编版本。
- PC speaker 完全由 `kernel/drivers/beep.c` 用 `arch/x86/io.h` 直接编程 PIT channel 2 与 port `0x61`，时长通过 `sleep()` 计时；不得回到基于 port `0x61` refresh 位的忙等或汇编实现。
- 物理内存探测在 `kernel/dos/mm/mem.c` 内用 C 完成：先用 AC 位区分 386/486，再在一次 `irq_save()`/`irq_restore()` 临界区内 read-modify-write 设置并清除 `X86_CR0_CD|X86_CR0_NW`，探测本身用 `volatile uint32_t` 写-取反-回读并恢复原值，步长从 1GiB 逐次缩到 1/4（最小 4KiB）。该文件用 `KERNEL_NOKASAN_CFLAGS` 编译，探测循环不得引入 KASAN 插桩或打印。
- 分页由 `init_page()`（`kernel/dos/mm/page.c`）一次完成：构造 PDE/PTE 与页管理器后自己写 CR3 并以 read-modify-write 置 `X86_CR0_PG|X86_CR0_WP`；调用方不得再单独补写 WP，也不得恢复 `C_init_page` + 汇编 wrapper 的两段式实现。
- 内核入口的 boot ABI 检查是 `arch_boot_verify()`（`kernel/arch/x86/descriptor_tables.c`），只校验 CS 等于内核代码 selector，失败时直写文本 VRAM 并 `cli; hlt` 停机；不得依赖 loader 的 IDT 或 `int 0x36` 打印。段寄存器在 `arch_interrupt_init` 里紧跟 `lgdt` 重新载入内核数据段 selector，`do_init_seg_register` 这类独立入口不再存在。
- 分页启用后必须永久设置 `X86_CR0_WP`，使 ring0 写只读用户页也触发 `#PF` 并进入 COW；后续 CR0 修改必须使用 read-modify-write 保留 WP，不得写入会清除该位的固定值。
- descriptor table 与任务状态的通用入口是 `arch_interrupt_init`、`arch_task_state_init` 和 `arch_task_set_kernel_stack`；GDT/IDT/TSS 的地址、limit、布局、selector、access bits 及 `lgdt`/`lidt`/`ltr` 只能出现在 `kernel/arch/x86/` 私有实现中。首次 GDT/IDT 构造和活动 descriptor 更新必须全程保存并关闭中断；IDT 必须先完整构造全部 256 个有效入口再执行 `lidt`，`0xff` 默认入口必须可直接安全返回且不发送错误 EOI。
- 驱动通过 `interrupt_register_entry(vector, entry)` 注册函数入口；该 API 只创建 DPL0 interrupt gate，并在保存中断状态的短临界区更新 IDT。DPL3 gate 只允许由架构初始化为既有的 syscall、custom syscall 和 net API 向量创建，驱动不得自行开放用户态调用权限。PCI 驱动取得 `uint8_t` IRQ 后必须先调用 `irq_is_valid`，成功后才能计算 vector、配置路由或解屏蔽；底层 mask/config API 对非法 IRQ 安全返回。
<!-- 过时：驱动把整数地址传给本地 handler helper，或通过 `ADR_IDT`、`set_gatedesc` 直接改写 IDT 并自行选择 selector/DPL。 -->
- 内核 BIOS 调用统一使用 `arch/x86/bios.h` 的 `x86_bios_interrupt`；调用方不得准备或清理 GDT 临时项，也不得直接调用底层 raw 汇编入口。
- 汇编保存顺序与 C 结构布局构成内核内部 ABI。修改任一侧时同步检查任务初始栈、fork、signal、IDT 注册和最终 `iret` 恢复路径。
- x86 软件任务上下文统一使用 `arch/x86/task.h` 的 `arch_task_context_t`；调度器通过 `arch_task_switch`/`arch_task_start` 显式传入当前 context 槽、下一 context、CR3 和 scheduler current 槽/任务。架构汇编不得读取 `mtask` 字段偏移或全局 `current`；fork 中断帧通过 `arch_task_interrupt_return` 恢复。
<!-- 过时：`define.h` 定义通用 `stack_frame`，`mtask->esp`/`pde` 依赖 0/4 字节固定偏移，切换汇编直接读全局 `current`，fork 通过 `handlers.asm` 的拼写错误入口 `interrput_exit` 恢复。 -->
- 正在运行的内核 C 栈上切换用户态时，C 只在安全的本地对象中完成 frame 构造，最后一步调用 `kernel/arch/x86/user_return.asm` 的 noreturn helper；helper 先 `cli`，在当前 ring0 栈真实 `sub`/复制完整 frame，再原子恢复寄存器并 `iretd`。不得在 C 中把“当前 ESP 减 frame 大小”当作已预留空间。
<!-- 过时：用户态切换函数进入时立即在 `task->top - sizeof(frame)` 写入最终中断帧。 -->
<!-- 过时：C 内联 helper 只计算 `ESP - sizeof(frame)` 后直接写入，未实际调整栈指针保留空间。 -->
- 任务创建需显式记录本路径是否 clone/retain 了 PDE；失败回滚只释放自己拥有的 PDE，启动任务共享的 `PDE_ADDRESS` 不得释放。fork 只能在 PDE、VFS、FIFO 等全部构造成功后发布为 `RUNNING`。
- `create_task`/`create_thread_task` 只返回 `ALLOCATING` 任务；调用方必须在 TTY、line、FIFO、参数页等外围资源完整后调用 `task_publish` 一次发布，失败则调用 `task_abort_creation`。任务退出会取消其 waiter timer；高文本光标 owner 退出时还必须清空全局 cursor/timer 引用，不能让 timer FIFO 指向已释放的任务栈。
<!-- 过时：`create_task_impl` 在调用方写入 line、TTY 和启动参数前直接设为 `RUNNING`。 -->
- 用户态 ELF/shell heap、stack 和 `0xf0000000` 映射必须逐页检查 `page_link`；任一失败在写用户地址或进入 `iretd` 前终止任务，由任务退出统一回收已标记的部分映射。
- 物理页引用计数和 fallback owner 仅由 `kernel/dos/mm/page.c` 的正式接口维护，外部代码不得直接读写 `PAGE_INFO.count/task_id`。`task_id` 仅在 `count == 1` 时可表示独占 owner；引用从 1 增到 2 前必须清空 owner，共享页降回 1 仍保持无 owner。任务退出的 `gc` 只回收 `count == 1` 且 owner 匹配的独占页，不能把仍有引用的共享页强制归零；retain 溢出和 release 下溢属于 fatal 不变量错误。
<!-- 过时：`task_id` 可在页面变为共享后继续代表创建者，任务退出时 `gc` 通过循环递减把该 owner 的页面引用强制清零。 -->
- 用户态 ELF loader 接收真实 image size，完整验证 Ehdr/phdr/PT_LOAD、文件范围、用户虚拟范围、对齐、溢出和 executable entry。装载严格分两阶段：全部 segment 页映射成功后才复制文件数据和清零 BSS；共享物理页的相邻 segment 不重复 `page_link`。不得恢复无 size 的 `elf32_get_max_vaddr/load_elf` 接口。
- 用户程序保持 `.text`/公开链接基址 `0x70000000`。通用链接参数使用 `-N -Ttext 0x70000000`，避免 GNU ld 额外生成低于用户边界的 header PT_LOAD；因此现阶段会出现 RWX LOAD 警告，而 loader 仍要求所有 segment/page 均不低于 `USER_SPACE_START`，entry 必须落在带 `PF_X` 的 load segment 内。
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
- 内核挂载状态显式区分 `INITIALIZING`、`ACTIVE`、`RETIRED`。任务 VFS 实例和显式 `X:` 路径操作都通过挂载引用保持共享缓存存活；卸载先从活动表摘除挂载，并在最后一个引用释放后再销毁缓存。
- 每个物理盘记录 retired generation 计数和 format reservation。新挂载可与 retired generation 并存，但 format 只能在无 active/initializing 挂载、无 retired 引用时预留并在锁外执行；mount 初始化与 format 必须互斥。retired 计数只能在 `DeleteFs` 完整结束后递减，保证 format 不会与销毁并发。
- 显式盘符路径必须在保存中断状态的短临界区 acquire 活动挂载，文件系统调用在临界区外执行并在结束后 release；相对路径由当前任务实例持有引用。不得让活动挂载中的裸 `vfs_t *` 逃逸。
- 每个 mount owner 通过 `vfs_t` 内嵌 prev/next 维护动态 instance 链，包含 owner root、任务实例和 clone replay 的临时实例；完整 create 后短 irq 临界区注册，release/abort 在释放 cache 前注销。文件系统需要更新共享 cursor 时遍历 owner instance 链，不得扫描固定任务数组。
- rename 是双路径操作：源和目标都要独立 acquire/resolve，只允许同一 mount owner，并向文件系统传递去除盘符的两条路径。跨盘 rename 必须失败。
- 文件系统 `cd` 需要先准备新 cursor/path 状态，malloc 和 `AddVal` 全部成功后才 commit；`..` 必须先验证非空。目录缓存、bitmap 和 ListFile 的 list append 失败必须回滚并释放已分配项。
- 内核虚拟盘 `Disk_Read` 的物理扇区固定为 512 字节，因此当前 FAT 实现只接受 BPB `BytsPerSec == 512`。多簇目录缓冲区的每簇偏移必须使用 `ClustnoBytes`，数据簇号必须先验证 `>= 2` 且整簇落在磁盘范围内。
- FAT 表 API 的长度统一表示条目数，循环使用 `i < count`；FAT12 奇数末项、链目标、保留标记和磁盘范围必须在写入缓存前验证，损坏 FAT 使 `InitFs` 失败。新目录只写 `.`、`..` 和标准 `0x00` 终止项，不创建 `NULL` 伪文件或额外占用簇。
<!-- 过时：FAT 保存长度表示“末项偏移”并使用 `i <= length`，新目录额外创建名为 `NULL` 的占位文件。 -->
- FAT mkdir 直接持有 parent slot、child cluster 和可选的 parent-extension cluster；所有分配、list append 与 realloc 在提交 parent entry/FAT 前完成并可逆序回滚，提交后直接写标准目录簇，不得恢复 `mkfile -> 再查路径 -> del 回滚` 的间接流程。
<!-- 过时：FAT mkdir 先创建普通文件，再通过路径重新查找并改写为目录，失败时调用通用 `del` 猜测所有权。 -->
- `vfs_clone_for_task` 从源实例直接 acquire 挂载（允许源挂载已 retired），在临时实例完整重放工作目录后才替换目标实例。clone 失败必须保持目标原状，任务创建、线程、fork 和执行路径必须检查失败并完整回滚。
- 内核文件系统 `InitFs`、`CopyCache` 返回成功状态；初始化失败必须清理部分资源和挂载 reservation，不能无条件把挂载发布为 active。新增 clone/free 路径必须同步维护该所有权，不能在仍有引用时直接调用文件系统 `DeleteFs`。
- PFS format 必须在写盘前完整读取并校验 boot sector 与 `dosldr.bin`，检查长度、保留区容量和分配失败，写后校验并把真实失败传播到 VFS/psh/安装器。安装器格式化活动目标盘前先卸载，成功后重新 mount/change，失败时按逆序尽力恢复源盘。
- PFS path resolver 在每个出口都写明 error，复制路径前检查 NULL、长度和 malloc；需要 leaf name 的调用返回 parent block + 原路径 span，需要完整目录的调用解析到最终目录。`pfs_FileInfo` 在分配前验证名称容量，内核 `fopen` 的 FILE/buffer/name/read 任一步失败都逆序释放。
- 启动系统盘必须同时包含 `init.bin`、`psh.bin`、`sys.cfg`；探测失败必须 panic，不得退回可能是 DEVFS 的 `first_vdisk()`。显式盘符剥离依赖标准重叠 `memmove` 语义，修改基础内存函数后必须做冷启动验证。
<!-- 过时：系统盘文件探测全部失败后仍退回第一个虚拟盘继续启动。 -->
- 启动扇区、加载地址、ELF 入口和分区/文件系统布局属于启动 ABI；任何改动都需要完整构建和冷启动验证。
- `kernel/Makefile` 使用大量显式 `mcopy`/`mmd` 命令。重命名产物、资源或目录时同步更新所有打包位置。

## 代码风格与实现约束

- 项目自有 C/C++ 代码遵循根目录 `.clang-format`：2 空格缩进、不使用 Tab、左大括号同行、指针星号靠变量。只格式化本次触及的项目自有文件。
- 历史代码风格并不完全统一，且部分目录关闭了警告。不要借功能修改之机大面积重排代码；编译成功也不代表没有截断、越界、符号扩展或并发问题。
- 内核编译禁用宿主头文件、内建函数、栈保护、PIE、MMX/SSE，并使用 x87；C++ 禁用异常和 RTTI。新代码必须保持这些限制，不要引入依赖异常、线程局部存储、动态链接或宿主运行时的库。
- 编译参数使用 UTF-8 输入、GB2312 执行字符集。修改中文字符串、字体或终端输出时要考虑转换和字节长度，不能默认 UTF-8 字节序列会原样进入镜像。
- 优先使用仓库已有的整数类型、分配器、字符串/内存函数、锁和日志接口。引入宿主专用 API 前先确认它确实只用于 `scripts/` 或宿主工具。
- `NewList`、`AddVal`、TTY 和 MST 构造路径都可能分配失败；调用者必须在解引用前检查，失败时回滚已 append 的节点并释放 token buffer、嵌套 list 和外层对象，不得继续使用部分构造的状态。
- 该代码大量依赖 32 位指针和整数互转。新增代码应使用项目已有的 `uintptr_t`/固定宽度类型表达地址，并显式检查溢出、对齐和范围。
- 不要修改或提交 `apps/out/`、`apps/libs/`、`Loader/out/`、`kernel/obj/`、`kernel/img/`、`kernel/*.img`、`kernel/*.log` 等生成物，除非任务明确要求交付镜像或二进制。
- 不要随意执行全量 `clean`：清理规则会递归删除大量产物，之后完整重建耗时且会重新创建大型镜像。需要清理时只清理与任务相关、可确认可重建的目标。

## 提交前检查

1. 确认只改了任务需要的源码/文档，没有顺手格式化第三方目录或提交构建产物。
2. 新文件已加入对应显式对象列表、顶层应用列表或镜像打包规则。
3. ABI 改动已同步内核、用户头文件和 `libp` 包装层。
4. 运行最小相关构建；启动链、驱动、文件系统、调度或 ABI 改动再做完整构建和 QEMU 串口冒烟测试。
5. 运行 `git diff --check`，并在交付说明中列出实际执行的验证以及由于环境限制未执行的验证。
