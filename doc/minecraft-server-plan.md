# 原版 Minecraft 服务端与 JIT 改进计划

[开发指南](development.md) · [OpenJDK](openjdk.md) · [验证要求](testing.md)

本文基于 2026-09-14 的源码、构建参数、原始 JAR 字节码和现存测试日志做静态分析。尚未实施修复、重新构建或运行新的客户机测试；下文的验收条件均为后续工作，不表示已经通过。

## 目标与基线

将需求中的“原始镜像”理解为：使用升级后的 Plant OS 原生 JDK 和系统镜像，直接运行字节不变的 `/mnt/e/server.jar`，不再替换 Minecraft/Log4j 类、不删除签名，并启用默认 C1/C2 分层编译。宿主路径部署到客户机后使用 Plant 盘符路径，不要求客户机具有宿主 `/mnt/e` 挂载布局。

本次核实的输入如下：

| 项目 | 静态核实结果 |
| --- | --- |
| Minecraft | `version.json`：1.20.1，协议 763，Java 17 |
| 原始外层 JAR | 47,791,053 字节；SHA-256 `3af73a9dc5a102e38147946360dd27d4d70bae7055bf91cf2151cd5d121b79e0` |
| 内层服务端 JAR | `META-INF/versions/1.20.1/server-1.20.1.jar`；SHA-256 `80db52b203ac5de6e5fc1c5082259df440fb2b5390b4c61d474e8fbc63cc41f5` |
| 完整性 | `versions.list`、`libraries.list` 共 30 项 SHA-256 均匹配；内层保留 `MOJANGCS.SF`、`MOJANGCS.RSA`。本次未执行证书链/密码学签名验证 |
| 主要库 | Log4j 2.19.0、Netty 4.1.82.Final、JNA 5.12.1 |
| 原生 JDK | OpenJDK 17.0.19，BSD 平台层与 Plant ABI，Server VM 仅构建 `compiler1 compiler2 serialgc` |

`-Xms/-Xmx` 是可保留的资源配置；`eula.txt` 是独立运行配置。最终不依赖 `-Xint`、编译排除列表、强制 C1、`-Dlog4j2.disable.jmx=true`、强制 CPU 数或修改世界类型才能运行。先用平坦世界做快速回归，再以正常地形的新世界和已有世界验收。原生客户端、LWJGL、图形和声音不在服务端任务范围内。

## 主要结论

问题可以收敛为三个基础能力方向：HotSpot 代码缓存写入完整性、JDK 功能构建与模块一致性、Plant 文件/网络 ABI 语义。现有证据不支持通过继续给 Minecraft 打补丁解决，也不支持优先重写调度器或 JIT。

| 现象 | 当前判断 | 证据强度与优先级 |
| --- | --- | --- |
| `FileChannel.tryLock()` 失败 | VFS 未实现记录锁 | 确定缺失；P1 |
| C1 崩溃 | 常量对齐补零直接写入代码缓存 RX 视图，遗漏写别名转换 | 历史崩溃、机器码、源码一致；P0 |
| C2 崩溃 | 具体故障路径需单独取证；共享写路径是首要审计对象 | 尚无本次可归属到 C2 的完整崩溃记录；P1 |
| Management/JMX 查询失败 | VM 编译时关闭 `management`，Java 模块/本地库仍调用 JMM | 配置和历史异常一致；P1 |
| JFR 失败 | `jdk.jfr` 模块可见，但 VM 未包含 JFR；MC 按模块存在选择实现 | 原版字节码和历史异常一致；P1 |
| 所谓“跳过 PID 写入” | 现存补丁实际跳过崩溃报告预初始化，保留 PID 分支 | 原始及最终补丁字节码确认；需修正问题归因 |
| 启动日志滚动被禁用 | 启动时间查询依赖 Management；不能据此断定 rename/滚动底层不可用 | 共同依赖已确认，实际滚动需恢复后验证；P1 |
| 启动后 `Accept failed` | NIO 直接调用原生 `accept`，未将 Plant 错误转换为 POSIX `errno` | 调用链明确，历史日志吻合；P1 |

### 1. JIT：已经定位到可解释的 C1 写保护异常

从既有 `/tmp/plant-mc-attempt.Te1HYx/cwd-jdk.img` 只读提取的 `java/mc/hs_err.log` 显示：

