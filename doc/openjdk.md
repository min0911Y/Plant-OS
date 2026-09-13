# OpenJDK

[开发指南](development.md) · [动态链接](dynamic-linking.md)

当前 x86_64 移植使用 OpenJDK 17 的 BSD 平台层和 Plant 原生运行库，提供经过
验证的 Zero 解释器镜像以及带 C1/C2 的 Server VM 镜像；不能用宿主 Linux JDK
的本地库替换。源码、配置目录和 JDK 镜像位于 `apps/out/`，长期维护的补丁位于
`apps/openjdk/patches/`。

## 路径约定

模块化 JDK 的 JVM 位于 `<java_home>/lib/<variant>/libjvm.so`，不包含旧版
`lib/<arch>` 层。BSD 平台层从实际加载的 JVM 路径计算 `java.home` 和
`sun.boot.library.path`，不能额外剥离一级目录，否则无法找到 `lib/modules`。

Plant 使用 `/` 分隔目录、`;` 分隔 Java 路径列表，保留 `C:/...` 等路径中的
盘符。HotSpot、启动器 classpath 通配符展开和 Java 的 `path.separator`
属性必须一致；默认 native library 搜索列表也使用相同分隔符。例如：

```text
C:/java/bin/java -cp "C:/classes;R:/classes" Main
```

`java.io.File` 在这一平台识别盘符为绝对路径，并将文件 URL 中的 `/C:/...`
还原为 VFS 使用的 `C:/...`；类加载器依赖这一转换定位 classpath 中的文件。
启动器通过原生 `getexecname()` 取得加载器记录的可执行文件路径，供 `javac`
等工具确定 `application.home`，不能将带盘符的绝对路径拼接到当前目录。
NIO 使用统一的根路径长度处理盘符、文件名、父目录、拼接、规范化与相对化；
URI 转换沿用同平台的 `java.io.File` 规则，带空格和非 ASCII 字符的路径也须
保持文件身份。跨盘符相对化应拒绝，不能把盘符当作普通目录名。

## 更新已有移植构建

Zero 的代码缓存保存解释器元数据，应以 RW 映射；只有生成机器码的 JVM 才
需要可执行代码缓存。不能为运行 Zero 放宽 Plant 的 W^X 约束。
BSD Zero 使用编译器的 `__builtin_frame_address(0)` 获取当前栈帧，不能返回
局部变量地址；后者可被编译器优化为 NULL，触发错误的栈溢出检查。

以下命令面向已经完成 Plant 配置及其他移植适配的 OpenJDK 17 源码树，不是
从上游源码开始的完整构建流程。新源码树先应用这些补丁，已应用的补丁
无需重复执行：

```sh
jdk_source=apps/out/sources/openjdk-17-17.0.19+10
jdk_build=apps/out/x86_64/openjdk/configure-probe-9
patch -d "$jdk_source" -p1 < apps/openjdk/patches/bsd-modular-java-home.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-path-separator.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/zero-code-cache.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/bsd-zero-stack-pointer.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-java-file-paths.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-x86-hotspot.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-launcher-execname.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-process-environment.patch
patch -d "$jdk_source" -p1 < apps/openjdk/patches/plant-nio-paths.patch
make -C "$jdk_build" hotspot-zero java.base-libs java.base-java-only JOBS=8
make -C "$jdk_build" java.base-jmod-only JOBS=8
make -C "$jdk_build" jdk-image-only JOBS=8
PLANT_OPENJDK_DIR="$jdk_build/images/jdk" \
  scripts/build-livecd.sh kernel/plant-os-x86_64.iso x86_64
```

打包会更新 ISO 与 `kernel/plant-os-x86_64-jdk.img`，启动时需要同时挂载两者。
JDK 盘不携带 `libp.so`、`libcpp.so`、`libm.so.6`、`libz.so.1`；它们统一从
启动盘的 `/lib` 加载，与 `ld.so` 一起更新。打包时会移除镜像中这些库的副本，
避免 RPATH 优先命中旧 ABI 或宿主库。旧布局的 JDK 盘需要重新打包。
验证通过临时 `kernel/res/init.mst` 执行 `C:/java/bin/java -version`、无参数
启动器和带 classpath 的 Java 程序；程序输出在图形控制台，串口中的 init
命令退出状态不能单独证明 JVM 初始化成功。测试后恢复 init 脚本与正常镜像。

