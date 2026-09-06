# Plant OS 的 i386 与 x86_64 构建

两个架构共用 `kernel/` 下的调度器、VFS、IPC/RPC、网络和平台服务，架构后端位于 `kernel/arch/x86/{i386,x86_64}`。没有第二套 `kernel64` 内核，也没有运行 i386 程序的长模式兼容层。

## 构建与启动

```sh
# 原有 i386 磁盘与 LiveCD
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386
make -C kernel ARCH=i386 livecd

# x86_64：不需要构建 DOSLDR 或预先准备 boot.img
make -C apps ARCH=x86_64
make -C kernel ARCH=x86_64 livecd
```

x86_64 使用 GCC/G++、binutils、NASM、mtools、xorriso 和固定版本的 Limine 12.6.1。生成的 `kernel/plant-os-x86_64.iso` 同时包含 BIOS 和 UEFI 启动项。内核、应用和库分别位于 `kernel/obj/x86_64/`、`apps/out/x86_64/`、`apps/libs/x86_64/`；切换架构不会覆盖另一架构的对象。

```sh
qemu-system-x86_64 -accel tcg -cpu max -smp 4 -m 1024 \
  -cdrom kernel/plant-os-x86_64.iso -boot d -serial stdio
```

UEFI 启动可使用 `/usr/share/OVMF/OVMF_CODE_4M.fd` 和一份可写的 `OVMF_VARS_4M.fd` 副本，分别作为只读/可写 pflash drive。ISO 使用 EFI 分区及 El Torito 元数据；不能通过键盘注入绕过 Limine 的卷识别错误。

Limine MP 请求声明支持 x2APIC，允许启动器保留固件已启用、甚至锁定的
x2APIC 模式。缺失该声明会触发启动器的
`kernel does not support x2APIC and x2APIC cannot be disabled` PANIC，
此时尚未进入 Plant OS 内核。内核已有 MSR 形式的 x2APIC 后端，仅在
xAPIC 模式下映射 LAPIC MMIO；不需要为此关闭 BIOS 中的 x2APIC。

回归可运行 `python3 scripts/test-x86_64.py --apic x2apic --firmware bios`
和 `--apic x2apic --firmware uefi`；`--apic xapic` 验证另一条路径。
脚本同时核对 Limine 的交接标志和内核 APIC 模式，QEMU 不支持指定特性时
直接失败，不把静默降级后的测试算作 x2APIC 验证。

## 启动、地址空间与 ABI

| 项目 | i386 | x86_64 |
| --- | --- | --- |
| 启动协议 | DOSLDR 或 Limine Multiboot2 | Limine native，base revision 3 |
| 固件 | BIOS | BIOS、UEFI |
| 内核链接地址 | `0x280000` | `0xffffffff80000000` |
| 用户入口地址 | `0x70000000` | `0x100000000` |
| 可执行格式 | ELF32 / EM_386 | ELF64 / EM_X86_64 |
| 用户调用入口 | `int 0x36` | `syscall`，普通返回 `sysretq` |
| 浮点运算 | x87 | SSE2，binary64 `long double` |
| 默认终端 | TextMode，可选 HighTextMode | Limine framebuffer + flanterm |

x86_64 使用四级页表和 Limine HHDM，不探测写物理内存。物理页元数据按内存图分配；用户页优先从高物理地址分配，低地址保留给有 DMA 地址限制的设备。用户页按需建立，并支持 COW、共享映射和 NX；静态 loader 检查原生 ELF 的段、入口和用户地址边界；动态 PIE 的 PT_INTERP 启动独立用户态 `/lib/ld.so`，共享库解析和重定位由它完成，详见 [动态链接](dynamic-linking.md)。

x86_64 的 TLB 后端分别检测 PCID（CPUID.1:ECX[17]）和 INVPCID（CPUID.7.0:EBX[10]）；BSP 选择，AP 验证能力。每个 CPU 启用 CR4.PCIDE 前先清零 CR3 的低 12 位，并在初始化时清理引导器遗留翻译。任务切换、首次调度和临时地址空间调用统一使用 `arch_address_space_activate`：PCID 命中时以 CR3[63] 保留翻译，同根且记录有效时跳过 CR3 写入。每 CPU 的 4096 项目录记录完整页表根，只在支持 PCID 时按实际 CPU 数于 BSP 启动阶段分配，每 CPU 32 KiB；页框号映射到 12 位 PCID，冲突时刷新该标签再替换，不限制进程数量，也不向通用地址空间句柄混入 PCID 位。