```text
JavaThread "C1 CompilerThread0"
Current CompileTask: C1 ... dhu::a (251 bytes)
V [libjvm.so+0x359a78] AbstractAssembler::start_a_const(int, int)+0xc8
V ... LIR_Assembler::const2reg(...)
si_code: SEGV_ACCERR
si_addr: 0x00003ffff4e6d01c
ERR=0x7, TRAPNO=0xe
BufferBlob ... used for C1 temporary CodeBuffer
```

故障指令为 `movb $0x0,(%rax)`：这是 VM 编译线程写内存，不是已编译 Java 方法执行非法 SIMD 指令。页故障错误码 `0x7` 表示用户态对已存在页进行受保护写。堆栈剩余约 1020 KiB，也不支持将此例优先归因于编译线程栈溢出。

对应源码 `src/hotspot/share/asm/assembler.cpp:84` 的 `start_a_const()` 在对齐常量区时仍使用：

```cpp
while (--pad >= 0) { *end++ = 0; }
```

与此同时，`BufferBlob::create()` 会把内容设为可执行，`CodeHeap::make_executable()` 使用 RX；`CodeSection::emit_int*()`、浮点常量写入等则已经通过 `code_cache_write_address()` 转到 RW 别名。因此存在“普通发射已适配、对齐补零遗漏”的具体不一致。临时 JDK 与当前 `images/jdk-jit` 的 `libjvm.so` SHA-256 相同，反汇编也匹配故障位置：

```text
426a006618d93592bcde0bef1460c8f07281d7edee978156d72d35db04a55395
```

这是高置信度根因定位，仍须通过最小复现和修复后回归建立因果闭环。此例启动参数已经排除 Fastutil 编译，仍在另一方法失败，说明按 Java 包名排除编译不解决通用写路径缺陷。

同一磁盘还保留 `replay_pid7.log`，末尾为 Fastutil `Object2ObjectOpenHashMap.<init>(IF)V`、编译级别 1。它与后来覆盖的 `hs_err.log` 不是同一次故障，不能拼接成一个调用链，也不能当作 C2 证据。

**修复方向：**

- 复用现有 CodeSection/CodeCache 写接口，让发射、补齐、清零、复制、重定位和后续修补具有同一规则。对齐时一次取得写地址并批量补零；PC、标签、相对位移、范围判断始终保留执行视图地址。
- 精准审计共享 assembler/codeBuffer、x86 relocation/nativeInst、C1 延迟修补、C2 常量/跳转表、GC 内嵌 oop 更新、IC/stub 和入口失效。`CodeBuffer::copy_relocations_to()` 中也有未经转换的 filler 写入；必须先核对目标是否为可写元数据区，再决定是否修改，不能把所有指针赋值机械替换。
- 复查 `BufferBlob` 临时编译缓冲与真正执行 stub 的生命周期。目前它们共用创建时 RX 的策略；先保证统一写入口完整，不为这一个故障引入到处传递的布尔标记。只有现有类型/用途能清晰表达时，再分开 RW 临时缓冲和待发布代码。
- 保持既有连续别名、代码/元数据页边界、原子热修补和缓存同步规则，不新增 RWX 页，不在信号处理器中将错误写入页面临时改成 RW 来掩盖遗漏。

**C2 后续排查顺序：**共享写路径 → 扩容/回收/重定位 → 去优化和安全点 → SIMD/TLS/跨 CPU 上下文。仅当 PC、故障地址、编译级别和寄存器支持时，才提高后几项优先级。`-Xint` 不产生此故障只能表明与编译路径相关，不能证明所有 JIT 故障同源。

### 2. Management：修 VM 构建，不逐个禁用调用者

`scripts/build-openjdk-jit.py:49` 明确指定 `compiler1 compiler2 serialgc`；现存 `management.o.cmdline` 包含 `-DINCLUDE_MANAGEMENT=0 -DINCLUDE_JFR=0`。源码中：

```text
Management::get_jmm_interface() → 未包含 management 时返回 NULL
libmanagement JNI_OnLoad()     → 抛出 Unsupported Management version
```

