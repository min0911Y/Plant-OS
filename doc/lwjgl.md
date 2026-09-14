# LWJGL 3 原生移植

[开发指南](development.md) · [验证与交付](testing.md) · [GLFW](glfw.md) · [OpenGL](opengl.md)

Plant OS x86_64 已接入 LWJGL 3.3.6 的 core、GLFW、OpenGL 和 STB Java 绑定及对应 JNI native 库。版本、上游来源、SHA-256、许可证和补丁见 [apps/lwjgl/UPSTREAM.md](../apps/lwjgl/UPSTREAM.md) 与 [sources.json](../apps/lwjgl/sources.json)。本移植不伪装 `os.name` 为 Linux，也不加载宿主 Linux 的库。

## 构建

先准备已经构建好的 Plant OpenJDK 17 x86_64 镜像。构建入口会使用目标 JNI 头文件、宿主 JDK 17 的 `javac/jar` 和现有 Plant Mesa/GLFW 库：

```sh
make -C apps ARCH=x86_64 lwjgl -j8
```

产物位于被 `.gitignore` 忽略的 `apps/out/x86_64/`：

- `lwjgl/jar/`：`lwjgl-3.3.6.jar`、`lwjgl-glfw-3.3.6.jar`、`lwjgl-opengl-3.3.6.jar`、`lwjgl-stb-3.3.6.jar` 及编译期的 `jspecify-1.0.0.jar`；
- `lwjgl/native/`：`liblwjgl.so`、`liblwjgl_opengl.so`、`liblwjgl_stb.so`；
- `lwjgl/classes/LwjglSmoke.class` 和 `lwjgl/run-lwjgl.lua`：窗口回归程序及可编辑的 Lua 启动入口；
- `lwjgl-launcher.bin`：回归用的 Plant 启动器，fork `gui.bin` 后用 Plant 的 `exec` 启动 Java。

要生成可直接运行的 x86_64 LiveCD，并自动重新打包
`kernel/plant-os-x86_64-jdk.img`，使用：

```sh
make -C kernel ARCH=x86_64 lwjgl-livecd -j8
```

该目标默认使用
`apps/out/x86_64/openjdk/configure-probe-9/images/jdk-jit`，也可通过
`PLANT_OPENJDK_DIR`、`PLANT_OPENJDK_DISK` 和 `PLANT_LWJGL_DIR` 覆盖。普通的
`livecd` 目标仍不会携带 JDK。

该目标只支持 x86_64；i386 显式报告没有 LWJGL backend。上游源码缓存和 libffi 缓存只用于宿主构建，不进入 initramfs。

## 部署与运行

首期部署采用显式 native 目录，不依赖 natives JAR 自动提取或文件锁：

```text
C:/java/lwjgl/classes/          应用 class
C:/java/lwjgl/jar/              四个 LWJGL JAR 和 jspecify JAR
C:/java/lwjgl/native/            三个 liblwjgl*.so
```

启动应用时同时设置 `org.lwjgl.librarypath` 和 `java.library.path` 指向该 native 目录；GLFW 和 OpenGL 使用系统唯一实现：

```text
-Dorg.lwjgl.librarypath=C:/java/lwjgl/native
-Djava.library.path=C:/java/lwjgl/native
-Dorg.lwjgl.glfw.libname=/lib/libglfw.so
-Dorg.lwjgl.opengl.libname=/lib/libGL.so
```

构建完成后，直接启动带窗口的 QEMU：

```sh
make -C kernel ARCH=x86_64 lwjgl-run
```

启动进入 `psh` 后，执行：

```text
lua.bin C:/java/lwjgl/run-lwjgl.lua
```

Lua 脚本通过 `os.execute` 调用完整的 Java 命令，窗口出现后按 `A` 触发键盘回调。
需要修改 JVM 参数、classpath 或测试类时，直接编辑
`apps/lwjgl/run-lwjgl.lua` 后重新运行 `lwjgl-livecd` 即可。

三套 LWJGL native 库只依赖系统 `/lib/libp.so`。`libglfw.so` 仍来自 `apps/glfw`，并依赖 `/lib/libEGL.so` 和 `/lib/libGL.so`；不会在 LWJGL 目录中重复打包 GLFW、Mesa 或宿主 JDK native 库。