页表修改经 `x64_tlb_invalidate` 批量失效：短用户范围使用 INVLPG，超过 32 页或整个地址空间使用 INVPCID single-context，无 INVPCID 时重载当前 CR3。其他 CPU 中匹配的 PCID 记录作废，在下次激活时刷新，因此迁移、fork/COW、共享映射、exec 和页表根物理页复用都不能继承旧翻译。共享的内核高半区使用 INVPCID all-context（包括 global），无该指令时切换 CR4.PGE；其他 CPU 在下次激活时处理挂起的全量失效。失效接口在 kernel lock 内使用并保留调用者 IRQ 状态；这不替代同步跨 CPU shootdown，现有任务组固定 CPU、只修改非运行目标和模块使用新虚拟地址的约束仍适用。

`dyntest.bin` 的 `DYNTEST TLB PASS` 覆盖预热页的父进程 COW、两个进程的同址隔离、连续切换、64 页批量权限修改和同址反复解除/重建映射。基础回归另覆盖共享 GUI 缓冲和模块反复装卸。`--tlb` 强制 QEMU 特性并核对串口 `tlb: pcid=... invpcid=...`，避免静默降级；PCID 可用性取决于加速器，支持这些指令的 KVM 宿主可运行：

```sh
python3 scripts/test-x86_64.py --accel kvm --cpu host --tlb pcid-invpcid --memory 6144
python3 scripts/test-x86_64.py --accel kvm --cpu host --tlb pcid --memory 6144
python3 scripts/test-x86_64.py --accel kvm --cpu host --tlb invpcid --memory 6144
python3 scripts/test-x86_64.py --tlb cr3 --memory 6144
```

PCID 要求 IA-32e 分页，i386 保留原来的 CR3 路径；`--arch i386 --dynamic` 验证共用的用户 VM 回归。

系统调用保留 Plant API 的语义编号，但使用完整的 64 位参数：`RAX` 为编号，参数为 `RDI, RSI, RDX, R10, R8, R9`。这不是 Linux ABI。入口通过 `swapgs` 取得每 CPU 内核栈，屏蔽 IF/TF/DF/NT/AC；NMI、双重故障和机器检查有独立 IST。用户中断系统调用门未开放，32 位程序会在 ELF 检查时被拒绝。

用户与内核都以 `-mno-red-zone -msse2 -mfpmath=sse -mlong-double-64` 构建。调度器在 CPU 支持时使用 XSAVEOPT64/XRSTOR64，否则使用 FXSAVE64/FXRSTOR64；BSP 选择后端，AP 验证能力。XSAVEOPT 后端启用 CR4.OSXSAVE，并在每个 CPU 设置 XCR0=3，只保存 x87 和 SSE；任务保存区为 64 字节对齐的 576-byte 标准 XSAVE 格式，任务注册表按页分配。保存区在恢复和下次保存之间保持完整，当前任务 reset 同时重置硬件状态跟踪。

临时中断及 syscall 栈帧在执行 C 代码前仍保存完整 FXSAVE64 状态并装入内核 MXCSR，返回时恢复；清零或复用的临时栈不能依赖 XSAVEOPT 跳过未修改状态。调度、fork 和信号返回保留全部 XMM、x87 和 MXCSR。FSGSBASE 未启用，用户 GS 固定为零，AVX 不属于当前 ABI。信号返回使用编号 `0x65`，检查用户 frame 后经 `iretq` 恢复完整寄存器；信号 frame 保持完整的 FXSAVE 格式，不接受用户提供的 XSAVE header。

`simdtest.bin` 验证多核切换、fork、x87、16 个 XMM、MXCSR 和显式 reset。以下 CPU 配置分别覆盖 XSAVEOPT、只有 XSAVE、没有 XSAVE；串口 `simd: context=... xcr0=...` 显示实际选择，这些配置均运行完整回归：

```sh
python3 scripts/test-x86_64.py --cpu max
python3 scripts/test-x86_64.py --cpu max,xsaveopt=off,enforce
python3 scripts/test-x86_64.py --cpu max,xsave=off,xsaveopt=off,xsavec=off,xsaves=off,enforce
# 支持 XSAVEOPT 的宿主：验证真实硬件优化，而非仅 TCG 的指令模拟
python3 scripts/test-x86_64.py --accel kvm --cpu host,enforce
```