历史 `latest.log` 有完全对应的异常，之后 `ManagementFactory` 处于类初始化失败状态，再次访问会抛 `NoClassDefFoundError`。Netty 的直接内存估算、Minecraft 崩溃报告、Log4j 参数查询都会碰到这条依赖。

原版 Log4j 的 `JmxRuntimeInputArgumentsLookup.<clinit>` 直接调用 `getRuntimeMXBean().getInputArguments()`；`OnStartupTriggeringPolicy` 反射调用 `getStartTime()`，且已有捕获 `Throwable` 的回退。禁用 JMX 注册并不会消除所有 Management API 调用。现有临时覆盖类中“cgroup 查询不可用”的注释不能当作根因证据，已保存的异常指向缺失 JMM。

**修复方向：**

- 在正式 Server VM 功能配置中开启 `management` 及上游要求的 `nmt`。当前 `make/autoconf/jvm-features.m4:546` 明确要求两者同时启用；不能仅在 make 命令行追加一个 feature 绕过 configure 校验。
- 同步重建匹配的 `libjvm`、`java.management`、`jdk.management` 本地库/模块和镜像，核对 ABI、导出符号和模块内容。用功能探针验收，而不只看模块名存在。
- 支持 RuntimeMXBean 的参数、启动时间、uptime，以及应用使用的线程、内存、GC、OS 和 Compilation MXBean。可选测量不支持时按 JDK 契约报告，不伪造数据，不把整个 ManagementFactory 变为不可初始化。
- 恢复原版 Log4j 参数查询、启动滚动、Minecraft 报告初始化；先测本地 MBeanServer 注册和查询。远程 JMX/RMI 监听不是原版服务端启动的前置目标，`services`/attach 依赖按实际功能分别验证。

### 3. Main 补丁：实际不是 PID 写入

现存 `patch_main.py` 把 `b8 00 c9` 改为三个 NOP。原始 1.20.1 的常量池 `#201` 对应 `o.h()`；最终 `server-mc-final.jar` 的 Main 与已检查的补丁 Main 完全相同，字节码变化为：

```text
236 ... 检查 --pidFile 参数
243 invokestatic #196  a(Path)  // PID 写入，仍保留
246 invokestatic #201  o.h()    // 被替换为三个 NOP
```

`o.h()` 创建 “Don't panic!” 的崩溃报告并格式化结果，是报告系统预初始化。结合历史 Management 初始化错误，应该首先恢复这条依赖，而不是据此新增一套 PID 系统调用。

`ProcessHandleImpl_getCurrentPid0()` 已调用原生 `getpid()`，Plant BSD 后端也有当前进程查询。其子进程枚举、CPU 时间和启动时间实现仍有限，但当前证据不能断言 PID 写入不可用。

**计划：**单独验证 `ProcessHandle.current().pid()` 在不同线程中一致、不同进程可区分，再验证原版 `--pidFile <path>` 写入、错误目录和读回值。仅有实际失败时才补相应路径/进程语义；完整跨进程枚举和控制不作为此次启动的先决条件。修复 Management 后用原版 Main 验证报告预初始化和失败报告生成。

### 4. JFR：模块存在与 VM 能力必须一致

原版 `bat.<clinit>` 检查 `Runtime.class.getModule().getLayer().findModule("jdk.jfr").isPresent()`，然后选择 JFR 实现。它并非在这一步检查 `FlightRecorder.isAvailable()`。历史日志中，世界加载进入 `WorldLoadFinishedEvent.<clinit>` 后抛出：

```text
java.lang.InternalError: Flight Recorder is not supported on this VM
```

因此仅让可用性查询返回 false，或不传 JFR 启动参数，都不足以保证原版走空实现。

**推荐方向：**交付包含真实 JFR 的 Server JDK。沿现有 OpenJDK 平台层补齐事件注册、线程/时钟信息、缓冲、采样与录制文件路径；审核 BSD `os_perf_bsd.cpp` 的平台分支和“不支持”返回，不能将编译成功等同于可录制。Management/NMT、JFR 及需要的服务功能用一份经过 configure 依赖校验的配置维护。

验收包括原版事件类初始化、无录制时正常运行、录制开始/停止、生成文件由宿主 JFR 工具读取、录制中 GC/退出和文件滚动正确。首先保证事件与录制主路径，缺少的可选硬件指标如实报告。

