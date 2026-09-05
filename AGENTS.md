# Plant OS 仓库协作指南

## 注意
继续重新审视当前代码，还有没有可以优化，整合，删除你新增的和原有的冗余代码。进一步精简你的实现，使得你的实现更加内聚，而不是到处打补丁。你的实现应该是正确的，标准的，高性能的。你应该思考让你的代码精简优化，改动小，简洁优雅，符合最佳实践。你的实现应该是高性能的。不要创建无意义的自由函数，思考尽量不要或减少新增无意义的成员变量。不要兼容旧的遗留的实现，而是删除替换旧的实现。你的实现应当有通用性与扩展性，避免硬编码与限制。消除嵌套，消除强耦合，消除不必要的短小函数，用较为oop的设计，使用struct+enum等进行抽象和封装，禁止考虑向后兼容。适当拆出变量，从而更加清晰且减少嵌套。对于每个功能的实现，优先思考更精简优雅的写法，再考虑适当拆除变量从而提升可读性。你的代码更加精简优雅，规范且标准，效率高，结构清晰整洁，符合最佳实践。完全与各方面解耦。避免到处打补丁的做法，从架构和设计上使用更精简优雅的实现。做完之后，需要重新补充AGENTS.md

<!-- 过时：通过修改 process_spawn_first_user 注入自动测试命令。当前统一临时修改 kernel/res/init.mst，并在测试后恢复；禁止使用 sendkey。 -->



## 项目定位

- 本文件适用于整个仓库。
- Plant OS 是用于学习操作系统原理的多架构操作系统，当前支持 i386 保护模式和 x86_64 长模式；i386 保留 BIOS/FAT 磁盘链，x86_64 使用 Limine 原生协议从 BIOS 或 UEFI 启动。它不是宿主系统上的普通应用。
- 内核、加载器和用户程序都是 freestanding 代码：不能默认使用宿主 libc、线程库、文件系统语义或现代 CPU 运行时。
- 两个架构的有效实现都在 `kernel/`，共用任务、VFS、IPC、网络和设备服务。仓库中没有 `kernel64/`；`.gitignore`、`doc/README_zh-cn.md` 和 `scripts/build_rootfs.sh` 中残留的 `kernel64` 内容属于旧版本痕迹，不要据此创建或修改 64 位实现。
- 构建文件和当前源码比 README 中的历史描述更可信。发现两者不一致时，以实际 `Makefile` 和调用链为准，并在修改中指出差异。

## 启动与运行模型

- `kernel/boot/` 生成现有磁盘启动扇区；`loader/` 生成 `loader/out/dosldr.bin`，链接地址为 `0x100000`，入口为 `loader_main`；加载器再寻找并装载 `kernel.bin`。这条链继续由 `boot.img`/`img_run` 使用。
- i386 内核生成 `kernel/obj/kernel.bin`，链接地址为 `0x280000`。ELF 入口是 `kernel/arch/x86/i386/boot_entry.asm` 的 `x86_boot_entry`：它同时接受现有 DOSLDR 入口状态和 Limine Multiboot2 状态，建立固定的平坦 GDT/内核栈后调用 `KernelMain`（`kernel/dos/init/main.c`）。Multiboot2 header 必须保留在文件前 32 KiB 内，链接布局由 `kernel/arch/x86/i386/kernel.ld` 固定，并断言内核不覆盖 `0x400000` 的 bootstrap 页表。
- i386 Limine LiveCD 默认直接加载 `kernel.bin` 和一个 FAT initramfs module，不经过 DOSLDR。x86 早期入口依据 Multiboot2 memory map 把 module 搬到 KASAN shadow 之后的首个可用物理区；分页初始化后必须立刻经正式 page API 保留这些页，内存探测不得改写 module。initramfs 注册为可写但不持久化的 `R:` 虚拟盘，系统启动只挂载该盘作为根。Limine 菜单的第二项只 chainload 第一块硬盘上的现有 Plant OS 磁盘引导器。
- i386 用户程序链接到 `0x70000000`，x86_64 用户程序链接到 `0x100000000`，链接入口均为 `Main`。`apps/libp/entry.c` 或 `apps/libp/cppstart.cpp` 完成运行时初始化后调用应用自己的 `main(argc, argv)`。
- 内核与应用共享的是项目自定义 ABI，不是 Linux ABI。地址、结构体布局、寄存器约定和中断号都可能是兼容性边界；不要随意改成宿主平台惯例。

## 目录地图

- `kernel/boot/`：启动汇编。
- `kernel/dos/`：内核初始化、任务、IPC、系统调用和与硬件无关的内核服务。
- `kernel/drivers/`：存储、网络和设备类别适配；PC 总线、输入、显示、声音、时钟等实现位于 `kernel/platform/pc/`。
- `kernel/fs/`：FAT、PFS、ISO9660、VFS、ELF 加载及路径/文件实现。
- `kernel/io/`：文本/图形显示、TTY、输入栈和日志。
- `kernel/net/`：lwIP 2.2.1（`third_party/lwip`）、`net_stack.c` 的链路/DHCP 生命周期和 `socket.c` 的用户态 socket 端点实现；网卡帧适配位于 `kernel/drivers/network.c`。
<!-- 过时：`kernel/cmd/` 保存内核命令实现。 -->
- `kernel/cmd/`：系统调用到用户态 `apps/psh` 命令模式的适配层。不得恢复旧内核 `if/else` 命令解析器及其 `chat`、`netgobang` 实现；构造执行请求时 `argv[0]` 必须是实际 shell `psh.bin`。
- 用户可见的磁盘重挂载命令唯一名称是 `remount_drive`，接受单字母盘符或常规 `X:` 写法并在内部规范化为大写盘符；仓库调用方统一使用 `remount_drive X:`，不得保留 `rdrv` 别名。
<!-- 过时：用户态 shell 及安装工具通过 `rdrv` 重挂载磁盘，并由调用方直接传递未经校验的盘符。 -->
- `apps/psh` 中严格无参的简单内建命令（`cls`、`mem`、`pause`、`lsmod`）通过只读命令表分派；`dir` 独立接受零或一个目录参数，零参数传空路径枚举当前目录，有参数则直接传给 `list_directory`。有参命令继续按各自语义解析和校验，不要把不同参数模型硬塞进同一处理表。
<!-- 过时：`apps/psh` 的所有简单无参内建命令都在主命令函数中使用连续 `if/else` 分派。 -->
- `apps/psh -c` 直接把 `argv[2..]` 作为命令 argv 交给统一分派，不拼接后重新解析。交互输入也使用 `apps/libp/runtime_args.c` 的同一 quote/backslash-aware parser；所有内建命令校验 exact argc，外部命令通过共享的可逆 builder 生成执行命令行。外部命令解析只接受常规文件，路径存在但为目录时必须继续尝试补 `.bin` 和搜索 `path`，因此 `/tcc` 目录不能遮蔽 `tcc.bin`。
<!-- 过时：`psh -c` 只接受一个不含空格的命令参数。 -->
<!-- 过时：`psh -c` 把 `argv[2..]` 用裸空格拼接成字符串，再交给 shell 的第二套解析器。 -->
- `apps/psh` 与 `apps/lua` 的交互行编辑统一使用 `apps/third_party/pl_readline` 及其共享 Plant OS 按键适配；适配层必须把 `KEY_INPUT_*` 方向键及 Enter、Backspace、Tab 映射为库按键，只丢弃 `getch()` 返回的 `0` 与其他无字符控制值，不能再把它们写进命令行或恢复另一套本地编辑器。psh 的补全词表只含内建命令，首词统一用 `PL_COLOR_CYAN` 染色，文件路径暂不参与补全；Lua REPL 只使用行编辑与历史，以 `PL_ENABLE_INTELLISENSE=0` 配合普通重绘对象，不链接补全和高亮代码。该移植版将内部 `pl_list_*` 名称空间化以避开 MST 的链表 API，并依赖文本与高文本 TTY 保持标准 CR 和 `CSI K` 语义。
- 标准 PS/2 键盘扫描码只投递给当前前台 TTY 上拥有有效 key FIFO 的进程；TTY 所有权是唯一前台判据，不再额外依赖容易滞后的 `state` 或 `fifosleep`。同步前台子进程运行时父 shell 通过清空 TTY 明确交出前台，子进程退出后恢复 TTY 即恢复输入。`getch()`/`input_char_inSM()` 必须使用 `WAIT_REASON_KEYBOARD` 阻塞并由 IRQ 唤醒，禁止在持有 kernel lock 时忙等；ready 握手必须覆盖扫描码在发布等待前到达的竞态。不得重新广播给所有 `RUNNING` 任务，尤其不能向每 CPU idle task 的空 FIFO 写入。
- PS/2 控制器与鼠标协商期间保持 IRQ1/IRQ12 屏蔽，命令必须使用单调时间 deadline 并逐条消费 ACK/设备 ID，完成同步初始化后才解屏蔽；不得恢复固定次数忙等或让 IRQ 与轮询方竞争响应字节。GUI 输入线程通过 `input_wait()`/`SYSCALL_INPUT_WAIT`（`0x64`）一次等待鼠标、按键按下与松开 FIFO，使用 `WAIT_REASON_INPUT` 发布等待并由对应 IRQ 的 ready 握手唤醒；不得用多个 pending syscall 加 `api_yield()` 轮询。
- `AddThread(name, entry, stack_top, argument)` 显式接收线程参数和调用者分配的栈顶；调用者不能手写 cdecl 栈槽或减去固定字节数。内核在发布线程前校验入口和栈，架构入口建立对齐的 C 调用帧：i386 把参数放在返回地址之后，x86_64 使用 RDI。系统调用包装必须完整传递四个参数，线程进入用户态后自行退出，不能返回到空返回地址。
- `kernel/mst/`、`kernel/std/`、`kernel/modules/`：MST 脚本、基础运行库和可加载模块。
- `kernel/include/`：内核公共声明；很多模块通过 `dos.h`、`define.h` 等大头文件耦合。
- `kernel/include/arch/x86/`：共享 x86 声明（例如 CPUID 与端口 I/O）；当前 i386 的中断帧、入口和其他 ABI 声明位于 `kernel/include/arch/x86/i386/`，通用内核头文件不应重新定义这些布局。
- `kernel/arch/x86/common/`：可由 x86 变体共享的中断控制器实现；`kernel/arch/x86/i386/`：当前 32 位后端，包括启动入口、异常/系统调用 stub、BIOS 实模式切换、任务上下文、分页和用户态返回。`kernel/platform/pc/` 保存 PC 固件与设备实现，VBE 的 BIOS 线布局仅在该目录私有的 `vbe.h` 中定义，高层视频访问统一走 `platform_video_*` 接口。通用内核代码通过 `arch.h`、`platform.h`、`irq.h` 等接口访问这些后端，不直接依赖具体布局；x86 汇编入口均位于架构目录，`kernel/boot/` 的启动扇区除外。GDT、IDT、TSS、selector 和 BIOS 实模式切换所需的临时 descriptor 由 i386 后端私有持有；通用内核代码的新临界区通过 `irq_state_t`、`irq_save()`、`irq_restore()` 接口访问中断状态。

