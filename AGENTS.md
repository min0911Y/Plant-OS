# Plant OS 仓库协作指南

本文件适用于整个仓库。Plant OS 是支持 i386 和 x86_64 的 freestanding 操作系统，使用自定义 ABI；不能默认依赖宿主 libc、Linux ABI 或宿主运行时。

## 协作原则

- 继续重新审视当前代码，还有没有可以优化，整合，删除你新增的和原有的冗余代码。进一步精简你的实现，使得你的实现更加内聚，而不是到处打补丁。你的实现应该是正确的，标准的，高性能的。你应该思考让你的代码精简优化，改动小，简洁优雅，符合最佳实践。你的实现应该是高性能的。不要创建无意义的自由函数，思考尽量不要或减少新增无意义的成员变量。不要兼容旧的遗留的实现，而是删除替换旧的实现。你的实现应当有通用性与扩展性，避免硬编码与限制。消除嵌套，消除强耦合，消除不必要的短小函数，用较为oop的设计，使用struct+enum等进行抽象和封装，禁止考虑向后兼容。适当拆出变量，从而更加清晰且减少嵌套。对于每个功能的实现，优先思考更精简优雅的写法，再考虑适当拆除变量从而提升可读性。你的代码更加精简优雅，规范且标准，效率高，结构清晰整洁，符合最佳实践。完全与各方面解耦。避免到处打补丁的做法，从架构和设计上使用更精简优雅的实现。
- 以当前 Makefile、源码及调用链为准。两个架构都在 `kernel/`，没有 `kernel64/`；历史 README、迁移记录及 `scripts/build_rootfs.sh` 不能作为当前构建依据。
- 项目自有 C/C++ 遵循根目录 `.clang-format`：2 空格缩进、不使用 Tab、左大括号同行、指针星号靠变量。只格式化本次触及的代码，不机械重写第三方目录。
- 使用项目现有类型、分配器、锁和日志接口；地址使用 `uintptr_t` 等原生宽度类型，检查溢出、范围、对齐和分配失败。构造失败要完整回滚，不发布半初始化对象。
- 不随意执行全量 `clean`，不提交 `apps/out/`、`apps/libs/`、`loader/out/`、`kernel/obj/`、`kernel/img/` 及生成的镜像、日志；明确要求交付的产物除外。
- 完成修改后同步本指南中受影响的长期规则；实现细节和验证步骤放入对应专题文档，不追加修复流水账、过时注释或重复约束。

## 代码位置

| 路径 | 职责 |
| --- | --- |
| `kernel/dos/` | 初始化、任务、内存、IPC、系统调用和通用服务 |
| `kernel/arch/x86/{i386,x86_64,common}/` | 架构后端及共享 x86 实现 |
| `kernel/platform/pc/`、`kernel/drivers/` | PC 固件与设备实现、存储/网络/USB 驱动 |
| `kernel/fs/`、`kernel/io/`、`kernel/net/` | VFS/文件系统、TTY/输入/显示、lwIP/socket |
| `kernel/include/` | 公共接口；架构布局放在对应 `arch/` 头文件中 |
| `kernel/boot/`、`loader/`、`kernel/res/` | 磁盘启动扇区、独立 DOSLDR、镜像资源 |
| `apps/include/`、`apps/libp/`、`apps/ldso/` | 用户 ABI、C/C++ 运行库、动态链接器 |
| `apps/<name>/`、`scripts/`、`doc/` | 用户程序、宿主构建/测试工具、专题文档 |

## 构建与打包