不包含 JFR 模块的定制运行时理论上也能让原版 MC 自行选择空实现，可作为分阶段诊断方案；它需要同步处理依赖模块，并另行标注能力范围。本计划以真实 JFR 支持为正式方向，不以删除 `bat.class` 行为或空壳模块完成验收。

### 5. 文件锁：VFS 内实现真正的进程记录锁

链路已定位：`FileChannel.tryLock()` → `FileDispatcherImpl_lock0()` → `fcntl(F_SETLK)` → `vfs_fd_fcntl()`。后者在 `kernel/fs/vfs.c:1972` 对三种锁命令统一返回 `VFS_ERROR_NOT_SUPPORTED`。Java 将 `Long.MAX_VALUE` 锁范围转为 `l_len=0`，即延伸至 EOF 及后续增长部分，不能按当前文件长度截断。

**设计与边界：**

- 锁归属于进程/task group，冲突按稳定的挂载身份与文件节点身份识别，复用 VFS 现有身份机制。不同路径、独立 open、FAT 长短名称不能获得互不相干的锁；不能以文件名或 fd 数字作为锁身份。
- 按文件组织有序区间记录，封装 owner、范围、读/写类型与等待者，支持同 owner 的区间拆分/合并、共享锁和排他锁。先使用能覆盖需求的简单结构；不全表扫描所有进程，也不先引入通用锁服务或复杂树。
- 实现 `F_GETLK/F_SETLK/F_SETLKW`、`F_RDLCK/F_WRLCK/F_UNLCK`、SEEK_SET/CUR/END、零长度和合法负长度的规范化；统一检查有符号范围、溢出、fd 打开方式及文件类型。锁区间使用 64 位表达，不能沿用当前 handle 的 32 位 offset 作为锁范围类型。
- 遵循 POSIX 进程记录锁语义：同进程关闭指向该文件的任意 fd 会释放该进程在该文件上的记录锁；fork 不继承这些锁；线程共享 owner；进程退出释放并唤醒。不能只在 open-file description 最后一次引用释放时清理。
- 非阻塞冲突转换为 Java 识别的 `EAGAIN/EACCES`；阻塞锁使用现有 waiter/scheduler，支持取消、信号中断、进程退出及必要的死锁检测，不持 kernel lock 自旋。唤醒、重查冲突与重新阻塞需要统一竞态处理。
- 通过现有 VFS syscall 承载。当前 `fcntl.argument` 是裸地址值，增加锁结构访问时必须先校验并复制完整用户范围；`F_GETLK` 校验写回，等待期间只保留内核副本。公开结构与 ABI 布局在两架构核对并加断言，不直接套 Linux `struct flock`。

验收既覆盖两进程对同一 `session.lock` 互斥，也覆盖同 JVM 重叠锁异常、共享锁、部分解锁、独立 open、dup/close、fork/退出、超大范围和阻塞中断。锁是建议锁，不把全部普通文件 I/O 改成强制锁。系统重启后旧 `session.lock` 文件仍在，但应能重新取得锁。

### 6. 网络：原版 NIO 接入还需要错误语义修复

历史成功加载世界的日志同时含有多次 `java.io.IOException: Accept failed`，也有玩家成功加入记录。因此不能说网络完全不可用，也不能把“端口已监听”当作稳定网络验收。

当前可确认的接口不一致：

```text
kernel/net/socket.c::net_socket_accept
  非阻塞队列为空 → NET_SOCKET_ERR_AGAIN
apps/libp/socket.c::accept
  原样返回负的 Plant 错误值，不设置 errno
libnio/ch/Net.c::Java_sun_nio_ch_Net_accept
  调用 accept，随后只根据 errno 判断 EAGAIN/EINTR
```

`bsd_close.c::NET_Accept` 已有另一条错误转换，但此 NIO 路径没有使用它。Netty 每次就绪后可能继续 accept，直至队列为空；此时正常的“暂不可读”很可能被翻译成 IOException。历史日志没有保留调用时 errno，需用队列耗尽探针作最后确认。