### 当前多架构边界

- 当前可构建目标是 `ARCH=i386` 或 `ARCH=x86_64`，平台为 `PLATFORM=pc`。i386 标准顺序为 `make -C apps ARCH=i386`、`make -C loader`、`make -C kernel ARCH=i386`；未完成的架构必须由构建文件明确报错。
- i386 专属代码集中在 `kernel/arch/x86/i386/`，可共享的 x86 代码集中在 `kernel/arch/x86/common/`，PC 设备实现集中在 `kernel/platform/pc/`。新增通用内核逻辑应依赖架构/平台接口，而不是重新引入 x86 内联汇编、固定指针宽度或 PC 端口常量。
- 用户态运行库的汇编入口和 x87 数学原语位于 `apps/libp/arch/i386/`；仅供架构回归程序使用的汇编也按应用放在对应的 `arch/i386/` 子目录（例如 `apps/fputest/arch/i386/`）。
- x86_64 后端位于 `kernel/arch/x86/x86_64/`，产物分别写入 `kernel/obj/x86_64/`、`apps/out/x86_64/`、`apps/libs/x86_64/`，不能与 i386 混用。构建入口是 `make -C kernel ARCH=x86_64 livecd`，生成 `kernel/plant-os-x86_64.iso`，不依赖 DOSLDR 或已有 `boot.img`。实现说明见 `doc/multiarch.md`。
- x86_64 只接收 Limine native/base revision 3 的 HHDM、内存图、framebuffer、RSDP、MP 和 FAT module；四级页表根据内存图建立，不能探测写 RAM、扫描 BIOS RSDP 区或调用实模式服务。用户 ELF 必须是 ELF64/EM_X86_64，拒绝 ELF32、PT_INTERP、PT_DYNAMIC、段重叠及 W+X 段。
- x86_64 通过 `syscall`/`swapgs`/`sysretq` 进入与返回内核，IDT 不开放用户中断系统调用。GS 的前 16 字节固定为内核/用户栈槽，用户 GS 为零，禁止启用 FSGSBASE；NMI、双重故障、机器检查使用独立 IST，并检查 GS 基址处理 SWAPGS 过渡窗口。返回前校验 canonical 用户 RIP/RSP、CS/SS、RFLAGS 与 MXCSR。
- x86_64 的 C/C++ 浮点 ABI 使用 SSE2，`long double` 为 binary64；两侧均使用 `-mno-red-zone -msse2 -mfpmath=sse -mlong-double-64`，不启用 AVX/OSXSAVE。每次入口先保存完整 FXSAVE64 状态，再装入内核 MXCSR；调度、fork 和信号返回保留 16 个 XMM 寄存器及 MXCSR。不得在共享代码恢复 x87 内联数学汇编，i386 的 x87 后端仍独立保留。
- 默认 TTY 由构建选择的 `tty_console_create()` 创建；i386 由 TextMode 创建，HighTextMode 继续可选，x86_64 由 flanterm 创建。前台 TTY 直接以 `tty_default` 为准，不再按显示模式扫描列表。flanterm 的 `native_ansi` 输出直接送给自身解析器，TTY 输出层把 LF 转为 CRLF，不能依赖其未实现的 `CSI 20h` 换行模式；`VT100=0` 可以裁掉旧解析器，保留旧 GUI 文本 TTY 时仍应使用默认 `VT100=1`。该选项必须纳入构建配置指纹。
- x86_64 显示尺寸由 Limine 请求 `1024x768x32`，实际尺寸、pitch 和 RGB 位位置以响应为准。`set_mode` 返回 `intptr_t`，只清屏、取得显示所有权并映射当前 framebuffer，不修改硬件模式。`framebuffer_info()` 返回实际布局；GUI 合成器区分逻辑宽度与目标 stride，并转换 RGB 位序。内核和用户 framebuffer 别名必须保留相同 PAT 缓存属性；所有者退出后恢复 flanterm。VGA 320×200、80×25 和 BIOS/VBE 模式编号操作在 x86_64 返回负状态，不能伪造成功。
- x86_64 的旧 ISA 软驱驱动当前通过既有 `__NO_FLOPPY__` 开关禁用；不要把其 `0x80000` 固定 DMA 缓冲作为长模式虚拟地址使用。尚未移植的第三方应用和 KASAN/PERF 配置必须明确报错，不能装入或链接 i386 产物。
- 模块管理位于 `kernel/modules/loader.c`，通过 native ELF 类型访问各架构布局：i386 使用 REL，x86_64 使用 RELA。x86_64 模块使用独立内核映射，重定位后设置 W^X 权限；导出表只能发布已经填充的条目，禁止在重复名称查询中遍历未初始化的名称指针。
- i386 Limine LiveCD 通过 Multiboot2 直接加载 `/boot/kernel.bin` 与 FAT initramfs；DOSLDR 仅保留给现有磁盘启动链、格式化资源和菜单 chainload，不是 LiveCD 的默认内核加载器。
<!-- 过时：`interrupt_disable/get_interrupt_state/set_interrupt_state` 在 `kernel/dos/task/lock.c` 中实现并从 `dos.h` 暴露。 -->
<!-- 过时：GDT/IDT 地址、descriptor/TSS 布局和 `set_segmdesc`/`set_gatedesc`/`load_*` 从 `define.h`、`dos.h` 暴露给通用代码。 -->
- `kernel/res/`：打包进镜像的资源；资源是否进入镜像由 `kernel/Makefile` 中显式的 `mcopy` 命令决定。
- `loader/`：独立的加载器，包含自己的驱动、文件系统和基础库实现。不要假设它能直接复用内核实现。
- 内核 PCI API 位于 `kernel/include/pci.h`：`pci_initialize()` 只进行一次基于 multifunction function 与 PCI-to-PCI bridge 的拓扑枚举，动态设备表保存结构化 BDF/class 信息，IDE、AHCI、PCnet 与 RTL8139 均通过 `pci_find_*` 查询并使用统一 BAR/command API。不得恢复 `PCI_ADDR_BASE` 原始配置空间镜像、扫描全部 256×32×8 function，或在具体驱动中再次穷举 PCI 空间。
- DOSLDR 的软驱驱动必须区分控制器、介质与可读块设备：无控制器或无介质时仍以 `VDISK_TYPE_UNAVAILABLE` 保留 A: 槽位，保证后续保留槽与首块 IDE 磁盘继续落在 B:/C:，但 `DiskReady()` 只能对具有读回调的 `VDISK_TYPE_BLOCK` 返回真。FDC disk-change 位必须通过强制步进/重新校准后复检来判定介质；无介质直接返回并清零失败读缓冲，禁止递归重进 `fdc_rw`。所有 IRQ 等待必须有界，介质移除后注销软驱，不能无限忙等或继续把陈旧 DMA 缓冲当作有效扇区。
- `apps/include/`：用户态头文件和公开 ABI。
- `apps/libp/`：用户态 C/C++ 启动代码、系统调用、内存/标准库、IPC/RPC 等基础库。
- `apps/<name>/`：各个用户程序；通常每个目录有自己的 `Makefile`，产物写入 `apps/out/`。
- `apps/sdl2*`、`apps/freetype/`、`apps/libpng/`、`apps/jpeg/`、`apps/doomgeneric/`、`apps/lite-1.11/`、`apps/nasm-master/` 等包含大量移植或第三方代码。除非任务直接涉及，不要做全目录格式化或机械重写。
<!-- 过时：`kernel/a.iso`、`kernel/cd.iso` 和 `kernel/iso/psh.bin` 属于当前启动链使用的 ISO 镜像与运行时程序。 -->
- `font/`、`kernel/res/` 包含当前镜像使用的运行时二进制资源；`kernel/iso/` 中只有 `modules/*` 被当前 `kernel/Makefile` 打包，其余内容是未接入当前 32 位 `Mimg` 流程的旧 ISO 暂存资源。不要重新提交 `a.iso`、`cd.iso` 或 `iso/psh.bin` 等旧生成物。
- `chat/`、`netgobang/`、`fattools/` 是宿主侧辅助/演示程序，不属于内核或 Plant OS 用户态 ABI。
- `scripts/kernel-perf.py` 严格解析内核输出的 version 2 聚合采样，可从同一串口日志选择任意完整采样段，并直接生成 folded stacks 与无外部依赖的交互式 SVG 火焰图。SVG 栈帧点击缩放时必须按新宽度重新布局并重算标签，悬停详情保留完整名称，`Reset Zoom`/`Esc` 恢复原图；不得退回只把整张图等比放大、标签仍固定截断的静态实现。`scripts/build_rootfs.sh` 当前引用已不存在的 `kernel64`，不是 32 位主构建流程的一部分。
- `scripts/build-livecd.sh` 的 i386 分支从当前 `boot.img` 提取通用运行时文件，使用当前 `kernel.bin` 与 `apps/out/*.bin` 覆盖构建产物后生成 FAT initramfs；`boot.bin`、`boot32.bin`、`boot_pfs.bin` 与规范名称 `DOSLDR.bin` 必须作为 FAT/PFS 格式化及硬盘安装资源保留，但不得把 DOSLDR 或整个 `boot.img` 配置为 Limine 默认启动 module。应用默认位于根目录，`doom.bin` 与 `doom1.wad` 位于 `/games`，`apps/lite-1.11/data` 整体复制到 `/data`，不得手工枚举其 core、fonts、plugins、user 子目录。TCC 文件沿用 `tcc.img` 布局：`apps/include` 完整复制到 `/tcc/include`，静态库放入 `/tcc/lib`，`libtcc1.a` 单独放入 `/tcc/inst`，`apps/tcc` 构建的 `crti.obj` 作为 `/tcc/crt/crti.o` 供 LiveCD 直接链接，根目录的 `crti.c` 继续供 `tccinst.bin` 使用。脚本最后用固定校验值的 Limine 12.6.1 构造 BIOS ISO。
- LiveCD 的 `setup.mst` 不使用静态文件清单：initramfs 首次填充后，`scripts/build-livecd.sh` 必须通过 `mshortname` 从镜像本身取得每个路径的真实 FAT 别名，再自动生成覆盖全部目录和文件的清单并写回镜像。每个条目记录源 FAT 短路径、PFS 原路径和 FAT 8.3 目标路径；`DOSLDR.bin` 必须是第一个文件条目，`kernel.bin` 与 `setup.mst` 本身也必须包含。`setup1.bin` 从当前源盘复制到 C:，继续允许用户选择 FAT/PFS：FAT 使用清单短名，PFS 保留原名，不得因存在长文件名而强制 PFS，也不得恢复 A:/多软盘 `next` 流程。