## 显示与终端

Limine 请求 `1024x768x32`，以固件实际提供的尺寸、pitch 和颜色布局为准。x86_64 的 `set_mode(w, h)` 返回 `intptr_t`：取得当前显示、清屏并映射 framebuffer，硬件模式保持不变。程序通过 `framebuffer_info()` 取得真实布局。GUI 的目标 stride 独立于窗口的逻辑宽度，并支持 RGB 位序转换。

flanterm 自己处理 ANSI/VT100，默认 TTY 不再经过内核旧解析器。GUI 启动独立的 `term.bin`，由 os-terminal 处理终端输出；fartty 同样绕过内核旧解析器。旧解析器仅用于 i386 的传统显示后端，`VT100=0` 可以在构建时将它移除。i386 的 TextMode、HighTextMode、VGA 与 BIOS/VBE 操作继续由原后端提供，x86_64 对旧模式请求返回负状态。显示所有者退出后恢复 flanterm。

`term` 的内核后端 `fartty` 通过 IPC/RPC 请求用户态服务，协议单源定义在 `apps/include/tty_rpc.h` 和 `rpc_wire.h`。`tty_alloc(server_tid, opcode, columns, rows)` 将 TTY 绑定到同一任务组中的服务任务，返回不透明且不复用的句柄；`tty_set(tid, 0)` 恢复默认 TTY。设置、释放和输入通知均校验服务所属任务组与世代号，服务退出会关闭其 TTY 和会话。

输出、光标、清屏、滚屏、颜色区域及键盘 FIFO 查询/读取均由 term 的 RPC handler 执行。连续文本按消息容量分批，ANSI 字节不经内核解释或改写；同一 TTY 的请求串行执行，收到有效应答后更新光标状态。GUI 只管理普通窗口、合成与输入分发，终端状态和绘制归 term 所有。依赖、显式刷新和会话生命周期见 [终端](terminal.md)。

内核 RPC 调用使用保留的调用序号高位，用户态不能伪造这种请求。应答按调用序号、opcode 和双方 TID/generation 校验后直接唤醒内核等待者，不进入应用的 IPC 队列；队列已满也不妨碍返回，迟到应答被丢弃。每次 TTY 请求使用有限 deadline，超时或非法应答使该 TTY 断开，后续操作不重试。输入读取跨越 RPC 等待时用通知序号检查竞态，避免空应答覆盖已到达的按键唤醒。

`rpctest.bin` 的 `tty_rpc` 项验证批量文本、原始及分段 ANSI、光标/清屏、已满的应用队列、输入通知竞态、句柄权限、超时、迟到应答及服务退出后的会话回收。`--console` 验证独立 term 的输出协议，并通过实际键盘事件及连续像素比较验证 psh 提示符、回显、退格、滚屏及关闭后重新启动终端。

## SDL2 与桌面程序

两种架构使用同一份 `apps/sdl2` 后端，lite、Doom、invader 不再链接 `sdl2_old`。原生构建同时提供 SDL2、SDL2_ttf、SDL2_image、FreeType、zlib、libpng 和 JPEG 库。

在系统中先运行 `gui.bin`，再从 GUI 终端运行 `lite.bin` 或 `nk.bin`。Doom 与 WAD 位于 `/games`，可在该目录运行 `doom.bin`。SDL 软件 surface 直接写 GUI 提供的共享绘图缓冲，pitch 包含窗口边框；GUI 保留独立的已提交画面用于合成和遮挡恢复。Present 只按行复制 damage，并等刷新 RPC 应答后才复用共享绘图缓冲，避免 nk 清屏或绘制中的半成品帧被显示。SDL 不再额外持有第三份像素缓冲。普通 `window_refresh` 仍可合并异步更新；需要完整帧边界的程序使用 `window_present`。显示尺寸通过 `framebuffer_info()` 查询，SDL 不请求 BIOS 模式切换。每个窗口独立处理键盘前缀和鼠标状态，支持文字、方向键、滚轮、标题更新与关闭。

