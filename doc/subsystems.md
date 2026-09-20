# 子系统边界

[开发指南](development.md)

### Shell、输入与 GUI

- `kernel/cmd/` 只适配用户态 `psh.bin`，shell 与普通程序统一走 `os_execute`。`psh -c` 直接分派 `argv[2..]`，交互解析与外部命令构造复用 `runtime_args.c`；按命令语义校验 argc，目录不能遮蔽同名 `.bin`。重挂载命令统一为 `remount_drive X:`。
- psh/Lua 行编辑共用 `apps/third_party/pl_readline`。PS/2 与 USB 共用 `input_device.c` 的设备状态和逻辑键码；完整鼠标事件由 `mouse_read(mouse_event_t *)` 传递，不在用户态解码 PS/2 字节。
- `getch()` 的字符、Ctrl 控制字符与导航键统一由内核转换，键值单源定义在 `apps/include/key_input.h`；Escape 为 ASCII 27。`editor.bin` 使用 `apps/third_party/pl_editor` 的 C 核心和 `apps/editor/` 平台层，经 VFS 读写文件、经 ANSI 终端显示，不引入私有键盘解码器或终端解析器。使用与回归见 [编辑器](editor.md)。
- 键盘只投递给有效输入 owner 或前台 TTY；TTY 所有权决定前台，阻塞使用 `WAIT_REASON_KEYBOARD`/`input_wait()`。同步执行在发布子进程时原子交接 TTY，仅交接调用者自己的鼠标所有权；返回时不能覆盖其他 owner。
- `AddThread(name, entry, stack_top, argument)` 显式传四个参数，入口由架构建立对齐调用帧；调用者不手写栈槽。活动 TTY 与 `tty_session` 分开维护，销毁 TTY 前迁移会话并清理相关任务、FIFO、栈与窗口。
- GUI 是单例 `gui` RPC 服务，须在切换显示和获取输入前注册。客户端只持有不透明窗口句柄及经过 owner/generation 校验的共享区域；焦点显式维护。终端由独立的 C 应用 `term.bin` 提供，直接链接 os-terminal 的对应架构静态库，默认通过 fartty 启动 `psh.bin`；GUI 不内置终端；图层动态增长，像素归属使用稳定指针。远程窗口使用两块互不重叠的共享像素平面，服务端维护已提交平面的所有权；关闭或 owner 退出后解除映射。鼠标双轴位移不得为图层对齐而量化，拖动期间保持标题栏捕获至释放；合成基准与逐像素验证见 [GUI 性能](gui-performance.md)。鼠标捕获、结构化事件和同步尺寸变更遵循 [GUI 输入与尺寸](gui-input.md)。终端订阅窗口事件通知并在空闲时阻塞。fartty 原样转发 ANSI 字节，绕过内核旧 VT100 解析器；term 关闭 auto-flush，合并输出后显式刷新并同步提交窗口，键盘按下与松开交给 os-terminal，库消费快捷键后保留普通输入的 `getch` ABI，有应用输入时调用 `tty_notify_input`。`fartty` 通过同一 IPC/RPC 协议调用所属服务，使用不透明 TTY 句柄及 TID/generation 校验；内核不得切换到用户地址空间执行回调，RPC 等待不得消费应用消息。依赖与验证见 [终端](terminal.md)。
- SDL 唯一实现为 `apps/sdl3`，应用直接使用 SDL3、SDL3_image 与 SDL3_ttf API，不引入 SDL2 兼容层。GUI 合成读取已提交画面，`window_present` 应答后客户端才复用绘图缓冲，`window_refresh` 保留异步 damage 合并；完整帧可用 `window_present_frame` 交接绘图平面，应答后重新获取下一块绘图缓冲，禁止继续写已提交平面，不能将同步交换伪装成异步复用。显示布局以 `framebuffer_info()` 的实际尺寸、pitch 和颜色位序为准；framebuffer 别名保持相同缓存属性。详情见 [显示与 SDL](multiarch.md)。
- JIT 编译器及其目标库运行在原生用户态；生成代码也必须遵循当前架构的调用约定、SIMD、无 red zone 和 W^X 约束，不能仅依赖构建编译器本体的选项。Mesa/lavapipe 的依赖、移植阶段和实际支持状态见 [lavapipe 移植](lavapipe.md)。
- x86_64 SDL renderer 可选择原生 lavapipe；普通应用使用 SDL 的后端选择，回归显式固定软件后端并断言默认选择为 Vulkan。SDL window surface 默认使用直接共享缓冲。Vulkan WSI 使用 `window_get_buffer` 的实际布局，在渲染完成且同步 present 应答后才复用图像；平台代码、上游补丁与宿主生成器统一维护在 `apps/mesa/` 和 `scripts/build-mesa.py`，不引入宿主驱动或装载器。
- x86_64 OpenGL 使用同一 Mesa 构建中的 llvmpipe 和原生 EGL，SDL 复用上游 EGL 上下文接口。GL dispatch/TLS 保持单一实现，当前 EGL 对象须保留至解绑；EGL 窗口每帧重新获取绘图平面，通过 `window_present_frame` 同步交接完整客户区，遵循 GUI owner 与缓冲所有权规则。经典 glxgears 仅替换窗口层，不引入 X11/GLX 兼容层；接口与验证见 [OpenGL](opengl.md)。