## 构建环境与命令

需要 GNU make、支持 `-m32`/`elf_i386` 的 GCC/G++ 与 binutils、NASM、mtools（`mformat`、`mcopy`）和 QEMU（`qemu-system-i386`、`qemu-img`）。LiveCD 还需要 `curl`、`tar` 以及 `xorriso` 或 `genisoimage`。代码还假定编译器支持 freestanding、无 PIE 的 32 位输出。

干净工作区的标准构建顺序是：

```sh
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386
```

顺序很重要：

- `apps` 先生成 `apps/libs/*.a` 和 `apps/out/*.bin`。
- `loader` 生成内核制镜像时需要的 `loader/out/dosldr.bin`。
- `kernel` 最后编译内核、模块并创建/填充 `kernel/boot.img`、`kernel/disk.img` 和 `kernel/img/*.img`。
- `make -C kernel full` 会构建应用，但仍假定加载器和部分已有产物可用，不能替代上面的干净构建顺序。
- 标准构建完成后，`make -C kernel livecd` 生成 `kernel/plant-os-livecd.iso`；首次构建会下载并校验固定版本 Limine。`make -C kernel livecd_run` 从 CD 启动并把 `boot.img` 作为第一硬盘附加，以便菜单第二项验证现有引导链。

常用的局部构建方式：

```sh
# 已经完成过一次 apps 全量构建后，重编单个应用
make -C apps/rpctest ARCH=i386

# 只重编加载器
make -C loader

# 已存在 kernel/obj 时，快速检查某个内核子系统
make -C kernel/dos/task ARCH=i386
make -C kernel/drivers ARCH=i386
```

最终仍应运行与改动范围相称的上层构建，因为对象列表、链接和镜像打包都由显式规则控制。

内核构建支持以下诊断开关：

```sh
make -C kernel KASAN=1
make -C kernel PERF=1
make -C kernel BENCH=1
make -C kernel MEMTEST=0 MEMSIZE_MB=512
```

- i386 的 `PERF=1` 会启用内核 CPU 采样、保留 frame pointer 并关闭 sibling-call 优化；`PERF_STACKS` 可调整固定哈希表槽数，必须是至少为 4 的 2 次幂，默认 4096。`kernel/obj/.build-config` 跟踪会影响代码生成的诊断开关，在普通、KASAN、PERF 等配置间切换时必须依靠它重编对象，不得退回复用不同编译参数旧对象的做法。
- 启动 timer benchmark 默认关闭；只有显式 `BENCH=1` 才比较 PIT 与 TSC-deadline。benchmark 结果不参与运行时计时，不得恢复无消费者的 `base_count`、普通启动忙等或关闭 benchmark 后反而等待 100 tick 的 fallback。
- `MEMTEST=0` 必须同时提供 `MEMSIZE_MB`。
- 这些命令仍依赖已经构建好的应用和加载器产物。

## 运行与验证

- 仓库没有统一的宿主侧自动化测试套件。最低验证要求是：相关局部构建成功；涉及 ABI、链接、启动或镜像内容时再执行完整构建和 QEMU 冒烟测试。
- 当前 `Mimg` 规则生成并填充 `boot.img`/`disk.img`。`make -C kernel img_run` 使用这套镜像并启动 QEMU，是与当前制镜像流程最一致的现成目标。
- LiveCD 改动至少同时验证 `kernel.bin` 只有规范的 `PT_LOAD` program headers、Multiboot2 header 位于前 32 KiB、默认 Limine+initramfs 路径冷启动到 `init.bin`/`psh.bin`，以及菜单硬盘 chainload 项仍能通过现有 DOSLDR 启动。自动选择菜单项时临时改 `kernel/res/limine.conf` 的 default entry，测试后立即恢复；仍禁止 `sendkey`。
- `make -C kernel run`、`full_run`、`kernel/run.sh` 和 `bochsrc.txt` 仍引用 `kernel/img/Powerint_DOS_386.img`，但当前 `Mimg` 不生成该文件；除非任务专门修复旧软盘流程，不要把这些目标成功与否当作唯一验证标准。
- 现成运行目标使用 KVM/`-cpu host`。在没有 KVM 的环境中，应基于同一镜像手动运行 QEMU，并去掉 `-enable-kvm -cpu host`，而不是修改内核来迁就宿主环境。
- QEMU 命令使用 `-serial stdio`；启动、崩溃和测试输出优先从串口收集。不要只依赖图形界面现象。
- 需要自动运行系统内命令时，临时修改 `kernel/res/init.mst`，构建并测试后立即恢复该文件。禁止用 QEMU monitor 的 `sendkey` 注入命令。
- IPC/RPC 改动可在系统中运行 `rpctest.bin`；磁盘和任务生命周期相关改动可结合 `dktest.bin`。两者都已由 `kernel/Makefile` 打包进主镜像。
- GUI RPC 与共享映射改动可运行 `guitest.bin`：它 fork 出 `gui.bin`，验证服务发现、窗口创建、共享 framebuffer、异步刷新、键盘队列控制与关闭回收，并在串口输出 `GUITEST PASS`。高任务数与多客户端刷新回归使用 `guitest.bin stress`：它保持 256 个负载进程和 48 个窗口，确保系统实际越过旧的 255 TID/页引用边界，再从两个全新的 exec 地址空间按 50ms 周期持续刷新；任一窗口的共享 damage 连续 2 秒未被消费即失败，成功时输出 `GUISTRESS PASS`。无 KVM 的慢速 TCG 环境可使用同样越过 255 边界、但只保留两个刷新窗口的 `guitest.bin capacity`。自动运行时仍临时修改 `init.mst`，测试后立即恢复。
- `guitest.bin` 还通过真实线程和 IPC 验证原生宽度的入口参数及非法入口/栈拒绝，输出 `GUITEST THREAD PASS`。鼠标修复必须运行 `guitest.bin mouse`，按 `GUIMOUSE READY` 中的原点和目标注入相对移动、左键、右键和向上滚轮，检查共享事件中的精确坐标及 `GUIMOUSE PASS events=15`；仅通过窗口/RPC 回归不能证明输入可用。`scripts/test-x86_64.py --mouse --firmware bios`/`--firmware uefi` 自动执行此测试并保存移动前后截图；`--arch i386 --mouse --memory 512` 复用同一测试验证 i386。测试只用 QMP `input-send-event` 注入鼠标，仍禁止 `sendkey`，结束后恢复 `init.mst` 和正常启动镜像。
- 两种架构的异常入口与用户异常退出均可在系统中运行 `exc_test.bin`，它依次验证 `#DE`、`#UD`、`#GP`、`#PF` 的子进程退出状态；该程序同样已打包进主镜像。
- SMP FPU 保存、恢复、fork 快照与迁移回归使用 `fputest.bin`：它按 CPU 数创建多个独立进程，在 x87 栈保留哨兵值后连续主动让出 CPU，并校验多次切换及 fork 两侧的完整状态；成功时串口输出 `FPUTEST PASS`。

<!-- 过时：旧文档中的 kernel64 构建、运行和 rootfs 流程；当前仓库只有 kernel/ 下的 32 位内核。 -->
- PERF 内核在启动早期自动开始 boot session，并持续采样到用户明确执行 `perf stop`；禁止再根据进入 `psh.bin` 或其他程序猜测启动完成并自动停止。psh 的 `perf start`、`perf status`、`perf stop` 通过 `SYSCALL_PERF_CONTROL`（`0x63`）控制会话，`stop` 总是结束当前活动 session 并把结果写入串口，停止后可再次 `start`。IRQ 采样路径只允许使用固定聚合表，不得分配、阻塞或输出日志；聚合键保留 CPU、TID/generation 与内核调用栈，用户态时间统一记录为 `[user]`。内核 `PERF_BEGIN version=2` 格式必须携带 linker 提供的内核文本边界供宿主校验；该格式、`apps/include/perf.h` 的定长控制 ABI 和宿主解析器必须同步修改，不保留 version 1 原始逐样本格式兼容层。
- 性能采样的完整说明见 `doc/performance.md`；最简转换命令为：

```sh
python3 scripts/kernel-perf.py \
  --kernel kernel/obj/kernel.bin \
  --serial path/to/serial.log \
  --out path/to/perf.folded
```

- x86_64 自动回归使用 `python3 scripts/test-x86_64.py --firmware bios --memory 6144` 和 `python3 scripts/test-x86_64.py --firmware uefi --capacity`。脚本临时写入并恢复 `init.mst`，验证 ELF32/旧中断入口拒绝、旧显示 API 返回错误、固定 framebuffer、SSE/fork、用户异常、IPC/RPC、磁盘生命周期、回环网络、GUI、原生模块及 Lua，最后恢复正常 ISO。`--capacity` 实际越过 255 TID，执行持续刷新；UEFI 使用 OVMF，不得通过 sendkey 跳过引导警告。
- C/C++ 入口共享静态初始化生命周期；原生链接脚本保留对齐的 `.init_array`/`.fini_array`，全局对象在 main 前构造，退出回调及析构逆序执行。`atexit` 和 `__cxa_atexit` 共用可扩容回调表，`__dso_handle` 必须是数据对象，本地静态初始化使用真实 guard，不得恢复无参空函数占位。`cpptest.bin` 覆盖构造、析构、guard、对齐与回调扩容。
- GUI 字体舍入使用共享 `roundf`，不得用依赖 x87 扩展精度的 `x + 2^52 - 2^52` 实现单精度舍入。文本绘制按真实字形边界和图层尺寸裁剪，禁止恢复固定 512×128 位图及未经检查的负 bearing 偏移。
- 内核格式化集中在 `kernel/std/format.c`，`snprintf`/`vsnprintf` 必须遵守容量，`long`、`long long`、`size_t`、指针按实际架构取参；不得恢复返回空指针的 64 位十进制转换或共享静态格式化缓冲。
- 仅修改文档时至少运行 `git diff --check`。不要为了文档改动重建大型磁盘镜像。

## 跨层修改规则

### 网络栈