**方向：**在运行库边界统一 POSIX socket 返回值和 errno；保留清晰的内核内部错误类型，并将原生专用调用界限显式化。移除 JDK 中重复/冲突的转换；同步迁移依赖旧负值的仓库调用者，不保留两套同名接口语义。范围覆盖实际使用的 accept/connect/send/recv、readv/writev、poll、shutdown/close 和 SO_ERROR，避免只修一个 accept 点。

必须新增“接收完当前连接后再次非阻塞 accept 返回 unavailable”、多连接、部分读写、背压、断开/重连、Selector wakeup、阻塞关闭与中断的行为测试。现有 NIO 成功连接测试不能代替队列耗尽覆盖。默认使用原版可选的 Java NIO 通道，不通过伪装 `os.name=Linux` 加载 Linux epoll/JNA 本地库。

### 7. CPU、内存、存储与可诊断性

- `sysconf(_SC_NPROCESSORS_ONLN)`、`HW_NCPU` 已返回 `cpu_count()`，HotSpot 有对应读取路径。移除 `ActiveProcessorCount=4` 后用 1/2/4 个 vCPU 核对 Java 与 MXBean 结果，再判断是否需要修复；此参数本身不证明 CPU 探测有缺陷。
- 现有 Server 仅含 Serial GC。先用它关闭正确性问题，再评估 G1 对正常地形和多人负载的停顿收益；G1 不是修复 RX 写错误的替代方案，也不作为第一阶段的强制扩展。
- 内存预算须包括 Java 堆、metaspace、压缩类空间、代码缓存、线程栈、Netty 直接缓冲、VFS cache、内核及 JDK 模块映射。RW/RX 别名应共用物理页，不能重复计算为两份代码内容。先确认 NMT/OS 统计准确，再调整预算。
- 恢复真实启动滚动后验证时间戳、关闭后 rename、创建新日志与旧日志压缩。世界保存验证 FileChannel 写入/force、文件替换、重启读回和磁盘写失败传播；不能为了通过测试吞掉 `EBUSY` 或磁盘错误。
- 历史 OSHI “Unknown” 出现在硬件诊断路径且被报告系统捕获，应与真正的启动失败区分；先保证诊断降级可用，不为可选硬件信息伪造 Linux 环境。
- 每次运行独立保存 stdout/stderr、退出状态、`hs_err`、编译日志、GC/安全点日志、镜像与二进制标识。旧日志不能被新一轮覆盖；JVM 尚未初始化时也必须能采集原生错误，正式验收直接 `java -jar`，不能依赖自定义 Runner 改全局异常处理器。

## 分阶段实施与交付

按下列次序推进；每阶段单独形成可审查的补丁和有因果关系的回归结果。表中复杂度是工作范围判断，不是完成日期承诺。

| 阶段 | 工作与交付物 | 完成条件 | 复杂度/主要风险 |
| --- | --- | --- | --- |
| P0-A 基线与可复现构建 | 固定上游源码版本/校验和、Plant 适配、feature 配置与工具链；保存原版 JAR 和现有失败证据 | 构建输入和模块/VM 能力可追踪，不混用旧库；新增修复不依赖未记录的 `apps/out` 手改 | 中；现有脚本并非完整移植入口 |
| P0-B JIT 首个闭环 | 常量对齐复现、统一写路径修复、共享写入审计；保留 release 符号并提供 fastdebug 诊断构建 | 修复前探针失败、修复后通过，C1/C2 计算及 nmethod 证据通过 | 首个点小，完整审计中；C2 仍有未知路径 |
| P1-A JDK 功能 | Management+NMT、真实 JFR、匹配模块/库；恢复原版 Main/Log4j 行为；PID 探针 | ManagementFactory、JFR 事件/录制、报告生成、日志滚动和 PID 文件通过 | 中到高；平台统计/录制和构建依赖 |
| P1-B VFS 记录锁 | 文件身份、区间锁、owner 生命周期、阻塞与 ABI | C 和 Java 两层通过冲突、退出释放、范围和中断测试 | 中到高；关闭/fork/等待竞态 |
| P1-C 网络边界 | POSIX socket 错误语义与调用者迁移 | 原版 Netty 正常处理 accept 队列耗尽，多连接/背压/关闭正确 | 中；运行库调用者范围 |
| P2 原版集成 | 独立测试盘、无补丁 JAR、标准 `-jar` 启动；移除临时限制 | 默认分层编译、正常世界、联网、保存/重启和双实例拒绝全部通过 | 中；可能暴露更多文件/网络边界 |
| P3 性能与持续回归 | 固定负载、时延/CPU/内存数据；按证据评估 GC 和热点 | 无持续 tick 积压、无内存持续增长，正常负载有可重复数据 | 取决于基线，不预先重写子系统 |