需要 GNU make、GCC/G++、binutils、NASM、mtools 和 QEMU；i386 工具链须支持 `-m32`/`elf_i386`。LiveCD 另需 `curl`、`tar` 和 `xorriso` 或 `genisoimage`。

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
- LiveCD 按构建图生成的 `applications.list` 收录应用，不能扫描残留 `.bin` 或手写第二份应用清单；i386 TCC SDK 只按构建图生成的 `sdk-libraries.list` 收录静态自举库，不能扫描残留归档或混入应用私有 PIC 库；磁盘镜像仍由 `kernel/Makefile` 的显式 `mcopy`/`mmd` 控制。改名或新增资源时同步打包规则。
- i386 LiveCD 保留 FAT/PFS 引导模板和 `DOSLDR.bin` 安装资源。`setup.mst` 从已填充镜像生成，使用 `mshortname` 得到真实 FAT 别名，覆盖全部目录和文件，`DOSLDR.bin` 为首个文件；安装器继续支持 FAT/PFS。布局见 [LiveCD](doc/livecd.md)，应用清单以当前脚本为准。
- 日常交付保持默认 `USB_DEBUG=0`，诊断时显式启用 `USB_DEBUG=1`。`KASAN=1`、`PERF=1` 仅支持 i386；`BENCH=1` 才启用启动计时基准，`MEMTEST=0` 必须同时指定 `MEMSIZE_MB`。这些选项及 `VT100` 必须纳入构建配置指纹。

## 启动、架构与 ABI

- 通用内核通过 `arch.h`、`platform.h`、`irq.h` 使用后端；x86 汇编、寄存器、GDT/IDT/TSS 布局及 PC 端口操作留在架构/平台目录。共享代码不引入固定指针宽度、私有 CPUID/port I/O 包装或 BIOS 细节。
- i386 磁盘链为启动扇区 → DOSLDR → `kernel.bin`；Limine LiveCD 通过 Multiboot2 直接加载内核与 FAT initramfs。保持链接脚本的地址断言和前 32 KiB 内的 Multiboot2 header；initramfs 物理页须及时保留，不能被内存探测覆盖，作为可写但不持久化的 `R:` 根盘。
- x86_64 使用 Limine native/base revision 3 的内存图、HHDM 和固件信息，不探测写 RAM、不扫描 BIOS 区或调用实模式服务。MP request 必须声明 x2APIC 支持，保留固件已启用的模式。启动协议、SSE2/XSAVE 与 syscall/信号帧约束见 [多架构说明](doc/multiarch.md)。
- 内核保持 freestanding 编译约束，C++ 禁用异常和 RTTI。i386 使用 x87、禁用 MMX/SSE；x86_64 两侧使用 `-mno-red-zone -msse2 -mfpmath=sse -mlong-double-64`。i386 执行字符集为 GB2312，x86_64 应用为 UTF-8；修改文字输出时检查编码和字节长度。
- 用户 syscall 编号和参数顺序统一维护在 `apps/libp/arch/syscalls.inc`。修改 ABI 时同步内核处理器、`apps/include/`、两种架构包装及调用方，保留结构大小断言；i386 包装必须保存 EBX、ESI、EDI、EBP。用户指针及其完整范围必须验证，变长结果使用 query + capacity。
- 汇编保存顺序与 C 结构是内部 ABI；修改时检查任务初始栈、切换、fork、信号和返回路径。返回用户态前校验完整 frame；除合法 COW 和 lazy-FPU 恢复外，普通用户异常终止任务，内核异常及 NMI/双重故障/机器检查停机。
- 正式应用使用原生 `ET_DYN` PIE、`/lib/ld.so` 和 `libp.so`，C++ 另用 `libcpp.so`；统一经 `apps/libp/entry.c` 初始化后调用 `main`。静态自举解释器与 i386 TCC SDK 单独构建，不混用 PIC/非 PIC 归档。
- 动态链接只在 `apps/ldso/` 中实现。内核经 `loader_start_t` 交付原 ELF fd 和路径，从系统启动盘加载解释器；ELF 与启动 ABI 由 `apps/include/elf.h`、`loader.h` 单源定义。库搜索、重定位、构造/析构及 VM 权限规则见 [动态链接](doc/dynamic-linking.md)。
- ELF/VM 映射须检查大小、溢出、地址冲突和权限，失败回滚，拒绝 W+X；i386 保持 CR0.WP，区分 COW 与真正只读页。用户初始栈和堆用共享零页按需分配，写入时走 COW；COW 缺页分配失败终止所属任务。用户页表先分离内核共享映射，物理页引用只经正式 page API 维护；线程退出不能回收地址空间仍持有的页。
- 系统盘必须含 `init.bin`、`psh.bin`、`sys.cfg`、`lib/ld.so` 和 `lib/libp.so`，探测失败必须 panic。启动扇区、加载地址、ELF 入口或磁盘布局变化须检查 loader/kernel 两侧并完整构建、冷启动。