`apps/openjdk/Startup.java` 是启动回归程序，用宿主 JDK 17 编译后放入测试盘的
`java/` 目录，以 `-cp "C:/missing;C:/java" Startup` 启动，应输出
`OPENJDK STARTUP PASS`，覆盖路径列表分割、带盘符目录中的类加载和系统属性。
该程序还核验模块文件存在、盘符根目录为绝对路径，以及文件 URI 往返转换。

## NIO 与动态加载回归

JVM 通过通用 `dlopen` 加载，general-dynamic TLS 支持启动后建立的模块。
`Nio.java` 先执行 Startup 的路径断言，再检查 Selector 超时、管道读就绪、
跨线程写入、显式 wakeup、EOF、TCP accept、非阻塞 connect 完成及 socket 数据。

```sh
python3 scripts/test-openjdk.py \
  --jdk apps/out/x86_64/openjdk/configure-probe-9/images/jdk \
  --javac apps/out/host/openjdk/jdk-17.0.2/bin/javac \
  --accel kvm --out /tmp/plant-openjdk-nio
```

`--javac` 指向宿主 JDK 17 或更新版本的编译器。脚本编译测试、构建原生应用和
内核，在输出目录创建独立 ISO/JDK 磁盘，并临时替换后恢复 `init.mst`。
脚本通过 Lua 的 `os.execute` 在同一进程中切换目录，检查绝对路径启动与
进入 `C:/java/bin` 后由 shell 执行 `java --version` 均成功。
`startup-times.txt` 记录一次开机中三次启动的秒数，依次为首次及两次重复启动。
计时使用 Plant 的毫秒 `clock()`，包含进程启动和等待完成的时间；对比时固定
QEMU 配置。文件读取和缓存策略见 [文件读取与缓存](storage-cache.md)。
`--memory` 设置客户机内存 MiB（默认 2048）；较小内存也可验证模块映射大于
缓存预算时的淘汰和重新读取。
默认回归还运行 `Hello.java` 的绝对及相对路径源码启动、客户机 `javac` 编译及
生成的 class，并检查各自写出的结果。`RuntimeChecks.java` 覆盖环境表访问、
Lambda/Stream、反射、重复异常、UTF-8 文件读写，以及 32 MiB 堆中的并发分配与
GC。所有命令必须正常退出，结果文件在启动前清除，最后验证 ACPI S5。
Plant 当前没有将内核配置及 `getenv` 的惰性缓存导出为 POSIX `environ`；
Java 环境枚举在该表为 NULL 时返回空映射，不能解引用空表。完整环境继承仍
不属于此移植的已支持范围。
测试成功须在磁盘写出 `OPENJDK NIO PASS` 且完成 ACPI S5；程序退出码或串口
命令状态不能替代测试断言。TCG 可选，但 Zero 解释器执行大型 JDK 的耗时显著增加。
测试日志与磁盘保留在输出目录；文件锁、完整 Java 网络库及 MC 本身不属于此测试。
QEMU 使用 `-no-shutdown`，宿主确认其进入 shutdown 状态后保存 `console.ppm`
并退出 QEMU；超时也尽可能保存控制台，以保留未写到串口的 JVM 初始化错误。
运行中每十秒更新一次控制台快照。磁盘产物较大，多轮测试可将 `--out` 指向
`apps/out/java-regressions/` 下的独立目录，避免占满宿主的 `/tmp` tmpfs。

## x86_64 Server VM 与 JIT 验收

`scripts/build-openjdk-jit.py` 使用已有的 Plant OpenJDK 17 配置、源码树及基础
JDK 镜像，构建 `compiler1 compiler2 serialgc` 及 `java.base` 本地库。这仍不是
从干净上游源码开始的完整移植流程。脚本应用或检查 `plant-x86-hotspot.patch`、
`plant-launcher-execname.patch`、`plant-process-environment.patch` 和
`plant-nio-paths.patch`，更新 `java.base` 模块及镜像，保留原配置，
将 Server VM 库输出到 `hotspot/variant-server/libjvm/`，生成独立的 JDK 镜像：

```sh
python3 scripts/build-openjdk-jit.py \
  --build apps/out/x86_64/openjdk/configure-probe-9
```