LWJGL 的 `PLANTOS` 平台通过现有 ELF `dlopen/dlsym/dlclose` 装载库和目标 JDK JNI。libffi closure 用 `vm_map` 与 `vm_map_alias` 建立 RW 数据视图和 RX 代码视图，未申请 RWX。GLFW 的 Plant runtime hook 按 `/lib/libEGL.so`、`/lib/libGL.so` 重新打开图形模块并查询其符号作用域，保留 Mesa 唯一的 EGL/GL dispatch 和 TLS。

## 支持与验证矩阵

| 模块 | 已构建和接入 | 本次冒烟已验证 | 明确边界 |
| --- | --- | --- | --- |
| core | Java core、JNI、MemoryUtil、MemoryStack、libffi callback 路径 | 真实 JNI native 装载、`memAlloc/realloc/free`、数据保持、嵌套 `MemoryStack` | 未宣称上游所有 core 专项测试和所有 ABI 签名均已覆盖 |
| GLFW | Java GLFW 绑定、原生 `libglfw.so`、Plant GUI/EGL 后端 | 初始化、固定尺寸实际窗口、GLFW error/key callback、事件等待/唤醒、正常 terminate | 不支持调整大小、全屏、最大化、光标捕获/warp/raw motion、剪贴板、IME、GLFW Vulkan WSI；完整边界见 [GLFW](glfw.md) |
| OpenGL | OpenGL 绑定和 `liblwjgl_opengl.so`，经 GLFW 使用 Mesa llvmpipe | OpenGL 3.3 core context、GLSL shader、VBO/VAO、uniform、绘制、`glReadPixels`；QEMU 实际客户区红/绿帧 | 只验证软件渲染链路，不代表硬件驱动、GLX、OpenGL ES 或 CTS 一致性 |
| STB | STB image、image_write、truetype 绑定和 `liblwjgl_stb.so` | PNG callback 编码、内存解码、像素/尺寸断言、truetype 初始化、缩放和字形度量 | 本次没有有效 vorbis fixture；不宣称 STB 全部生成接口都已回归 |

生成绑定时，GLFW 的 `GLFWNativeEGL` 和 `GLFWVulkan` 被按当前 Plant 能力排除；IME/preedit 符号在现有 GLFW 3.4 中按 optional lookup 处理，但 Plant 当前没有 IME 实现。OpenAL、Vulkan、其他 LWJGL 模块、Minecraft 及 natives JAR 自动提取不在本次交付范围。

## 自动回归

`test-lwjgl.py` 会临时写入 `kernel/res/init.mst`，构建 x86_64 apps、内核和 LiveCD，把 JAR/native 放到附加 FAT JDK 盘，测试结束后恢复 init 文件。`--jdk` 必须指向只包含 JDK 的目录，例如 `images/jdk-jit`；不要把 `test-openjdk.py` 的输出目录直接当作 JDK 输入，否则旧 ISO/JDK 盘会被再次打包并耗尽空间。

```sh
jdk=apps/out/x86_64/openjdk/configure-probe-9/images/jdk-jit
python3 scripts/test-lwjgl.py --jdk "$jdk" --firmware bios --accel tcg --out /tmp/plant-lwjgl-bios
python3 scripts/test-lwjgl.py --jdk "$jdk" --firmware uefi --accel tcg --out /tmp/plant-lwjgl-uefi
```

脚本通过 QMP 只投递键盘事件，不注入客户机命令；它轮询真实截图，检测红色初帧后发送 A，检测绿色终帧后保存第二帧。Java 将 `JNI`、`CORE`、`CALLBACK`、`STB`、`GLFW`、`OPENGL` 阶段写入 `C:/java/lwjgl/lwjgl-result.txt`，正常异常路径同时写出 `lwjgl-failure.txt`；脚本在 FAT 盘取回这些文件并尝试取回 JVM fatal 日志，再检查 Java 命令状态、ACPI S5 和两帧像素不同。

2026-09-14 的 TCG 证据：BIOS 和 UEFI 均通过六阶段回归；两轮初帧分别检测到 2487 个红色像素，A 键后分别检测到 2487 个绿色像素，Java 命令正常退出并完成 S5。KVM 性能和完整 LWJGL 上游测试矩阵尚未执行。

相关 ELF 可用以下命令复核：

```sh
readelf -h apps/out/x86_64/lwjgl/native/liblwjgl.so
readelf -d apps/out/x86_64/lwjgl/native/liblwjgl.so
readelf -Ws apps/out/x86_64/lwjgl/native/liblwjgl.so | rg 'JNI_OnLoad|Java_org_lwjgl'
```