## 并发与资源生命周期

- 普通临界区成对使用 `irq_save()`/`irq_restore()`，保留调用者中断状态。等待路径在同一临界区检查条件、发布等待并处理 ready 竞态；调度启动后用 waiter/timer 阻塞，不持有 kernel lock 忙等。
- IRQ、异常和 syscall 入口按现有约定进入/离开 kernel lock；可能调度后重新读取当前 CPU。IRQ 回调不分配、不阻塞、不自行 EOI 或切换任务，由统一分派器完成 EOI 和调度；ISA 使用独占注册，PCI INTx 使用共享注册。
- 任务资源全部构造完成后调用 `task_publish`，失败用 `task_abort_creation`；启动参数及输入队列归新任务所有；任务退出取消 waiter/timer 并释放所属资源，内核栈只在切离后随任务槽回收。任务注册表通过迭代器访问，TID/页引用不得收窄为 8 位，异步引用用 TID/generation 识别。
- 保持每 CPU 的 current、idle 和运行队列，只有 BSP 推进全局时钟及 timeout。调度器不可自切换；BSP 在资源就绪且释放最外层 kernel lock 后唤醒 AP，AP 等待 release 时休眠。
- 实现同步跨 CPU TLB shootdown 前，同一地址空间的整个任务组固定在同一 CPU；跨进程共享映射只修改未在 CPU 上运行的目标。x86_64 页表修改统一经 TLB 失效接口处理当前和缓存的 PCID，不能依赖地址空间切换刷新；共享内核映射须覆盖所有 PCID。i386 BIOS/VBE 仅在 BSP 执行。
- DMA 使用正式 page/DMA/MMIO API 和驱动持有的缓冲，不指向等待调用者的栈或用户地址。硬件等待使用单调 deadline；失败不自动重放写入。停止设备并确认不再 DMA 后才释放资源，无法确认时禁用 bus master 并隔离相关页。

## 子系统边界

### Shell、输入与 GUI