- 网络协议的唯一实现是 `kernel/net/third_party/lwip`（上游 lwIP 2.2.1）加 `kernel/net/net_stack.c`/`socket.c`。禁止恢复手写 ARP、IPv4、ICMP、UDP、TCP、DHCP、DNS、HTTP、FTP 或第二套协议状态机。
- 当前端口使用 `NO_SYS=1` 和 lwIP raw API：网卡 IRQ 交付完整、无 FCS 的以太网帧，`net_stack_tick()` 在时钟中断中驱动 lwIP timeout 并轮询 lwIP 原生 loopif，`socket.c` 只把 raw callback 封装为用户端点。`net_stack_initialize()` 无条件创建 `lo`（`127.0.0.1`），与以太网/DHCP 生命周期分离；本机 IPv4 输出必须等 lwIP output 调用完全返回后再通过 `net_stack_poll_local()` 立即 drain 回环队列，时钟轮询仅作兜底，不得手写回环包或从 raw-API output callback 直接重入 `ip4_input`。调用 raw API 的普通内核路径必须以 `irq_save()`/`irq_restore()` 串行化，不能引入未受保护的 netconn/socket 线程层。
- `kernel/drivers/network.c` 只负责选择链路驱动；PCnet/RTL8139 只负责 PCI、DMA、寄存器和 IRQ。驱动必须报告实际接收长度，并接收不含 FCS 的发送帧；不得恢复 `Card_Recv_Handler`、`netcard_send`、IP 缓存、DHCP 忙等或驱动内协议解析。
- 用户态网络 ABI 是 `apps/include/socket.h` 的 `socket`/`bind`/`connect`/`listen`/`accept`/`sendto`/`recvfrom`/`setsockopt`/`socket_close`，句柄按 task group 所有而不是内核指针，并通过 `SYSCALL_SOCKET`（`int 0x36`，编号 `0x5e`）的定长 request 分派；内核与 `libp` 必须同步更新 request 布局、操作枚举和错误码。`AF_INET` 支持 TCP stream、UDP datagram 与 `IPPROTO_ICMP` raw socket；`AF_LOCAL` 支持全局命名的 stream/datagram 端点，accept 出来的服务端句柄归监听者 task group；`SO_RCVTIMEO` 使用 `timeval`，在内核按 10ms timeout tick 向上取整并由 accept 继承。不得恢复 `Socket_*`、独立 `ping` syscall、DPL3 的 `int 0x30` 网络入口或跨层暴露 PCB 指针。
- socket 阻塞调用通过 `WAIT_REASON_SOCKET` 的 waiter 和 lwIP callback 唤醒，连接/接收超时由 `net_socket_tick()` 检查；不得退化为反复 `task_next()` 或 `MSG_DONTWAIT + sleep()` 轮询。PCnet/RTL8139 IRQ 入口只保存一份规范的寄存器/段 frame；网卡 IRQ 在 lwIP input 完全返回并发送 EOI 后，只有本次收包确实唤醒 socket waiter、设置了 `mtask_run_now` 时才调用一次 `task_next()`，不得恢复双重 `pusha`/段保存、在 lwIP callback 内切换任务或让普通无等待者流量触发额外调度。RAW 接收必须复制完整 IPv4 packet 后再让 lwIP 继续处理，不能借用会被协议栈改写的 pbuf。
- `network=enable` 仅启动以太网和异步 lwIP DHCP；`lo` 不依赖网卡或租约，地址可在租约完成前为零，不能把网络启动改回阻塞式 DHCP 或持久化旧的 `ip/gateway/submask/dns` 环境变量。
- 网络验证优先使用 `nettest.bin`：`nettest.bin loopback` 在无需网卡/DHCP 时验证 HPET 单调时间能在一个 10ms tick 内前进、`127.0.0.1` 的 UDP/TCP/ICMP，以及通过 `monotonic_ns()` 测得的 ICMP RTT 小于 10ms；完整模式再验证跨进程 `AF_LOCAL` stream、`AF_LOCAL` datagram、DHCP、QEMU user-net 网关 UDP/ICMP，以及可选 TCP echo。`ping.bin <host-or-ipv4> [count]` 通过 `getaddrinfo` 使用 lwIP DNS，`ping.bin localhost` 和 `ping.bin 127.0.0.1` 不等待 DHCP，适合验证回环 raw ICMP；RTT 使用 HPET 单调纳秒时间，统一按三位小数毫秒显示，低于 1 微秒时显示 `time<0.001 ms`。自动验证时临时修改 `sys.cfg` 与 `init.mst`，结束后立即恢复，仍禁止 `sendkey`。
- `curl.bin` 是用户态纯 HTTP/1.1 客户端，只复用公开 socket/DNS API，不在内核增加 HTTP 实现；支持 `-i`、`-I`、`-L`、`-o`、`-X`、`-d`、`-H`，响应体按 chunked、Content-Length 或连接关闭进行流式传输。只接受 HTTP URL，重定向到 HTTPS 或其他协议必须明确失败。

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
- `apps/libp/arch/i386/syscall.asm`
- `apps/libp/ipc.c`
- `apps/libp/rpc.c`

用户指针来自 `0x70000000` 以上的用户地址空间。新增系统调用时必须复用现有的用户地址/长度验证方式，不能直接把用户指针当作可信内核指针。涉及阻塞、超时或任务退出时，还要检查等待原因、唤醒路径、时钟中断和孤儿/资源清理。

- 顶层系统调用与 IPC 子操作使用“语义化枚举 + designated initializer 处理表”分派。固定 ABI 编号表以枚举的 `COUNT` 作为容量，保留既有编号但不保留旧的 `if/else` 分派兼容层。
- `input_wait(events)` 的 event mask 与 `SYSCALL_INPUT_WAIT` 编号必须在内核、`apps/include/syscall.h` 和对应架构的 `apps/libp/arch/i386/syscall.asm` 同步；它只允许当前鼠标/键盘 owner 等待已经初始化的 FIFO，检查 FIFO 与发布等待必须处于同一关中断临界区并循环处理 ready 竞态。
- `clock()`/`SYSCALL_UPTIME` 保持既有毫秒 ABI；高分辨率单调时间使用 `monotonic_ns()`/`SYSCALL_MONOTONIC_NS`，按 i386 64 位返回约定由 `EDX:EAX` 返回纳秒。内核优先稳定读取 64 位 HPET counter 并用完整 femtosecond period 换算，HPET 不可用时才退回 10ms tick；不得把 HPET MMIO 指针暴露给用户态或改变旧 `clock()` 的单位。
- 表处理函数应对应真实的 API 或同一职责域；不要为了减少函数体行数创建只转发一次的无意义包装。
- GUI 进程是名为 `gui` 的 RPC 服务。用户态 GUI API 只使用不透明 `window_t` 句柄和 `gui_rpc.h` 的定长协议，禁止恢复 `int 0x72`、`set_custom_handler`、跨页执行 GUI 函数或向客户端暴露 GUI 内部指针。创建窗口时由 GUI 通过带 tid/generation 校验的共享映射一次性提供 framebuffer 与单生产者/单消费者事件队列；像素写入、事件/键盘轮询必须直接访问该共享区域。`window_refresh` 在共享 damage 状态中合并矩形，只在从空闲变为待处理时发送无应答 RPC；该通知使用 `IPC_DELIVER_NOW`，接收端正因 IPC 等待时内核直接交接时间片，避免等下一个时钟 tick。该标志只用于短小、无锁的低延迟单向通知，不能在中断上下文或持有会阻塞的锁时使用。`0xf0100000..0xf1000000` 是客户端 GUI 映射保留区，关闭窗口前由客户端解除映射。
- 文件读取与 VFS mount/change/unmount 用户态包装必须保留并返回内核 `eax` 状态；内核、`apps/libp` 和 `apps/include/syscall.h` 的返回语义必须一致。
- 目录枚举的用户态 API 是 `list_directory(path, finfo **entries, size_t *count)`：空路径枚举当前目录，非空路径必须按当前文件系统的相对/绝对目录语义解析；先查询条目数，再按容量填充；合法空目录返回成功且 `count == 0`，失败返回负状态。目录变化导致容量不足时由 `libp` 重新查询并安全重试，不得恢复固定 512 项缓冲区、尾部零哨兵或无容量的旧 `listfile` ABI。
<!-- 过时：`listfile(path)` 固定分配 512 个 `finfo_block`，由内核写入零名称哨兵。 -->
- 命令行 syscall 同样使用 query + capacity；`get_command_line` 动态取得一份 mutable storage。C/C++ 启动代码共同调用 `runtime_arguments_load`，argv 指针数组动态分配并指向该 storage，main 返回后统一 destroy；不得恢复 `GetCmdline`、1024 字节 line 或 128 项/字节 argv 上限。
<!-- 过时：C 与 C++ 启动代码分别用 `GetCmdline` 写固定 1024 缓冲，再为每个 argv 固定分配 128 字节。 -->

### 内核架构边界