当前支持软件渲染、字体、图片、键鼠及纳秒性能计时；窗口尺寸固定。OpenGL/Vulkan、音频设备、SDL 线程和 `SDL_AddTimer` 的异步回调尚未实现，不能把 `SDL_INIT_TIMER` 当成受支持的线程服务。`SDL_GetTicks64`、`SDL_GetPerformanceCounter` 和 `SDL_Delay` 可独立使用。

lite 的渲染器按 surface pitch 寻址，命令缓存按结构对齐；文件查询和路径接口使用真实 VFS 结果。nk 根据实际屏幕尺寸限制窗口，字体来自 `/data/fonts/mono.ttf`。两者均可通过 GUI 窗口关闭按钮退出。

## 日期与时间

RTC 保存 UTC。读取完整、稳定的快照，解码 BCD/二进制与 12/24 小时格式，世纪寄存器直接参与年份计算。缺少世纪寄存器时使用 1970..2069 年窗口，不再额外偏移年份。`time()` 返回 Unix 秒；`gmtime` 使用 UTC，`localtime`/`mktime` 使用当前固定的 UTC+08:00。`tm_year` 从 1900 起算，`tm_yday` 从 0 起算。GUI 每秒更新一次；单调计时仍由 HPET/内核时钟提供。

## 程序与验证

原生构建生成 ELF64 程序，包括 init、psh、GUI、Lua/luac、lite、nk、Doom、kcube、invader、minewep、bitz、C4、Duktape、MY-BASIC、NASM/ndisasm、压缩与网络工具，以及架构、SSE、时间、SDL、异常、IPC/RPC 和磁盘生命周期回归程序。全部正式应用为动态 PIE，公共 C/C++ 运行库分别为 `libp.so`、`libcpp.so`，由独立的静态自举 `ld.so` 装载；两种语言使用同一入口与初始化/退出生命周期；`cpptest.bin` 验证全局/局部静态对象、析构、对齐及退出回调。已接入的应用也可单独构建，例如 `make -C apps/gui ARCH=x86_64`。尚未移植的第三方程序不能复用 i386 二进制或静态库；两种架构的公共构建规则在 `apps/build.mk`，应用及依赖在 `apps/native-apps.mk`。SDL、lite、Doom 和 NASM 的源文件清单与 i386 共用。

`setup1` 的 DOSLDR 安装流程、`tcc`/`tccinst` 的编译器与运行时工具链、`fputest` 的 x87 探针仍仅面向 i386；它们随 i386 全量构建保留。生成 ELF64 不表示所有旧演示程序的 BIOS 显示功能可用：这些调用在 x86_64 仍按约定返回错误。

原生内核模块使用 ELF64 RELA，示例为 `hello.mod`。模块管理与 i386 共用实现，x86_64 将模块映射到独立地址，重定位后设置 W^X。当前 KASAN/PERF 诊断仍只支持 i386；长模式不启用使用固定低地址 DMA 缓冲的旧软驱驱动。

```sh
# 包含 4 GiB 以上物理页的完整基础回归
python3 scripts/test-x86_64.py --firmware bios --memory 6144

# UEFI 与超过 255 个任务的持续 GUI 刷新
python3 scripts/test-x86_64.py --firmware uefi --capacity

# 单 CPU 路径
python3 scripts/test-x86_64.py --cpus 1 --memory 512

# 验证真实 PS/2 移动、左右键、滚轮及共享事件坐标
python3 scripts/test-x86_64.py --mouse --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --mouse --memory 512

# C4 的原生指针/虚拟机、NASM ELF 输出与 JavaScript 对象
python3 scripts/test-x86_64.py --tools --firmware uefi

# SDL2 帧提交隔离、局部 damage、遮挡恢复、字体和键鼠输入
python3 scripts/test-x86_64.py --sdl --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --sdl --memory 512

# 保存程序截图并通过鼠标关闭；lite 验证编辑保存，nk 连续采样检查闪烁
python3 scripts/test-x86_64.py --desktop-app lite --firmware uefi
python3 scripts/test-x86_64.py --desktop-app nk --firmware uefi

# 检查 GUI 终端提示符、键盘回显、退格和滚屏后的实际像素
python3 scripts/test-x86_64.py --console --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --console --memory 512
```

脚本使用串口判定结果，临时修改后恢复 `kernel/res/init.mst`，并在结束时恢复正常启动 ISO。可用 `--out` 指定日志目录、`--timeout` 调整慢速 TCG 的期限；不使用 `sendkey`。
