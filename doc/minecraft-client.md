# Minecraft Java 1.20.1 客户端

[开发指南](development.md) · [LWJGL](lwjgl.md) · [OpenJDK](openjdk.md)

客户端在 x86_64 Plant OS 上使用原生 Java 17 Server VM、GLFW/EGL、Mesa
llvmpipe 和 OpenAL Soft。游戏 JAR 保持原样，不替换 Minecraft 类或删除签名。
当前目标是本地单人、窗口模式；不导入 ZIP 中的启动器账户、token、旧配置或存档。

## 构建和运行

输入为包含 `.minecraft/versions/1.20.1/`、`libraries/`、`assets/` 的 ZIP：

```sh
make -C kernel ARCH=x86_64 minecraft-client-image \
  MINECRAFT_CLIENT_ARCHIVE=/mnt/e/1.20.1/mc.zip -j4
make -C kernel ARCH=x86_64 minecraft-client-run
```

产物是 `kernel/plant-os-x86_64-minecraft-client.iso` 和同名 `.img`。
前者自动启动客户端；后者持久保存 JDK、资源、日志及世界。打包拒绝覆盖已有
磁盘。运行目标直接启动已有镜像，不重建或清除世界；重新打包使用新的
`MINECRAFT_CLIENT_ISO` / `MINECRAFT_CLIENT_DISK` 路径。

默认 KVM、四核、4 GiB RAM、3 GiB 磁盘；可通过 `MINECRAFT_ACCEL`、
`MINECRAFT_CPU`、`MINECRAFT_MEMORY`、`QEMU_CPUS` 和
`MINECRAFT_CLIENT_DISK_MIB` 调整。无 KVM 时可用 TCG，但不以 TCG 帧率评估性能。

只重新打包已构建的运行库时，可调用 `scripts/build-minecraft-client.py`，
传入 `--archive`、`--iso`、`--disk`；`--jdk`、`--lwjgl` 可覆盖依赖目录。
宿主编译器沿用 LWJGL 的 `JAVAC` 选择，必须能编译 Java 17。

## U 盘文件包

构建最新 x86_64 LiveCD、Server JDK 和 LWJGL 后，可以生成可搬移的目录：

```sh
python3 scripts/package-minecraft-usb.py --archive /mnt/e/1.20.1/mc.zip \
  --output /mnt/e/plmc
```

该目录包含 Java、游戏与资源、LWJGL、启动器、说明、校验清单及配套系统 ISO。
复制整个 `plmc` 文件夹到 FAT32 U 盘，进入配套的 x86_64 Plant OS 后，
用 `disks.bin` 查询挂载点。例如挂载在 E:/ 时，在默认终端执行
`lua.bin E:/plmc/run.lua`。启动脚本从自己的绝对路径定位目录，游戏参数相对于
工作目录解析，不绑定 C:。存档和日志写到 U 盘的 `plmc/java/mc/`。
目录打包同样拒绝覆盖已有输出，不携带测试存档或账户凭据。

配套 `system/plant-os-x86_64.iso` 提供系统 Mesa/GLFW/OpenAL/libp；文件包不能
在 i386 或缺少这些运行库的旧系统上运行。复制数据文件不等于制作启动 U 盘。
USB 控制器、分区和文件系统支持范围见 [USB](usb.md)。

## 部署与依赖

客户机目录是 `C:/java/mc/`，包括 `client.args`、`run-client.lua`、`client.jar`、
`libraries/`、`native/`、`assets/`、`options.txt` 和后续创建的 `saves/`。
命令使用 Java 参数文件，classpath 以 Plant 的分号分隔。

打包根据版本元数据核对游戏、依赖与资源的 SHA-1；只选取通用 Java 依赖，
不部署 Windows/Linux/macOS native 库。LWJGL core、GLFW、OpenGL、OpenAL
使用原生 3.3.6 绑定。**STB 使用客户端原有的 3.3.1 JAR 及配套原生库**，
其源码和编译补丁固定在 `apps/minecraft/sources.json` 与 `patches/stb/`。
3.3.6 的 STB resize API 已改变，不能用它直接替换这个游戏依赖。
应用私有 native 目录在共享 LWJGL 目录之前，classpath 不混入另一版 STB。

内存分配使用 LWJGL 标准 system allocator；不需要 jemalloc native。
OpenAL Soft 的原生构建见 [来源说明](../apps/openal/UPSTREAM.md)：包括真实
软件混音和 loopback，当前播放设备是 `No Output`，**没有扬声器声音**。
图形库按 SONAME 交给系统动态加载器查找，不依赖 cwd 的盘符。

`lwjgl-launcher.bin --directory <目录> <程序> ...` 先启动 GUI，再设置工作目录，
通过共享参数编码器保留参数边界。独立 Java 启动类仅将 stdout/stderr 写入
`client.log` 后调用原版 Main；它位于独立包中，避免与游戏签名包冲突。
游戏日志是 `logs/latest.log`，崩溃报告在 `crash-reports/`，VM fatal 日志为
`hs_err.log`。未改变游戏异常处理或认证逻辑。

## 支持边界与验证

默认 640×480、2 区块视距、5 区块模拟距离、30 FPS 上限，关闭垂直同步、
全屏和 raw motion。用户可以在游戏内调整。普通地形和默认 C1/C2 分层编译
用于验收；不使用平坦世界、解释执行或 Minecraft 字节码补丁替代它。

验收顺序为主菜单 → 创建普通世界 → 真实键鼠移动/跳跃/转向 → 保存退出 →
冷启动并重新进入同一存档。核对日志中的玩家加入、各维度保存、正常 Java
退出，以及实际游戏截图和位置变化。STB 配套检查包含进入世界后生成存档
缩略图，不允许忽略后台 `NoSuchMethodError`。

当前交付镜像已完成上述流程。原版游戏 JAR 的 SHA-1 为
`0c3ec587af28e5a785c0b4a7b8a30f9a8f78f838`；普通生存世界中移动、跳跃和
转向后，冷启动登录位置与保存位置一致。四核 KVM、4 GiB RAM 和默认画质下，
F3 截图显示约 25 FPS；这是瞬时读数，不是性能基准。

图形和输入仍有明确限制：没有 GPU 硬件加速、全屏、系统剪贴板、IME 或
raw mouse motion。旁白服务未移植，原版会自行降级；离线测试账户的认证与
Realms 报错不代表本地世界失败。tinyfd 的原生错误弹窗未移植，不宣称全部
可选 LWJGL 模块可用。此入口不是在线账户启动器，也未验证模组兼容性。

独立回归使用 `test-lwjgl.py` 的 OpenAL loopback PCM 混音及 Java/GLFW/GL/STB，
`test-x86_64.py --glfw` 的窗口图标、鼠标定位与真实相对输入，以及两种架构
的 `--mouse`，覆盖 GUI 装饰、像素、缩放、输入和新增数学函数。

当前 x86_64 BIOS/KVM 的 LWJGL、GLFW 回归，以及 i386 和 x86_64 的完整
`--mouse` 套件均通过，包括 GUI、libc 和网络回环检查。