- x86_64 GLFW 使用 `apps/glfw/` 的上游核心和原生 GUI/EGL 后端；窗口创建显式传入标志，状态查询读取一致的共享快照。GUI 通知目标只允许同一 task group 中经过 TID/generation 校验的线程；GLFW 通过专用通知线程和私有 futex 等待，不消费应用线程的 IPC。功能边界与回归见 [GLFW](glfw.md)。
- x86_64 LWJGL 3 复用同一 `libglfw.so`、Mesa EGL/GL 和 Plant ELF/JNI 装载器；Java callback closure 由 libffi 的 RW/RX 别名分配器提供，窗口与 swap 仍在所属 Java 线程执行。提供 core、GLFW、OpenGL、STB 与 OpenAL 绑定，显式 native 目录是标准部署方式，支持矩阵与回归见 [LWJGL](lwjgl.md)。

- RenderTM 的 `rendertm.bin` 保留原版终端行为；独立 SDL3 前端为 `renderhd.bin`，两者共用渲染、控制及 C++ modules，不复制渲染实现。并行像素阶段之间须完成同步，窗口缓冲在同步呈现完成后才能复用。终端应用的指针位置经当前 TTY 的受校验 RPC 查询，不接管 GUI 输入 owner。构建与验证见 [RenderTM](rendertm.md)。

### 文件系统与设备

- ACPI S5 关机通过用户态 `shutdown` → `power_off()` → PC 平台实现；固件解析、设备同步、支持边界与回归见 [电源管理](power-management.md)。

- FAT12/16/32 与 FAT initramfs 共用 VFAT LFN 实现；磁盘名字使用 UTF-16LE，路径接口使用 UTF-8。创建、枚举、重命名和删除统一处理经过序号、校验和及编码验证的 LFN/SFN 组合；短别名保持唯一，不能以别名替代安装目标原名。名称容量及验证见 [FAT 长文件名](fat-lfn.md)。
- 路径解析唯一实现在 VFS；驱动只接收目录 node 和单个 component。`X:` 从指定盘根开始，`/` 从当前盘根开始，相对路径从 cwd 开始；rename 不跨挂载，打开文件和 cwd/祖先目录的 unlink/rename 返回 `EBUSY`。
- `vfs_context` 封装 cwd 与 fd 表：线程共享，普通新进程继承 cwd，fork 复制 fd 表并共享 open-file description。挂载区分 `INITIALIZING`、`ACTIVE`、`RETIRED`；卸载先摘除再等引用归零，format 与挂载初始化/销毁互斥且拒绝有引用的盘。存在旧挂载引用或 format reservation 的盘符不能复用，旧句柄不得访问新介质。
- 用户文件操作统一经 `SYSCALL_VFS`，`FILE` 为不透明类型。读写返回实际字节数或错误；`mkdir`、`chdir`、`format` 等包装成功返回 0，失败返回 -1 并设置 `errno`，调用方按标准语义判断。
- 目录流持有真实目录描述符；stat 的设备/节点身份由 VFS 维护，不暴露内核地址。realpath 和 truncate 走统一 VFS 操作。stdio 的文件与内存流共用实现，共享流须加锁，注册表与流锁按统一顺序获取；fork、失败回滚和部分写入必须保留一致状态。
- `vdisk` 保留 64 位容量/LBA、真实 I/O 状态及设备传输能力，分批 I/O 间响应重调度，`fsync` 下传设备同步。FAT/PFS 只探测 512-byte 块盘，ISO9660 使用 2048-byte 光盘接口；保留 A:/B:/C: 的软驱/DEVFS/legacy IDE 槽位。
- 文件缓存保持 write-through，写成功后才更新或失效缓存；连续簇/扇区批量 I/O，避免逐页拆分及多余复制。缓存、映射固定页与设备传输约束见 [文件读取与缓存](storage-cache.md)。格式化先校验全部输入和布局，初始化保留区并回读验证，底层失败传播到 VFS、shell 和安装器。
- PCI 统一经 `pci.h` 枚举与访问，驱动从注册表查询；ECAM 覆盖范围内的独立根总线不能遗漏，无对应 ECAM 时仅 segment 0 可用 CF8/CFC。BAR 探测只在接管静止设备时进行，配置访问遵守真实寄存器宽度及 W1C 语义。
- `pci_irq_initialize` 按 MSI-X、MSI、共享 INTx 选择中断，Interrupt Line 为 255 不阻止消息中断；注销前停设备并关闭中断源。USB/Hub/HID/BOT 与 AHCI 的支持范围、DMA 生命周期和专项回归见 [USB](usb.md)、[AHCI](ahci.md)。

### 网络与计时

- 网络协议唯一实现为 `kernel/net/third_party/lwip`，使用 `NO_SYS=1` raw API；`net_stack.c` 管链路/异步 DHCP，`socket.c` 管用户端点，网卡驱动只管帧、DMA 和 IRQ。普通路径以 IRQ 临界区串行 raw API，回环 drain 不在 output callback 中重入协议栈。
- `lo` 独立于网卡和 DHCP；socket 句柄按 task group 所有，阻塞由 `WAIT_REASON_SOCKET` 与 callback/timeout 唤醒。用户网络统一经 `SYSCALL_SOCKET`，不向用户暴露内核指针或增加第二套协议实现。
- `clock()` 保持毫秒 ABI，高分辨率计时使用 `monotonic_ns()`；调度后的 sleep 使用阻塞 timer。RTC 提供 UTC，`localtime`/`mktime` 当前为 UTC+08:00。
- `time_t` 在两个架构均为有符号 64 位秒；标准 timespec/clock_gettime/nanosleep 由原生包装提供。标量数学与 `fenv` 保留舍入及异常标志语义，i386 不执行 SSE；不能以简化公式替代 FMA 等要求单次舍入的操作。
- 正常热路径不输出逐次分配、裸地址或输入报告。性能采样 IRQ 仅写固定聚合表，格式与控制 ABI 同宿主解析器同步；采集和火焰图说明见 [性能分析](performance.md)。

匿名管道、混合文件/socket 的 poll、阻塞及关闭语义见 [管道与事件等待](io-poll.md)。