- x86 中断帧、汇编入口和寄存器约定放在 `kernel/include/arch/x86/i386/` 及对应 i386 实现中；共享 x86 声明保留在 `kernel/include/arch/x86/`。通用任务、系统调用和信号代码通过架构头访问，不在 `define.h` 重复声明布局。
- i386 的 0..31 异常统一通过 `kernel/arch/x86/i386/exceptions.asm` 构造规范化 frame，并由 `x86_exception_dispatch` 按只读描述表分派。只有 `#NM` 的 lazy-FPU 恢复和合法的写时复制 `#PF` 可以返回；普通用户异常依据保存的 `CS.RPL` 终止当前任务，NMI、双重故障、机器检查及所有内核异常必须 fail-stop。
<!-- 过时：异常入口分散在 `kernel/dos/asm/errors.asm`，通过 FS 猜测用户态、改写 CatchEIP 或在汇编中单独处理/自旋。 -->
- i386 控制寄存器访问统一使用 `kernel/include/arch/x86/i386/control.h` 的固定宽度 inline 接口和 `X86_CR0_*` 位定义；写 CR0/CR3 必须带 `memory` clobber，页故障地址通过 `x86_cr2_read` 获取。
<!-- 过时：CR0 位定义放在 `define.h`，并同时保留 `get_cr0/set_cr0` 与 `load_cr0/store_cr0` 多套实现。 -->
- i386 的 x87 状态与每 CPU owner 统一由 `kernel/arch/x86/i386/fpu.c` 管理，任务结构只保存 `x86_fpu_state_t` 与状态是否已初始化。任务实际切离 CPU 前必须调用 `x86_fpu_flush_cpu()`：只有当前 CPU 的真实 owner 才执行 `FNSAVE`，随后清空 owner 并置 TS，保证所有 `on_cpu == 0` 的任务状态都已落入任务结构、可直接跨 CPU 迁移。`#NM` 通过 `CLTS` 后恢复或初始化当前任务并登记 owner；CPU 初始化保持 CR0.EM 清零、CR0.MP/NE/TS 置位。fork、显式 reset 和任务回收必须使用该架构接口，不得直接改状态标志或在调度器、异常处理器中散落 `FNSAVE`/`FRSTOR`/CR0 FPU 位操作。
- x86 port I/O 统一使用 `kernel/include/arch/x86/io.h` 的 `x86_port_read8/16/32` 与 `x86_port_write8/16/32`；动态 port（特别是 PCI BAR）必须先验证是 I/O 空间、整个寄存器窗口不超过 `uint16_t` 范围，然后才显式收窄；数据宽度必须与硬件寄存器一致。这些 inline asm 都是 `volatile`，使用 `Nd` port/累加器约束并带 `memory` clobber；`x86_io_wait()` 只通过向 `0x80` 写入一个 8-bit 零实现，仅用于 PIC 初始化和明确需要该 legacy delay 的软盘控制器轮询。
- VGA 文本 TTY 的滚屏只能搬移前 `ysize - 1` 行并单独清空末行，优先使用对齐的 32-bit 顺序访问；禁止恢复逐列跨行复制、逐字节搬移或读取第 `ysize` 行之外显存的实现。
<!-- 过时：port I/O、中断开关、EFLAGS 和文本光标通过 `kernel/dos/asm/i386.asm` 的 `io_*`/`ASM_call` 全局 wrapper 访问。 -->
- 普通临界区必须成对使用 `irq_save()`/`irq_restore()` 保留调用者 IF 状态；等待路径要在关中断时发布等待状态并调用 `task_next()`，切回后才 restore。`irq_enable()` 只用于首次启动、明确允许抢占的 syscall 入口、任务终止后等待调度以及需主动开中断等待硬件 IRQ 的路径；不得用它代替临界区 restore。新内核任务可能由中断内的调度切入，因此确实要允许硬件 IRQ 的 bootstrap 入口（例如 `init()`）必须自己显式 enable，不得依赖下游驱动的等待函数偶然开中断。`x86_eflags_read/write` 只用于 AC 位等必须直接修改 EFLAGS 的 CPU 探测，不用于中断保护。
- `kernel/dos/asm/` 已整体退役：x86 中断/系统调用入口 stub 位于 `kernel/arch/x86/i386/interrupt_entries.asm`，其余 legacy 汇编（`memtest_sub`、`gensound`、CPUID `get_cpu*`、`init_page`、`check`、`init_float`、`__init_PIT`、v86 与内核侧 `return_to_app` trampoline、`setjmp`/`longjmp`）已改写为 C 或删除，不得恢复。用户态的信号返回 trampoline 由 `apps/libp` 自己提供并通过 `set_rt` 注册，内核不再复制代码页。入口样板使用汇编宏集中生成，并由 `kernel/arch/x86/common/interrupt_controller.c` 提供共享的中断控制器逻辑。
<!-- 过时：`kernel/dos/asm/i386.asm` 保留 `memtest_sub`、`gensound`、CPUID `get_cpu*`、`init_page`、`check`、v86/返回 trampoline 和 FPU 相关 legacy 实现，后续拆分作为独立任务。 -->
- CPUID 统一使用 `kernel/include/arch/x86/cpuid.h` 的 `x86_cpuid(leaf, subleaf)`，返回值 `x86_cpuid_t` 的字段顺序即 eax/ebx/ecx/edx，可直接按小端字节流复制（brand string 即依赖此性质）。EBX 必须写成输出约束交给编译器保存，不得再出现私有的 `cpuid` 内联包装或破坏 C ABI 的汇编版本。
- PC speaker 完全由 `kernel/platform/pc/beep.c` 用 `arch/x86/io.h` 直接编程 PIT channel 2 与 port `0x61`，时长通过 `sleep()` 计时；不得回到基于 port `0x61` refresh 位的忙等或汇编实现。
- 物理内存探测在 `kernel/arch/x86/i386/memory.c` 内用 C 完成：先用 AC 位区分 386/486，再在一次 `irq_save()`/`irq_restore()` 临界区内 read-modify-write 设置并清除 `X86_CR0_CD|X86_CR0_NW`，探测本身用 `volatile uint32_t` 写-取反-回读并恢复原值，步长从 1GiB 逐次缩到 1/4（最小 4KiB）。该文件用 `KERNEL_NOKASAN_CFLAGS` 编译，探测循环不得引入 KASAN 插桩或打印。
- 分页由 `init_page()`（`kernel/arch/x86/i386/page.c`）一次完成：构造 PDE/PTE 与页管理器后自己写 CR3 并以 read-modify-write 置 `X86_CR0_PG|X86_CR0_WP`；调用方不得再单独补写 WP，也不得恢复 `C_init_page` + 汇编 wrapper 的两段式实现。
- 高地址 bootstrap 页表是仅内核可见、未逐地址空间计入引用数的隐式共享映射。任何遍历任意物理页目录/页表的路径（包括 `pde_clone`、`free_pde`、共享映射和 `page_get_*_pde`）必须在保存中断状态后临时切到 `PDE_ADDRESS` 的恒等映射，再恢复调用者 CR3；禁止在当前用户页表下直接把高端物理页表地址当线性地址解引用。任何路径要在原本不含 `PG_USU` 的 PDE 中建立用户映射，必须先分配并清零私有页表，禁止原地添加 user/write 位或修改该全局页表。exec 装载准备只处理同时 present+user 的 PDE，并丢弃继承的 `PG_SHARED` PTE；不得把设备映射或 GUI 共享页降级成只读映射后留给新程序。用户页映射统一经 `page_prepare_user_table` 完成页表分离，不得恢复已删除的 `page_links`/`page_link_share` 旁路。
- 内核入口的 boot ABI 检查是 `arch_boot_verify()`（`kernel/arch/x86/i386/descriptor_tables.c`），只校验 CS 等于内核代码 selector，失败时直写文本 VRAM 并 `cli; hlt` 停机；不得依赖 loader 的 IDT 或 `int 0x36` 打印。段寄存器在 `arch_interrupt_init` 里紧跟 `lgdt` 重新载入内核数据段 selector，`do_init_seg_register` 这类独立入口不再存在。
- 分页启用后必须永久设置 `X86_CR0_WP`，使 ring0 写只读用户页也触发 `#PF` 并进入 COW；后续 CR0 修改必须使用 read-modify-write 保留 WP，不得写入会清除该位的固定值。
- descriptor table 与任务状态的通用入口是 `arch_interrupt_init`、`arch_task_state_init` 和 `arch_task_set_kernel_stack`；GDT/IDT/TSS 的地址、limit、布局、selector、access bits 及 `lgdt`/`lidt`/`ltr` 只能出现在 `kernel/arch/x86/i386/` 私有实现中。首次 GDT/IDT 构造和活动 descriptor 更新必须全程保存并关闭中断；IDT 必须先完整构造全部 256 个有效入口再执行 `lidt`，`0xff` 默认入口必须可直接安全返回且不发送错误 EOI。
- 驱动通过 `irq_register_handler(irq, handler)`（`kernel/dos/hal/interrupt.c`）注册 IRQ 处理函数；该 API 只维护硬件 IRQ 到处理函数的表，DPL0 gate 仍由 i386 IDT 初始化统一创建。DPL3 gate 只允许由架构初始化为既有的 syscall、custom syscall 和 net API 向量创建，驱动不得自行开放用户态调用权限。PCI 驱动取得 `uint8_t` IRQ 后必须先调用 `irq_is_valid`，成功后才能计算 vector、配置路由或解屏蔽；底层 mask/config API 对非法 IRQ 安全返回。
<!-- 过时：驱动把整数地址传给本地 handler helper，或通过 `ADR_IDT`、`set_gatedesc` 直接改写 IDT 并自行选择 selector/DPL。 -->
- 内核 BIOS 调用统一使用 `kernel/include/arch/x86/i386/bios.h` 的 `x86_bios_interrupt`；调用方不得准备或清理 GDT 临时项，也不得直接调用底层 raw 汇编入口。实模式软件中断全程保持硬件中断关闭，禁止在 raw bridge 中执行 `sti`：APIC/IOAPIC 向量没有实模式处理与 Local APIC EOI 路径，提前开中断会在 KVM 下留下 in-service 向量并阻塞同优先级设备 IRQ。
- 汇编保存顺序与 C 结构布局构成内核内部 ABI。修改任一侧时同步检查任务初始栈、fork、signal、IDT 注册和最终 `iret` 恢复路径。
- x86 软件任务上下文统一使用 `kernel/include/arch/x86/i386/task.h` 的 `arch_task_context_t`；调度器通过 `arch_task_switch`/`arch_task_start` 显式传入当前 context 槽、下一 context、CR3 和 scheduler current 槽/任务。架构汇编不得读取 `mtask` 字段偏移或全局 `current`；fork 中断帧通过 `arch_task_interrupt_return` 恢复。
- `task_next()` 在没有其他 runnable 任务或选出的 next 就是 current 时必须直接返回，禁止调用 `arch_task_switch` 自切换；自切换会先覆盖当前 context 槽、再加载调用前取得的旧 context 指针，造成内核栈回退和返回地址损坏。
<!-- 过时：`define.h` 定义通用 `stack_frame`，`mtask->esp`/`pde` 依赖 0/4 字节固定偏移，切换汇编直接读全局 `current`，fork 通过 `handlers.asm` 的拼写错误入口 `interrput_exit` 恢复。 -->
- SMP 调度器启动必须在所有 per-CPU idle 与 bootstrap task 发布完成后关中断发布 `scheduler_active`，再由 BSP 单独直接启动 bootstrap task，不能先进入 idle 或让 BSP/AP 并发执行首次 context start。显式 bootstrap 入口只请求 AP release，真正的 release 必须由 BSP 最外层 kernel lock 在 owner 清零后提交，禁止在仍持有大内核锁时唤醒 AP。AP 只启动自己的 idle，通用 task bootstrap 不承担 SMP release 等一次性全局职责。
- 大内核自旋锁使用 test-and-test-and-set：锁被占用时只读 owner 并执行 `pause`，观察到空闲后才尝试一次 locked compare-exchange；禁止在等待循环的每次迭代都执行 `lock cmpxchg`，否则 VirtualBox 等虚拟化环境会因总线锁争用导致锁持有者饥饿。
- online 但尚未 release 的 AP 必须在本地 APIC 已启用、定时器尚未启动的状态下用 `sti; hlt; cli` 休眠；BSP 提交 release 后通过专用 `X86_VECTOR_SMP_WAKE` IPI 唤醒，wake handler 只发送 Local APIC EOI，不获取 kernel lock 或进入调度器。AP 醒来确认 release 后才初始化本地定时器并启动 idle，禁止恢复 `pause` 忙等 parked AP。
- 正在运行的内核 C 栈上切换用户态时，C 只在安全的本地对象中完成 frame 构造，最后一步调用 `kernel/arch/x86/i386/user_return.asm` 的 noreturn helper；helper 先 `cli`，在当前 ring0 栈真实 `sub`/复制完整 frame，再原子恢复寄存器并 `iretd`。不得在 C 中把“当前 ESP 减 frame 大小”当作已预留空间。
<!-- 过时：用户态切换函数进入时立即在 `task->top - sizeof(frame)` 写入最终中断帧。 -->
<!-- 过时：C 内联 helper 只计算 `ESP - sizeof(frame)` 后直接写入，未实际调整栈指针保留空间。 -->
- 任务创建需显式记录本路径是否 clone/retain 了 PDE；失败回滚只释放自己拥有的 PDE，启动任务共享的 `PDE_ADDRESS` 不得释放。fork 只能在 PDE、VFS、FIFO 等全部构造成功后发布为 `RUNNING`。
- `create_task`/`create_thread_task` 只返回 `ALLOCATING` 任务；调用方必须在 TTY、line、FIFO、参数页等外围资源完整后调用 `task_publish` 一次发布，失败则调用 `task_abort_creation`。任务退出会取消其 waiter timer；高文本光标 owner 退出时还必须清空全局 cursor/timer 引用，不能让 timer FIFO 指向已释放的任务栈。
- 任务槽由 `kernel/dos/task/mtask.c` 的稳定地址分块注册表按需扩容，TID 不再受 255 个静态数组槽限制；跨子系统扫描统一使用任务迭代器，不得重新暴露或假定连续的全局 `mtask[]`。物理页 owner 与引用计数均为 32 位，fork/COW/共享映射不得把 TID 或引用数收窄到 8 位；页元数据末端必须通过静态断言保持在 KASAN shadow 之前。
<!-- 过时：`create_task_impl` 在调用方写入 line、TTY 和启动参数前直接设为 `RUNNING`。 -->
- 用户态 ELF/shell heap、stack 和 `0xf0000000` 映射必须逐页检查 `page_link`；任一失败在写用户地址或进入 `iretd` 前终止任务，由任务退出统一回收已标记的部分映射。
- 物理页引用计数和 fallback owner 仅由 `kernel/arch/x86/i386/page.c` 的正式接口维护，外部代码不得直接读写 `PAGE_INFO.count/task_id`。`task_id` 仅在 `count == 1` 时可表示独占 owner；引用从 1 增到 2 前必须清空 owner，共享页降回 1 仍保持无 owner。任务退出的 `gc` 只回收 `count == 1` 且 owner 匹配的独占页，不能把仍有引用的共享页强制归零；retain 溢出和 release 下溢属于 fatal 不变量错误。
<!-- 过时：`task_id` 可在页面变为共享后继续代表创建者，任务退出时 `gc` 通过循环递减把该 owner 的页面引用强制清零。 -->
- 用户态 ELF loader 接收真实 image size，完整验证 Ehdr/phdr/PT_LOAD、文件范围、用户虚拟范围、对齐、溢出和 executable entry。装载严格分两阶段：全部 segment 页映射成功后才复制文件数据和清零 BSS；共享物理页的相邻 segment 不重复 `page_link`。不得恢复无 size 的 `elf32_get_max_vaddr/load_elf` 接口。
- 用户程序保持 `.text`/公开链接基址 `0x70000000`。通用链接参数使用 `-N -Ttext 0x70000000`，避免 GNU ld 额外生成低于用户边界的 header PT_LOAD；因此现阶段会出现 RWX LOAD 警告，而 loader 仍要求所有 segment/page 均不低于 `USER_SPACE_START`，entry 必须落在带 `PF_X` 的 load segment 内。
- 继续拆分架构代码时只处理 `kernel/`；`loader/` 保持当前实现，除非任务明确要求修改。