P0-A 先保存能够对应到二进制的故障证据，构建可复现整理按后续实际改动逐步补齐，不以清理整个 OpenJDK 移植历史阻塞第一个复现。P0-B 首轮只处理已有证据支持的写路径；P1 各项可独立评审，但原版完整验收必须全部满足。

正式维护位置沿用 `apps/openjdk/patches/`、现有构建/回归脚本和原生子系统。不把成果只留在 `apps/out/sources` 或 `/tmp`，不把修改后应用 JAR 纳入发行镜像。新增测试优先扩展现有 Java/C 回归；Minecraft 集成使用一个独立入口，复用现有 QEMU/打包能力，不复制整个测试框架。

## 验证矩阵

### JIT 与基础 ABI

| 覆盖项 | 必须证明的行为 |
| --- | --- |
| 常量区 | 混合 float/double/整数/地址常量，不同对齐和跨页填充；运行时输入防止常量折叠，校验实际计算结果 |
| 编译器 | C1-only、C2-only 分别记录目标方法 nmethod；默认 tiered 覆盖 C1 到 C2、OSR、去优化、入口失效；`-Xcomp/-Xbatch` 仅作诊断压力 |
| 代码生命周期 | 小代码缓存下扩容、回收、重新分配、IC/stub 修补，确认没有向 RX 视图写入或执行已回收页 |
| 运行库/GC | Lambda/MethodHandle、反射、异常、数组/字符串、JNI 调用和并发分配；默认分层编译下仍需通过已有源码启动和客户机 javac |
| 平台 | TCG 多线程与单线程对照，KVM 可用时验证；qemu64 基础指令集与 `max,-xgetbv1` 的 AVX/YMM 路径；1/2/4 vCPU 按需定位并发差异 |
| 内核改动 | 按影响运行 signals、simd、tlb、dynamic、threads/futex；共享 VFS/运行库覆盖 i386 与 x86_64，不扩大成 i386 HotSpot JIT 移植 |

保留现有入口作为下限，修改后再补上述覆盖，不能用只计算整数循环的 `Jit.java` 宣称 Minecraft 全面兼容：

```sh
python3 scripts/test-openjdk.py --jdk <Plant-JDK-JIT> --javac <Host-javac> --jit c1 --accel tcg --repeat 4 --out <c1-out>
python3 scripts/test-openjdk.py --jdk <Plant-JDK-JIT> --javac <Host-javac> --jit c2 --accel tcg --repeat 4 --out <c2-out>
```

信号和 SIMD 的 TCG 型号约束沿用 [signals.md](signals.md)，不用模拟器已知 XINUSE 行为否定正确内核逻辑。不是每个小修都运行完整笛卡尔积；先最小复现，再相关回归，集成里程碑运行完整必要矩阵。

### 原版服务端验收

目标启动形式（路径和堆大小按测试盘资源配置）：

```text
C:/java/bin/java -Xms256m -Xmx1536m -jar C:/java/mc/server.jar nogui
```