- `kernel/cmd/` 只适配用户态 `psh.bin`，shell 与普通程序统一走 `os_execute`。`psh -c` 直接分派 `argv[2..]`，交互解析与外部命令构造复用 `runtime_args.c`；按命令语义校验 argc，目录不能遮蔽同名 `.bin`。重挂载命令统一为 `remount_drive X:`。
- psh/Lua 行编辑共用 `apps/third_party/pl_readline`。PS/2 与 USB 共用 `input_device.c` 的设备状态和逻辑键码；完整鼠标事件由 `mouse_read(mouse_event_t *)` 传递，不在用户态解码 PS/2 字节。
- `getch()` 的字符、Ctrl 控制字符与导航键统一由内核转换，键值单源定义在 `apps/include/key_input.h`；Escape 为 ASCII 27。`editor.bin` 使用 `apps/third_party/pl_editor` 的 C 核心和 `apps/editor/` 平台层，经 VFS 读写文件、经 ANSI 终端显示，不引入私有键盘解码器或终端解析器。使用与回归见 [编辑器](doc/editor.md)。
- 键盘只投递给有效输入 owner 或前台 TTY；TTY 所有权决定前台，阻塞使用 `WAIT_REASON_KEYBOARD`/`input_wait()`。同步执行在发布子进程时原子交接 TTY，仅交接调用者自己的鼠标所有权；返回时不能覆盖其他 owner。
- `AddThread(name, entry, stack_top, argument)` 显式传四个参数，入口由架构建立对齐调用帧；调用者不手写栈槽。活动 TTY 与 `tty_session` 分开维护，销毁 TTY 前迁移会话并清理相关任务、FIFO、栈与窗口。
- GUI 是单例 `gui` RPC 服务，须在切换显示和获取输入前注册。客户端只持有不透明窗口句柄及经过 owner/generation 校验的共享区域；焦点显式维护。终端由独立的 C 应用 `term.bin` 提供，直接链接 os-terminal 的对应架构静态库，默认通过 fartty 启动 `psh.bin`；GUI 不内置终端；图层动态增长，像素归属使用稳定指针。窗口绘图与提交缓冲使用独立 VM 映射，关闭或 owner 退出后解除映射。终端订阅窗口事件通知并在空闲时阻塞。fartty 原样转发 ANSI 字节，绕过内核旧 VT100 解析器；term 关闭 auto-flush，合并输出后显式刷新并同步提交窗口，有按键时调用 `tty_notify_input`。`fartty` 通过同一 IPC/RPC 协议调用所属服务，使用不透明 TTY 句柄及 TID/generation 校验；内核不得切换到用户地址空间执行回调，RPC 等待不得消费应用消息。依赖与验证见 [终端](doc/terminal.md)。
- SDL 唯一实现为 `apps/sdl3`，应用直接使用 SDL3、SDL3_image 与 SDL3_ttf API，不引入 SDL2 兼容层。GUI 合成读取已提交画面，`window_present` 应答后客户端才复用绘图缓冲，`window_refresh` 保留异步 damage 合并。显示布局以 `framebuffer_info()` 的实际尺寸、pitch 和颜色位序为准；framebuffer 别名保持相同缓存属性。详情见 [显示与 SDL](doc/multiarch.md)。

### 文件系统与设备

- 路径解析唯一实现在 VFS；驱动只接收目录 node 和单个 component。`X:` 从指定盘根开始，`/` 从当前盘根开始，相对路径从 cwd 开始；rename 不跨挂载，打开文件和 cwd/祖先目录的 unlink/rename 返回 `EBUSY`。
- `vfs_context` 封装 cwd 与 fd 表：线程共享，普通新进程继承 cwd，fork 复制 fd 表并共享 open-file description。挂载区分 `INITIALIZING`、`ACTIVE`、`RETIRED`；卸载先摘除再等引用归零，format 与挂载初始化/销毁互斥且拒绝有引用的盘。存在旧挂载引用或 format reservation 的盘符不能复用，旧句柄不得访问新介质。
- 用户文件操作统一经 `SYSCALL_VFS`，`FILE` 为不透明类型。读写返回实际字节数或错误；`mkdir`、`chdir`、`format` 等包装成功返回 0，失败返回 -1 并设置 `errno`，调用方按标准语义判断。
- `vdisk` 保留 64 位容量/LBA、真实 I/O 状态及设备传输能力，分批 I/O 间响应重调度，`fsync` 下传设备同步。FAT/PFS 只探测 512-byte 块盘，ISO9660 使用 2048-byte 光盘接口；保留 A:/B:/C: 的软驱/DEVFS/legacy IDE 槽位。
- 文件缓存保持 write-through，写成功后才更新或失效缓存；连续簇/扇区批量 I/O，避免逐页拆分及多余复制。格式化先校验全部输入和布局，初始化保留区并回读验证，底层失败传播到 VFS、shell 和安装器。
- PCI 统一经 `pci.h` 枚举与访问，驱动从注册表查询；ECAM 覆盖范围内的独立根总线不能遗漏，无对应 ECAM 时仅 segment 0 可用 CF8/CFC。BAR 探测只在接管静止设备时进行，配置访问遵守真实寄存器宽度及 W1C 语义。
- `pci_irq_initialize` 按 MSI-X、MSI、共享 INTx 选择中断，Interrupt Line 为 255 不阻止消息中断；注销前停设备并关闭中断源。USB/Hub/HID/BOT 与 AHCI 的支持范围、DMA 生命周期和专项回归见 [USB](doc/usb.md)、[AHCI](doc/ahci.md)。