### SMP 与多核调度

- x86 SMP 拓扑来自 ACPI MADT；`kernel/arch/x86/i386/smp_trampoline.asm` 的实模式 trampoline 固定复制到物理地址 `0x6000`，AP 通过 INIT-SIPI-SIPI 进入共享分页内核。BSP 必须排在逻辑 CPU 0，APIC ID 与逻辑 CPU 编号不得混用；xAPIC ICR 使用 8-bit destination field，x2APIC ICR 使用高 32 位完整 destination ID。
- 每个在线 CPU 拥有独立 TSS、ring0 栈指针、当前任务、idle 任务、最小虚拟运行时间和重调度状态；共享 GDT 为每 CPU 使用不同 TSS descriptor。AP 在启用自己的 Local APIC/x2APIC 后才能读取当前 CPU ID，随后加载 TSS 并等待调度器发布 idle 任务。
- 调度器采用每 CPU 运行队列语义、CFS 风格加权虚拟运行时间和周期负载均衡：普通任务权重为 1，交互/高优先级任务提高权重，唤醒任务获得有界的 wakeup boost；CPU 0 每 100ms 在最忙和最空闲 CPU 间迁移一个未运行且未固定的任务。不得恢复按 TID 全局轮转、全局 `current` 或单个全局 idle task。
- CPU 本地时钟均为 100Hz：BSP 继续负责全局 timer、网络 timeout 与 IPC timeout；AP 使用 TSC-deadline，缺失时校准 Local APIC periodic timer。每个 CPU 的时钟只累计本 CPU 当前任务的运行时间并触发本地调度，AP 不得重复推进全局时间和全局 timer 链。
- 调度启动后的 `sleep()` 必须通过内核 timer 和 `WAIT_REASON_TIMER` 阻塞；禁止在持有 kernel lock 时轮询 BSP 的 `timerctl.count`，否则迁移到 AP 的任务会阻塞 BSP 时钟 IRQ。只有 `current_task()->tid == NULL_TID` 的调度前引导路径可保留 tick 忙等。
- 当前遗留内核子系统仍以可调度的 kernel lock 串行进入；系统调用、异常和已注册硬件 IRQ 的汇编入口必须成对进入/离开该锁。锁所有权按 CPU 记录并可在同一 CPU 的上下文切换中直接交接；`kernel_lock_leave()` 可能触发调度，因此恢复后必须重新读取当前 CPU，不能使用切换前缓存的 CPU 编号。用户态在不同 CPU 上并行运行，idle 在释放 kernel lock 后使用 `sti; hlt`。
- 在实现跨 CPU TLB shootdown 之前，共享同一 PDE 的整个任务组固定在同一 CPU：创建首个线程时必须同时固定 leader 和已有同地址空间任务，`task_pin_current` 迁移时也必须整体迁移，不能只固定新线程。跨进程共享映射只允许修改当前未在 CPU 上运行的目标任务。修改这一限制时必须先实现同步 TLB shootdown，保证目标 CPU 在旧映射物理页释放前完成失效；若硬件因本地旧 TLB 权限产生 fault，而当前 PDE/PTE 已可写，异常路径可在重载 CR3 后重试。
- BIOS/VBE 实模式调用只能在 BSP（逻辑 CPU 0）执行；用户任务首次请求 VBE/BIOS video 时固定并迁移到 BSP，后续由其他用户进程继续占用 AP。`set_mode` 返回失败、framebuffer 未页对齐、尺寸溢出或物理范围回绕时必须停止映射并向用户返回失败，不能从 `0xffffffff` 建立页表。
- GUI terminal 的输入 FIFO 位于 GUI 用户地址空间；`gmouse` 写入 console FIFO 后必须调用 `tty_notify_input(tty_t)`。内核只接受仍注册在 `tty_list` 中的句柄，并按 TTY 唤醒 `WAIT_REASON_KEYBOARD` 任务；仅写用户 FIFO而不通知内核会使 terminal 永久睡眠。
- GUI 桌面显式维护唯一 `focused_window`：窗口首次显示以及任意鼠标按下（包括标题栏和右键）都必须经统一焦点入口置于普通窗口最上层、鼠标等 overlay 下方；键盘只投递给该焦点窗口，禁止再用 `sheet->height == top - 1` 猜测焦点。隐藏或销毁焦点窗口时必须从剩余可见窗口中重新选择焦点。
- `gui.bin` 是严格单例：必须在读取大资源、切换 VBE 或创建 `gmouse` 输入线程之前先注册 `gui` RPC 服务，服务名冲突时立即退出，禁止第二实例先覆盖显示模式或输入 owner。内核的 `mouse_enable()`/`use_keyboard()` 采用不可抢占的独占获取，已有其他任务持有时返回失败；GUI 输入线程必须检查返回值，不能静默抢占全局 PS/2 所有权。
- 任务的活动 `TTY` 与稳定 `tty_session` 语义分离：同步执行子程序时可临时交出活动 TTY，但会话归属必须继承并保持。释放注册 TTY 时先把整个会话切换到安全 fallback，再终止其进程/线程并回收僵尸，禁止留下指向已释放 TTY 的等待父任务；TTY 输入通知只唤醒正在 `WAIT_REASON_KEYBOARD` 上等待的前台任务。
- GUI console 关闭必须完整释放其 TTY 会话、console 线程栈、键盘 FIFO、内部 sheet/控制器/VRAM、窗口和链表节点；不得保留空的 `close_console` 或忽略 `AddThread`/分配失败，否则每个已关闭终端仍会遗留 `thread + psh.bin` 并最终耗尽任务槽。ToolBox/super-window 的子 sheet、按钮和 textbox 同样由窗口关闭路径统一回收。
- `apps/gui/gui.h` 的 window/console/sheet 布局被多个显式对象共享；修改结构布局后必须通过头文件依赖重编全部 `apps/gui` 对象，不能混用旧对象。console 的 `tty_handle` 必须使用与公开 `tty_t` 同宽的 `uintptr_t`，并保留编译期宽度断言；不得使用 `unsigned` 截断 x86_64 内核句柄，否则 shell 输出、输入通知和 TTY 回收都会失效。console 线程使用完整的 `psh.bin` 命令行启动 shell，并在 shell 返回后退出，不能忙等。GUI 终端滚屏只复制前 `ysize - 1` 行并清空末行，禁止读取第 `ysize` 行之外的像素。
- GUI 终端回归使用 `scripts/test-x86_64.py --console --firmware bios`、`--console --firmware uefi` 和 `--arch i386 --console --memory 512`。脚本通过临时 `init.mst` 启动 GUI，QMP 只测试点击、字符输入、退格和空行，不能通过按键执行测试命令；直接检查提示符、回显、退格恢复和滚屏后的窗口像素，输出 `GUICONSOLE PASS` 并保存截图。测试后恢复启动脚本及正常镜像，仍禁止 `sendkey`。
- `X86_VECTOR_RESCHEDULE`（`0xf0`）是内核固定重调度 IPI；唤醒远端 CPU 上的任务时发送该 IPI，处理入口发送 Local APIC EOI 后进入本地调度。外部 IOAPIC IRQ 仍路由到 BSP，不能让 AP 重复处理同一设备中断。
- 用户态任务快照使用 `SYSCALL_TASK_SNAPSHOT`（`0x60`）的 query + capacity ABI，CPU 数量/当前 CPU 使用 `SYSCALL_CPU_INFO`（`0x61`）；`apps/ps` 构建为 `ps.bin`，显示 TID、TGID、状态、运行核心和累计运行时间。修改 `task_info_t` 时必须同步 `kernel/include/task_snapshot.h`、`apps/include/task.h` 与 `apps/libp/task.c`，并保持结构大小断言。
- `kernel/Makefile` 的 QEMU CPU 数由 `QEMU_CPUS` 控制，默认 4。内核 C/C++ 编译统一生成并由各子 Makefile 加载 `.d` 头文件依赖；修改 `mtask` 等共享结构布局后，受影响对象必须由依赖图自动重编，不能删除依赖跟踪或用不完整的旧增量对象做启动验证。

