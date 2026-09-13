# OpenJDK

[开发指南](development.md) · [动态链接](dynamic-linking.md)

当前 x86_64 移植使用 OpenJDK 17 的 BSD 平台层和 Zero 解释器，依赖 Plant
原生运行库；不能用宿主 Linux JDK 的本地库替换。源码、配置目录和 JDK 镜像
位于 `apps/out/`，长期维护的补丁位于 `apps/openjdk/patches/`。

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
测试成功须在磁盘写出 `OPENJDK NIO PASS` 且完成 ACPI S5；程序退出码或串口
命令状态不能替代测试断言。TCG 可选，但 Zero 解释器执行大型 JDK 的耗时显著增加。
测试日志与磁盘保留在输出目录；文件锁、完整 Java 网络库及 MC 本身不属于此测试。