默认镜像位于该构建目录的 `images/jdk-jit/`，可用 `--out` 指定其他目录；
重复执行会更新输出镜像。不能指向基础 `images/jdk/` 或其父子目录。
Zero 和 Server 在上游默认都会把库命名为 `server/libjvm.so`，因此必须隔离
库输出目录，不能仅凭目录名判断 JVM 类型。

补丁接入 Plant 的 x86_64 `ucontext_t`、`pthread_getattr_np` /
`pthread_attr_getstack` 和字节交换操作，将 DSO 析构身份移到两个 VM 共用的
BSD 平台层，并将 Linux 的自动栈扩展限定为 Linux。生成代码通过已有
`Thread::current()` 调用路径取得线程，使用原生动态 TLS。

Server VM 的代码缓存遵循 Plant 的 W^X：提交页先以 RW 写入，代码内容按页
粒度发布为 RX；每个 CodeHeap 一次预留覆盖其完整容量的连续写别名地址，
扩容时通过 `vm_map_alias` 按相同偏移映射同一物理页，不能将各次提交映射到
互不连续的别名，否则跨提交边界复制机器码会破坏重定位。CodeBlob 的头部、元数据与代码
布局按页边界隔离，代码复制、relocation、内联缓存和解释器 codelet 初始化均
经过同一写路径，C1 延迟修补以及 GC 更新内嵌 oop 也必须使用写别名。
入口及调用点热修补沿用 x86 的原子写入、内存屏障和缓存行刷新，不能将
指令缓存刷新置空；启动期直接执行原生刷新序列，无需先生成刷新 stub。
C1、C2 和本地方法包装器的 verified entry 统一以五字节 `jmp rel32` 开始，
初始目标为紧随其后的方法体。入口失效时保留操作码与指令长度，只改变
跳转目标，仍以原有对齐的八字节原子写发布。不能把栈检查指令直接改成
跳转：TCG 并发解码可能混合旧操作码与新位移，即使写入本身原子且随后
刷新缓存，也可能执行到指令中间。入口槽复用 `fat_nop()`，保持原有
栈检查、异常位置与元数据偏移的生成流程，不引入全局停机或额外入口表。
带别名的页在同一地址空间切换 RW 时不被错误地 COW；fork 后各视图采用独立
COW 语义，后续 `mprotect` 不能绕过进程隔离。

HotSpot 的通用安全点屏障依赖 POSIX 信号量。连续 `sem_post` 后，所有持有许可
的等待线程都必须能继续；原生运行库在取得许可且仍有剩余许可时传递唤醒，
防止下一轮 GC 或 JVM 退出停在安全点。

JIT 验收入口分别要求 C1 或 C2 编译，并核对计算结果和实际 `nmethod` 记录：

```sh
python3 scripts/test-openjdk.py \
  --jdk apps/out/x86_64/openjdk/configure-probe-9/images/jdk-jit \
  --javac apps/out/host/openjdk/jdk-17.0.2/bin/javac \
  --jit c1 --out /tmp/plant-openjdk-c1
# 将 --jit 改为 c2，输出目录改为 /tmp/plant-openjdk-c2，单独验收 C2。
```

`Jit.java` 对正负输入验证循环运算的闭式结果。宿主要求磁盘上的工作负载结果
通过，且 `LogCompilation` 中存在对应编译器生成的 `Jit.kernel` nmethod；
仅输出 VM 名称或解释执行得到正确结果都不能通过 JIT 验收。源码启动和综合
运行库测试始终使用默认分层编译；`--jit` 只限制最后的专用编译器验收程序。

Server VM 回归使用四个客户机 CPU，TCG 默认 `thread=multi`；
`--tcg-thread single` 可用于对照。`--repeat N` 在同一次启动中重复源码启动、
客户机 javac/class、运行库、NIO 和指定 JIT 工作负载，并逐项核对成功次数，
避免某轮成功掩盖另一轮失败；结果文件与编译日志保留最后一轮。
带 `--jit` 时，源码启动还记录 `source-jit.xml`，必须同时存在编译方法与
`make_not_entrant`，确认真正执行过入口失效。仅通过专用 JIT 运算不够。
热修补改动应分别用 `--accel tcg --repeat 4 --jit c1` / `--jit c2`
做重复回归，再检查 KVM；保留默认分层编译、线程与 GC。