### 新增或修改内核源文件

- 各子目录 `Makefile` 都使用显式对象列表；新增 `.c`/`.cpp`/`.asm` 文件时必须加入对应列表。
- 要链接进主内核的对象还必须出现在 `kernel/Makefile` 的 `OBJS_BOOTPACK` 中。
- 新公共声明放入合适的 `kernel/include/*.h`，避免继续无条件扩大 `dos.h`；但修改已有共享结构时要检查所有直接依赖者。
- 中断、驱动和调度代码可能在不可阻塞或中断关闭的上下文执行。引入分配、锁、日志或等待前，先确认调用上下文。

### 新增或修改应用

- 应用通常包含 `../defs.mk`，定义 32 位 freestanding 编译参数、`Main` 入口和基础库链接方式。沿用相邻小型应用的 `Makefile`，不要使用宿主默认链接规则。
- `apps/libp` 的 `mkdir`、`chdir` 等文件系统包装遵循 POSIX 返回语义：成功返回 `0`，失败返回 `-1` 并设置 `errno`；调用方必须用 `== 0`/`!= 0` 判断，不得恢复旧布尔式 `!mkdir(...)`。需要接受已存在目录时，仅在 `errno == EEXIST` 且 `stat` 确认目标为目录后继续。
- 新应用必须加入 `apps/Makefile` 才会进入全量构建。
- 非 LiveCD 镜像仍由 `kernel/Makefile` 的显式 `mcopy` 控制；LiveCD 则动态收录全部 `apps/out/*.bin`，不要再为 LiveCD 维护第二份应用列表。
- C/C++ 应用实现常规的 `main`，不要绕过 `apps/libp` 的 `Main` 启动包装，除非任务明确要求自定义运行时。
- 修改用户态库时检查 C 与 C++ 两套归档：`libp.a`、`libcpps.a`，以及 `libabi.a`、`libgui.a` 等相关产物。

- x86_64 用户程序构建由 `apps/build-x86_64.mk` 提供公共规则，`apps/native-apps.mk` 维护应用、库与专属编译参数；单应用通过 `native.mk` 转到同一依赖图。SDL、lite、Doom、NASM 的 `sources.mk` 同时供两种架构使用，不能维护两份不同的源码清单，也不能把第三方对象写回源目录再被另一架构复用。当前构建生成 62 个 ELF64 程序；TCC 工具链、DOSLDR 安装器和 x87 专用 fputest 仍只构建 i386。
- SDL 的唯一活动实现是 `apps/sdl2`，lite、Doom、invader 使用同一版本，不得重新链接 `sdl2_old`。SDL surface 直接写共享绘图缓冲的 client area，pitch 包含外部窗口边框；GUI 的 sheet 必须使用自己持有的已提交画面，不能直接引用客户端仍在绘制的共享像素。提交时只把裁剪后的 damage 按行 memcpy 到 sheet，不能增加 SDL 私有的第三份缓冲或逐像素复制。`window_present` 复用刷新 RPC 并等待应答，返回后 SDL 才能清空、绘制下一帧；`window_refresh` 继续提供合并通知的异步更新，dirty/queued 清零不能当作完整帧已复制的确认。GUI 的复制与合成由现有 TaskLock 串行化，鼠标、遮挡恢复和标题重绘统一读取已提交画面；窗口私有 vram 统一由窗口创建和释放，禁止恢复外部 vram/owns_vram 双重所有权。窗口由 GUI RPC 管理，显示尺寸从 `framebuffer_info` 查询，不能在 SDL 后端调用 BIOS/VBE。
- SDL 输入状态按窗口保存，只消费该窗口共享队列；E0 前缀必须跨事件保留，不能忙等下一个字节或用扩展扫描码索引 ASCII 小表。键盘修饰状态复用 SDL 的实现，鼠标事件始终成组消费坐标与滚轮数据。`SDL_GetTicks64`、性能计数器与延时依赖 `monotonic_ns`/内核阻塞睡眠；当前线程、异步 SDL timer、音频设备与 GPU 后端仍不支持，不能以空成功函数伪装支持。
- `window_set_title` 使用 GUI_RPC_SET_TITLE，按服务端窗口 owner 验证，长度包含一个终止 NUL；GUI 统一重绘标题与控制按钮，文本必须裁剪在按钮之前。不能把 GUI 内部 window 指针传给 SDL 或客户端。
- lite 绘图必须按 SDL surface 的真实 pitch 寻址，命令缓存起点和每条命令都满足 `_Alignof(Command)`；Lua userdata/字体指针保留原生宽度。文件 `stat` 失败必须返回 nil/error，绝对路径不能用固定 255 字节缓冲拼接或错误地返回原始相对路径。nk 的窗口尺寸受当前显示限制，不能假定屏幕高于 800 像素。
- 用户态内存分配仅使用 `libp/abi.c` 的活动分配器；`memory/freeinfo`、`mm_alloc` 和空 `api_free` 已删除，调用者统一 `malloc/free`。C4 的字节码、符号表与栈单元为 `intptr_t`，不得将代码、数据或 FILE 指针存入 int。
- 日期读取通过 `platform_rtc_timestamp` 的稳定 RTC 快照提供 UTC，按状态寄存器解码 BCD/二进制和 12/24 小时制；世纪不能再叠加 1980。`gmtime` 为 UTC，`localtime`/`mktime` 当前为 UTC+08:00，标准 `tm_year` 基准是 1900。`timetest.bin` 验证 epoch、闰年、世纪、月份归一化及 RTC 推进；GUI 使用同一时间库。
- C4/NASM/JavaScript 回归使用 `scripts/test-x86_64.py --tools`，通过 Lua 准备临时 C/汇编文件并验证 C4 堆指针、NASM 原生 ELF class/machine 和 JavaScript 对象；不使用按键执行命令。NASM 文本文件使用标准 `r`/`w` 模式，不能依赖 `rt`/`wt` 扩展。
- SDL/时间回归运行 `scripts/test-x86_64.py --sdl --firmware uefi` 与 `--arch i386 --sdl --memory 512`。`--desktop-app lite` 验证打开、输入、保存、标题及正常关闭，`--desktop-app nk` 连续采样 40 帧检查三个颜色控件不闪烁，并验证关闭；可选 `--firmware uefi`，日志和截图由 `--out` 指定。测试使用固定 UTC RTC，按键只用于编辑和输入验证，测试命令仍通过临时 init.mst 注入。`--sdl` 还会在清屏未 Present、局部提交、Present 后立即复用缓冲三个阶段遮挡再暴露窗口，检查实际像素并输出 `SDLFRAME PASS`；不得用仅编译通过或单张截图来代替帧提交、显示和输入验证。

### 加载器、文件系统和磁盘格式