1. 独立目录首次解包并校验内层 JAR/库摘要；重复启动不依赖之前遗留的修改版 `versions/`、`libraries/`。原始签名保留，签名验证区分摘要失败与证书时效提示。
2. 不带 `--pidFile` 和显式指定 PID 文件分别通过；同一 JVM 原版报告预初始化与 Management 查询成功。
3. 在平坦世界快速冒烟后，以正常地形新世界进入 `Done`；已有世界也可加载。测试记录世界种子、视距、模拟距离和数据目录。
4. 编译日志确认 Minecraft/依赖热点实际产生 C1/C2 nmethod；没有包级排除、强制解释执行、强制单编译器等残留参数。
5. 第二个独立进程对同一世界明确被锁拒绝，不能破坏第一实例；使用不同端口或独立锁探针，避免“端口占用”冒充锁验证。第一个实例退出后可重新启动；进程异常退出后亦可重新取得锁。
6. TCP 状态查询、真实玩家登录、移动/交互、连续区块生成、多个连接和重连；在线认证模式与 DNS/TLS/会话服务分别记录验证结果，不能只凭状态 ping 宣称在线登录正常。
7. JFR 录制/停止和有效文件读取，Log4j 首次启动/跨启动滚动均成功；不再出现 Management 初始化失败和正常 accept 队列耗尽导致的 IOException。
8. 正常 stop/save，退出后重启核对世界和玩家数据；通过正常控制台/受支持命令通道停止，不以杀死 QEMU 代替成功退出。自动脚本遵守仓库 init/QMP 规则，最终确认 ACPI S5。
9. 建议先运行 30–60 分钟可重复混合负载，再做至少 4 小时持续运行。记录 tick 时间 P50/P95/P99、GC 停顿、编译/加载耗时、直接内存、线程数和系统空闲内存；稳态负载以 20 TPS、50 ms tick 预算评估，首次生成与稳态分开统计。TCG 数据只用于正确性及同配置对比，性能结论优先用 KVM/真实硬件。

遇到新故障时保存该轮产物并缩小复现，不增加新的应用字节码补丁。原版启动成功、默认 JIT 正确、正常服务/保存/退出是三个独立门槛，必须分别提供证据。

## 本次静态分析的证据与限制

源码路径中的 `src/`、`make/` 默认相对于 `apps/out/sources/openjdk-17-17.0.19+10/`；这是已适配的本地树，并非未修改的上游树。关键可复查位置：

| 主题 | 位置 |
| --- | --- |
| 锁命令与用户指针边界 | `kernel/fs/vfs.c:1947`、`kernel/dos/syscall/syscall.c:529`、`apps/libp/posix.c:221`、`apps/include/fcntl.h` |
| JVM 功能选择 | `scripts/build-openjdk-jit.py:49`、`make/autoconf/jvm-features.m4:535`、当前构建 `management.o.cmdline` |
| 常量写遗漏与代码权限 | `src/hotspot/share/asm/assembler.cpp:84`、`src/hotspot/share/asm/codeBuffer.hpp:203`、`src/hotspot/share/code/codeBlob.cpp:242`、`src/hotspot/share/memory/heap.cpp:214` |
| 既有写别名适配 | `apps/openjdk/patches/plant-x86-hotspot.patch`、`src/hotspot/share/code/codeCacheWrite.hpp` |
| Management 失败路径 | `src/hotspot/share/services/management.cpp:2278`、`src/java.management/share/native/libmanagement/management.c:47` |
| 当前 PID 后端 | `src/java.base/unix/native/libjava/ProcessHandleImpl_unix.c:295`、`src/java.base/bsd/native/libjava/ProcessHandleImpl_bsd.c` |
| 网络错误边界 | `kernel/net/socket.c:1215`、`apps/libp/socket.c:70`、`src/java.base/unix/native/libnio/ch/Net.c:390`、`src/java.base/bsd/native/libnet/bsd_close.c:91` |
| CPU 探测 | `apps/libp/posix.c:113`、`apps/libp/sysctl.c:35`、`src/hotspot/os/bsd/os_bsd.cpp:2158` |

本次从既有磁盘提取的只读分析副本位于 `/tmp/plant-mc-static-analysis/`。这些临时文件不是永久交付物，后续实施先归档必要证据；本文已记录核心故障、调用链和摘要，避免只依赖临时路径：

| 历史文件 | SHA-256 |
| --- | --- |
| `hs_err.log` | `90ef145e7040bf2f3d8fd01e139484a69c39857f8e7d2bc21464d99c1b453650` |
| `latest.log`（含多个历史启动） | `42f7c06c67239e8b4ea1d2079d3745bbd82fe6c46d4a65cf8994e833ab3e8d43` |
| `replay_pid7.log` | `b73d7370e40b7ff714aea6bad4b70a874ae3b257a6f70398e44ef5eb5c73fb95` |

静态分析可以确认缺失实现和错误契约，并强力解释已保存的 C1 故障；不能保证上述修复足以覆盖全部 C2、网络、保存和长时间负载问题。下一步最有价值的工作是先构建 C1 常量对齐的最小复现并修正统一写路径，同时建立不会覆盖历史证据的回归记录。