### 网络与计时

- 网络协议唯一实现为 `kernel/net/third_party/lwip`，使用 `NO_SYS=1` raw API；`net_stack.c` 管链路/异步 DHCP，`socket.c` 管用户端点，网卡驱动只管帧、DMA 和 IRQ。普通路径以 IRQ 临界区串行 raw API，回环 drain 不在 output callback 中重入协议栈。
- `lo` 独立于网卡和 DHCP；socket 句柄按 task group 所有，阻塞由 `WAIT_REASON_SOCKET` 与 callback/timeout 唤醒。用户网络统一经 `SYSCALL_SOCKET`，不向用户暴露内核指针或增加第二套协议实现。
- `clock()` 保持毫秒 ABI，高分辨率计时使用 `monotonic_ns()`；调度后的 sleep 使用阻塞 timer。RTC 提供 UTC，`localtime`/`mktime` 当前为 UTC+08:00。
- 正常热路径不输出逐次分配、裸地址或输入报告。性能采样 IRQ 仅写固定聚合表，格式与控制 ABI 同宿主解析器同步；采集和火焰图说明见 [性能分析](doc/performance.md)。

## 验证与交付

- 仅文档修改运行 `git diff --check`，无需重建镜像。代码修改先做相关构建；涉及启动、驱动、文件系统、调度或 ABI 时，再完整构建并做 QEMU 串口回归，共享逻辑覆盖两种架构。
- i386 磁盘冒烟使用 `make -C kernel img_run`，LiveCD 使用 `livecd_run`；旧 `run`/`full_run` 引用当前不生成的软盘镜像，不能作为唯一依据。无 KVM 时使用 TCG 和合适的 CPU 型号，不修改内核迁就宿主；QEMU 用 `-serial stdio` 收集结果。
- 自动执行系统内命令只临时修改 `kernel/res/init.mst`；测试后恢复它、涉及的 `sys.cfg`/Limine 配置及正常镜像。禁止 QEMU monitor `sendkey`；QMP 输入事件只用于键鼠交互验证，不能注入测试命令。
- LiveCD 启动改动同时检查 ELF/Multiboot2 布局、默认 initramfs 冷启动及 i386 菜单的 DOSLDR 硬盘 chainload。图形/输入改动须验证实际事件和像素，不能只靠编译或单张截图。

常用回归入口：

```sh
python3 scripts/test-x86_64.py --firmware bios --memory 6144
python3 scripts/test-x86_64.py --firmware uefi --capacity
python3 scripts/test-x86_64.py --arch i386 --dynamic --memory 512
```

| 改动范围 | 回归入口或脚本选项 |
| --- | --- |
| IPC/RPC、磁盘、网络 | `rpctest.bin`、`dktest.bin`、`nettest.bin` |
| 任务、异常、浮点 | `guitest.bin stress`/`capacity`、`--memory-pressure`、`exc_test.bin`、i386 `fputest.bin`、x86_64 `simdtest.bin` |
| GUI、输入、SDL、工具 | `--mouse`、`--console`、`--editor`、`--sdl`、`--terminal-load COUNT`、`--desktop-app`（`lite` 或 `nk`）、`--tools` |
| 动态链接与全部应用装载 | `--dynamic`、`--all-apps`，见 [动态链接验证](doc/dynamic-linking.md#验证) |
| USB、PCI、AHCI | `--usb`、`--usb-hubs`、`--usb-irq`、`--usb-root-bus`、`--ahci --machine q35`；故障与模式组合见对应专题文档 |
| APIC、SIMD 与 TLB 后端 | `--apic`、`--cpu`、`--tlb`，见 [多架构说明](doc/multiarch.md) |

脚本选项按需选择，完整参数用 `python3 scripts/test-x86_64.py --help` 查看。交付前确认只包含任务需要的文件、ABI/构建/打包规则已同步，运行 `git diff --check`，说明实际完成和未完成的验证。