- `loader/` 与 `kernel/` 各自有 FAT/PFS/VFS 和驱动代码。修改磁盘结构或加载协议时必须检查两边的结构定义和读写逻辑，不能只修一侧。
- 内核挂载状态显式区分 `INITIALIZING`、`ACTIVE`、`RETIRED`。进程 cwd dentry、打开文件和显式 `X:` 路径解析持有挂载引用；卸载先从活动表摘除挂载，已有引用可继续使用，最后一个引用释放后再销毁驱动状态与页缓存。
- 每个物理盘记录 retired generation 计数和 format reservation。新挂载可与 retired generation 并存；format 遇到零引用的 active 挂载时在同一临界区摘除挂载并取得 reservation，再在锁外依次销毁缓存和格式化，仍被任务引用、正在初始化或存在 retired 引用时必须保持现状并失败。mount 初始化与 format 必须互斥，retired 计数只能在 `delete_fs` 完整结束后递减，保证 format 不会与销毁并发。
- 路径解析唯一实现在 VFS：`X:`/`X:/` 从指定盘根开始，`/` 从当前盘根开始，相对路径从 cwd 开始；VFS 统一折叠 `/`、`\\`、重复分隔符、`.` 和 `..`。FAT/PFS/ISO9660 驱动只接收已解析的目录 node 与单个 component，禁止恢复驱动内多组件 path resolver 或 cwd cursor。
- 文件系统驱动通过 `vfs_filesystem_t` operations 实现 root、normalize-name、lookup、offset read/write、truncate、create/remove/rename、iterate 和 sync。数据 I/O 返回真实字节数或负错误，不得恢复整文件 bool `read_file/write_file`。
- rename 由 VFS 独立解析两个 parent+leaf，跨挂载必须失败；当前 FAT/PFS 仅支持同目录 rename。仍被打开的文件或作为 cwd/祖先的目录在 unlink/rename 时返回 `EBUSY`。
- 内核虚拟盘 `disk_read` 的物理扇区固定为 512 字节，因此当前 FAT 实现只接受 BPB `BytsPerSec == 512`。多簇目录缓冲区的每簇偏移必须使用 `ClustnoBytes`，数据簇号必须先验证 `>= 2` 且整簇落在磁盘范围内。
- 虚拟盘可通过 `register_vdisk_at` 预留 ABI 盘符；软盘、DEVFS 和 legacy IDE 分别固定使用 `A:`、`B:` 与 `C:` 起的设备槽，不能退回 first-free 注册导致 IDE 回调访问的设备编号与 `vdisk` 容量元数据错位。ATA IDENTIFY 声明 48-bit LBA 但容量字段为零时必须回退 28-bit 容量。
- 块文件系统探测必须先检查 `vdisk_type`：FAT/PFS 只读取 512-byte 块盘，ISO9660 只通过 `CDROM_Read` 读取 2048-byte 光盘。禁止让 FAT/PFS 用 `disk_read` 探测 ATAPI 盘，否则单扇区探测会把 2048 字节写入 512 字节缓冲并破坏堆。
- FAT format 根据磁盘容量选择 FAT12/16/32，迭代计算 FAT 长度并只使用 2 的幂次 sectors-per-cluster；FAT 表和根目录必须完整清零、写入标准保留项，并回读 boot sector、两份 FAT 和根目录后才报告成功。不得恢复未初始化格式化缓冲区或任意非 2 次幂簇大小。
- VFS format syscall 返回标准状态：内核 `vfs_format` 成功为 `VFS_OK`，忙、参数错误、不支持的文件系统及底层 I/O 失败返回对应负错误；`libp format()` 统一转换为成功 `0`、失败 `-1` 并设置 `errno`。psh 必须据此区分占用、不支持和 I/O 错误，不得恢复无论原因都提示“切换盘符并关闭程序”的布尔接口。
- FAT 表 API 的长度统一表示条目数，循环使用 `i < count`；FAT12 奇数末项、链目标、保留标记和磁盘范围必须在写入缓存前验证，损坏 FAT 使 `init_fs` 失败。新目录只写 `.`、`..` 和标准 `0x00` 终止项，不创建 `NULL` 伪文件或额外占用簇。
<!-- 过时：FAT 保存长度表示“末项偏移”并使用 `i <= length`，新目录额外创建名为 `NULL` 的占位文件。 -->
- FAT mkdir 直接持有 parent slot、child cluster 和可选的 parent-extension cluster；所有分配、list append 与 realloc 在提交 parent entry/FAT 前完成并可逆序回滚，提交后直接写标准目录簇，不得恢复 `mkfile -> 再查路径 -> del 回滚` 的间接流程。
<!-- 过时：FAT mkdir 先创建普通文件，再通过路径重新查找并改写为目录，失败时调用通用 `del` 猜测所有权。 -->
- 进程文件状态由引用计数 `vfs_context` 封装 cwd 和动态 fd table。线程共享 context；普通新进程只继承 cwd；fork 复制 fd 表但引用同一 open-file description，因而共享 offset/append 状态。任务构造失败与退出必须释放 context 并关闭全部 fd。
- 用户文件 ABI 唯一入口是 `SYSCALL_VFS` 的语义化 operation 表和带 size 的定长 request。禁止恢复 `filesize/api_readfile/Edit_File/mkfile`、分散挂载 syscall 或把用户 `FILE *` 强转为 fd。
- libp `FILE` 是公开不透明类型，私有结构持有真实 fd、8 KiB 缓冲、EOF/error 与 ungetc 状态；`fopen/fread/fwrite/fseek/fflush/fclose/fdopen/fileno` 必须遵守当前 VFS offset 和 errno 语义。
- VFS 使用全局 4 KiB clean-page 读缓存，默认容量为检测内存的 `1/128`（限制在 256 KiB..8 MiB）；哈希桶负责常数复杂度查找，LRU 链只维护淘汰顺序。达到 8 KiB 的读取先检查请求范围是否已全部驻留，未命中时必须一次进入文件系统，并用结果填充完整覆盖的缓存页（到达 EOF 的末页也是完整有效页），禁止重新拆成逐页驱动调用。单次流式读取可缓存页数超过全部容量时不得边插入边淘汰。写入为 write-through：驱动成功后再更新或失效重叠缓存页，不得在没有完整回写/失败语义时引入脏页。
- FAT 对齐的完整连续簇必须合并为批量磁盘 I/O，只在首尾非对齐或清零写入时分配 bounce buffer；不得恢复逐簇获取磁盘信号量、逐簇分配或无条件二次复制。程序 ELF 加载只解析一次路径并直接使用 VFS handle，段复制完成后立即释放内核临时镜像。
- `vdisk.max_transfer_sectors` 是块设备单次传输能力，未声明时保守使用 8 个扇区；通用磁盘层按设备能力分批并在每批之间响应已发布的重调度，不得在持有全局 kernel lock 时将长读写变成不可抢占的整段忙等。每个逻辑磁盘信号量使用可扩容的 32 位 TID FIFO，禁止把等待者重新塞进 `FIFO8` 或固定 256 项字节数组。内核 IDE 数据路径只使用 PCI Bus Master DMA：从 `init_PCI` 已建立的设备表按 class/subclass 查找兼容模式 IDE controller，验证 BAR4 是范围合法的 I/O window 并启用 PCI I/O/bus-master command bits，不得恢复启动时传入零 BAR、ATA PIO 或 ATAPI data-phase PIO fallback。每个 channel 持有 page-backed 64 KiB bounce buffer 与 PRDT，PRD 必须在 64 KiB 边界分段；ATA 单次最多传输 128 个 512-byte 扇区，ATAPI READ(12) 必须通过 PACKET feature DMA 批量传输最多 32 个 2048-byte 扇区。两者共用 bus-master/IRQ 阻塞完成状态，ATAPI 只保留独立的 command packet 阶段；ISO9660 对齐的完整连续扇区必须直接批量读取，不得退回逐扇区 data transfer。IRQ 必须停止 bus master、清除 status、读 ATA status 并只发送一次对应 EOI。
- 程序创建、fork、页分配和用户堆扩展的成功热路径不得输出裸地址或逐次分配日志；串口日志只保留可操作的错误、显式诊断模式和必要生命周期状态，避免同步输出成为程序启动延迟。
- 内核通用堆由 `kernel/dos/mm/heap.c` 统一封装按架构选择的 vendored liballoc/talc；x86_64 堆及任务 SIMD 状态必须满足 16 字节对齐；第三方导出在构建副本中统一改名为私有 `liballoc_*`，内核和模块只能使用带 KASAN/元数据校验的标准 `malloc/free/realloc`。heap 从 1 MiB span 起步并按需通过 `page_malloc` 增长，不得恢复 `memory/freeinfo` 排序整理器或启动时预清零固定 128 MiB arena。链接时 liballoc archive 必须紧跟 `heap.o`，使其静态锁状态位于固定 `0x400000` 页表区之前；放到对象列表末尾会在 KASAN 构建中被页表初始化覆盖。
- PFS format 必须在写盘前完整读取并校验 boot sector 与 `dosldr.bin`，按 boot sector 每次 92 个扇区的读取粒度动态扩大并清零 loader 保留区，再把 bitmap/root 布置在保留区之后；检查长度、磁盘容量和分配失败，所有区域写后校验并把真实失败传播到 VFS/psh/安装器。VFS 统一负责安全摘除无人使用的活动挂载；psh 格式化成功后重新挂载目标盘，安装器成功后重新 mount/change，失败时按逆序尽力恢复源盘。
- PFS node 以 directory block + inode index 定位文件，以 directory data block 定位目录；文件数据按 508-byte payload block 进行偏移 I/O。PFS 中不得出现 path resolver、`current_dict_block` 或 `prev_dict_block` cwd 状态。
- 启动系统盘必须同时包含 `init.bin`、`psh.bin`、`sys.cfg`；探测失败必须 panic，不得退回可能是 DEVFS 的 `first_vdisk()`。显式盘符剥离依赖标准重叠 `memmove` 语义，修改基础内存函数后必须做冷启动验证。
<!-- 过时：系统盘文件探测全部失败后仍退回第一个虚拟盘继续启动。 -->
- 启动扇区、加载地址、ELF 入口和分区/文件系统布局属于启动 ABI；任何改动都需要完整构建和冷启动验证。
- `kernel/Makefile` 使用大量显式 `mcopy`/`mmd` 命令。重命名产物、资源或目录时同步更新所有打包位置。

## 代码风格与实现约束

- 项目自有 C/C++ 代码遵循根目录 `.clang-format`：2 空格缩进、不使用 Tab、左大括号同行、指针星号靠变量。只格式化本次触及的项目自有文件。
- 历史代码风格并不完全统一，且部分目录关闭了警告。不要借功能修改之机大面积重排代码；编译成功也不代表没有截断、越界、符号扩展或并发问题。
- 内核编译禁用宿主头文件、内建函数、栈保护和 PIE；i386 禁用 MMX/SSE 并使用 x87，x86_64 使用 SSE2。C++ 禁用异常和 RTTI。新代码必须保持这些限制，不要引入依赖异常、线程局部存储、动态链接或宿主运行时的库。
- i386 编译参数使用 UTF-8 输入、GB2312 执行字符集；x86_64 应用使用 UTF-8 执行字符集以匹配 flanterm。修改中文字符串、字体或终端输出时要考虑转换和字节长度，不能默认 UTF-8 字节序列会原样进入镜像。
- 优先使用仓库已有的整数类型、分配器、字符串/内存函数、锁和日志接口。引入宿主专用 API 前先确认它确实只用于 `scripts/` 或宿主工具。
- `NewList`、`AddVal`、TTY 和 MST 构造路径都可能分配失败；调用者必须在解引用前检查，失败时回滚已 append 的节点并释放 token buffer、嵌套 list 和外层对象，不得继续使用部分构造的状态。
- 该代码大量依赖 32 位指针和整数互转。新增代码应使用项目已有的 `uintptr_t`/固定宽度类型表达地址，并显式检查溢出、对齐和范围。
- 不要修改或提交 `apps/out/`、`apps/libs/`、`loader/out/`、`kernel/obj/`、`kernel/img/`、`kernel/*.img`、`kernel/*.log` 等生成物，除非任务明确要求交付镜像或二进制。
- 不要随意执行全量 `clean`：清理规则会递归删除大量产物，之后完整重建耗时且会重新创建大型镜像。需要清理时只清理与任务相关、可确认可重建的目标。

## 提交前检查

1. 确认只改了任务需要的源码/文档，没有顺手格式化第三方目录或提交构建产物。
2. 新文件已加入对应显式对象列表、顶层应用列表或镜像打包规则。
3. ABI 改动已同步内核、用户头文件和 `libp` 包装层。
4. 运行最小相关构建；启动链、驱动、文件系统、调度或 ABI 改动再做完整构建和 QEMU 串口冒烟测试。
5. 运行 `git diff --check`，并在交付说明中列出实际执行的验证以及由于环境限制未执行的验证。
