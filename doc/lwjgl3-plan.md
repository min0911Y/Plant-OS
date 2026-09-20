# LWJGL 3 原生移植执行计划

本文可直接交给下一个对话作为任务说明。工作目录：`/home/min0911/plos`。
这是待执行计划；本文列出的新增文件、命令与 PASS 标记均为拟议接口，不代表已经实现。

## 任务与完成边界

请在 Plant OS x86_64 上完成 LWJGL 3 原生移植，并提供可重复构建、打包和客户机自动回归。直接按计划推进实现，不要只返回另一份计划。先读取当前 `AGENTS.md`、`doc/development.md`，检查 git 状态，再精确定位涉及的模块。本文基线来自 2026-09-13 的代码和文档，执行时以实际代码为准。

使用现有原生 OpenJDK 17，选择经上游资料确认可运行于 Java 17、适合现有 GLFW 3.4 的稳定 LWJGL 3 发行版，再固定源码、生成器及依赖版本和 SHA256。版本选择只依据 LWJGL 自身的要求和现有平台能力，不受外部应用依赖约束。若上游资料无法获取，明确记录版本选择依据及未验证项。

本次必做模块为 core、GLFW、OpenGL、STB。“完整”指这些模块的原生构建、装载、调用、回调、资源生命周期、独立示例和自动回归形成完整交付，不代表上游所有绑定或底层图形功能全部可用。接口支持范围必须据实列出，不能将最小三角形演示当作全部完成。

最终必须交付 Java → LWJGL → 原生 GLFW/EGL/OpenGL → GUI 实际窗口的完整链路，并用独立 Java 程序验收。暂不涉及 Minecraft，也不下载、打包或启动它。OpenAL、jemalloc 独立绑定、tinyfd、Vulkan 等其他模块留待单独安排；core 使用上游支持的标准 allocator 路径。不要为本任务扩展音频设备、鼠标捕获、全屏、网络认证或硬件驱动。

## 已有基础与已知限制

| 能力 | 当前基线与注意事项 |
| --- | --- |
| JVM | 已有原生 OpenJDK 17 Zero 和带 C1/C2 的 Server VM；已有源码启动、javac、反射、异常、线程、GC、NIO 回归。主要使用 JIT 镜像，Zero 可作诊断对照。 |
| Java 路径 | `/` 分隔目录，`;` 分隔路径列表，识别 `C:/...`。不能照搬 Unix 用冒号拆路径的逻辑。完整环境继承尚未支持。 |
| 动态链接 | `apps/ldso/` 已支持运行时路径加载、局部/全局符号作用域、动态 TLS；`dlclose` 减少引用但不卸载映射。IFUNC、RELR、`LD_LIBRARY_PATH` 等仍有限制。 |
| JNI 现有验证 | `apps/dyntest/late.c` 中名为 `JNI_OnLoad` 的测试函数只是符号隔离用例，不具备真正 JNI 签名，不能作为 JVM 加载 JNI 成功的证据。 |
| 图形 | x86_64 已有 Mesa llvmpipe、原生 EGL、`libGL.so`、GLFW 3.4。图形库由 CPU 软件渲染，尚无硬件驱动。 |
| 窗口线程 | 原生窗口操作与 swap 必须在创建窗口的线程执行；共享 EGL 上下文可用于工作线程。GUI 经 owner/generation 校验。 |
| GLFW 加载 | `libglfw.so` 显式依赖原生 `libEGL.so`、`libGL.so`；现有 GLFW module hooks 只查询已加载库的作用域。LWJGL 自身仍需真正的模块加载路径。 |
| 输入和窗口 | 固定尺寸窗口；已有基本键鼠、滚轮和事件等待。捕获、隐藏光标、warp、raw motion、剪贴板、全屏、调整大小尚未支持。 |
| 文件锁 | `kernel/fs/vfs.c` 的 `F_GETLK/F_SETLK/F_SETLKW` 返回不支持。首期采用显式 native 目录，避免让自动提取机制引入文件锁前置依赖。 |

优先阅读相关完整代码块及以下专题，不要整库通读：

- `doc/architecture.md`、`doc/dynamic-linking.md`、`doc/concurrency.md`、`doc/signals.md`。
- `doc/openjdk.md`、`doc/glfw.md`、`doc/opengl.md`、`doc/lavapipe.md`。
- `doc/build.md`、`doc/testing.md`、`doc/subsystems.md`。
- `apps/build.mk`、`apps/native-apps.mk`、`apps/dynamic.mk`、`apps/glfw/build.mk`。
- `apps/glfw/src/plantos_*.c`、`apps/libp/dlfcn.c`、`apps/include/dlfcn.h`。
- `scripts/test-openjdk.py`、`scripts/test-x86_64.py`、`scripts/build-livecd.sh`。

## 设计约束

1. 原生库只使用 Plant ABI 与运行库。不能加载 Linux GLFW、Mesa、glibc 或宿主 JDK 本地库，也不能把 `os.name` 全局伪装成 Linux 来绕过适配。
2. 复用上游 Java API、生成绑定与原生公共实现。平台差异集中在对应平台层；避免手写第二套 LWJGL、JNI 调用系统、GL dispatch 或动态链接器。
3. 原生代码遵守 x86_64 PIE/PIC、SSE2、无 red zone、TLS 和 W^X 规则。汇编、调用桩和运行时生成的回调代码必须单独审计，不能认为编译选项自动约束它们。
4. 运行时生成代码按现有 VM 接口经历写入、发布和回收。需要同时修改与执行的代码必须使用已有合法别名机制，禁止请求 RWX 或放宽内核 W^X。
5. 动态库按具体句柄查符号，显式管理所有权、错误和依赖。不能统一使用 `dlopen(NULL)` 代替对任意新库的加载；仅对已有图形代码做实际必要的修复。
6. 先确定原生库是否采用上游预期的静态或动态依赖布局，避免重复装入 GLFW、STB 分配器、GL TLS 或 dyncall 状态。共享资源只由所属模块释放。
7. 接入现有构建图、缓存、配置指纹和依赖规则。不把宿主生成工具打进目标镜像，不引入第二套 SDK。目标 JNI 头文件必须匹配原生 JDK。
8. 遵循既有代码组织，不为平台差异建立过度抽象。不维护旧实现兼容层。每阶段回看当前 diff，整合或删除冗余补丁，保留用户已有更改。

## 阶段 0：冻结依赖和可执行基线

工作：

- 核对上游发行说明和构建要求，列出本次四个模块的 Java JAR、本地组件、classifier、版本、来源、许可证与校验和。
- 精确检查选定 LWJGL 的 `Platform`、`Library`、`SharedLibrary`、`MemoryUtil`、`MemoryStack`、`Callback`、`JNI` 及生成器入口。实际路径以该版本源码为准。
- 确认 core 使用的 dyncall/dyncallback、汇编、内存分配与共享库加载实现；核实是否存在额外的线程 TLS、可执行内存和缓存刷新要求。
- 核实原生 JDK 镜像、JNI 头文件及可用宿主 Java/生成器。已有 OpenJDK 构建过程依赖配置好的源码树；若环境缺失，定位并恢复真正必需的构建输入，不掩盖依赖。
- 先运行最小相关基线：目标 JDK 的启动/JNI 前置检查，以及当前 GLFW/OpenGL 回归。已有可信、同版本结果可复用；发现失败应先区分基线问题与新增问题。
- 输出模块矩阵：每个模块的构建方式、依赖、适配点、测试与完成标准。检查选定 LWJGL 绑定与现有 GLFW 3.4 的符号/行为交集，缺失能力明确列为不支持。

验收：版本单源固定，能解释每个所需模块如何落到 Plant 运行库，剩余未知项有具体探测步骤。

## 阶段 1：构建、平台识别和真正 JNI 加载

工作：

- 建立 `apps/lwjgl/` 的上游来源记录、许可证、补丁和构建入口；可参考 Mesa 的源码缓存与补丁模式，避免复制一套下载/构建框架。
- 需要重新生成绑定时固定生成器与工具链；若上游提供完整匹配的生成源码，可直接使用。Java JAR 与 native 必须来自同一锁定版本和补丁集合。
- 新增 Plant 平台识别和资源/库命名约定，保留 Java 17 可运行的字节码级别。检查平台 enum 的所有分支，避免只加枚举却遗漏 GL provider、库路径或本地分配器选择。
- 先支持显式 native 目录装载，并明确 `java.library.path`、LWJGL 自身 library path 配置和模块间依赖解析的关系。带盘符路径不得按冒号切开。
- 用一个小型真实 JNI fixture 验证 `System.load` / `System.loadLibrary`、正确签名的 `JNI_OnLoad`、返回值、字符串、数组、DirectByteBuffer 和异常传播。
- 验证两个具有 `JNI_OnLoad` 的库分别初始化、缺库/缺符号诊断、并发加载和重复加载行为；测试对象须通过 JVM 调用，不复用假的 JNI 函数冒充验收。

验收：客户机 Java 程序真实加载原生 JNI 库并验证结果；`readelf` 检查 ELF 架构、依赖、导出和重定位，没有意外的宿主库或不支持的元数据。

## 阶段 2：LWJGL core、调用与回调

工作：

- 构建并运行 LWJGL core，接入现有 `dlopen/dlsym/dlclose/dlerror` 和 Plant 内存接口。
- 覆盖 `MemoryUtil`、对齐分配/释放、realloc、直接缓冲、UTF-8 转换、`MemoryStack` 的嵌套与线程隔离。使用上游已有标准 allocator 路径并验证默认选择，不依赖环境变量偶然选中某分支；不伪造 jemalloc 接口。
- 验证 JNI 函数指针调用的整数、指针、浮点/双精度、返回值和高参数数量签名，关注栈对齐、寄存器保存与指针宽度。
- 移植所选版本实际使用的 dyncallback/closure。核验 SysV x86_64 与 Plant 的具体差异，尤其 red zone、TLS、可执行页、指令发布和释放时机。
- 使用上游线程附着机制实现 C → Java 回调；测试已有 Java 线程以及原生 pthread 触发回调的线程附着/分离、JNI 引用生命周期和异常处理。
- 测试 GC 期间保活、注销/释放回调后不再触发、并发注册与销毁。注销后不得再调用已释放地址，不能用触发未定义行为作为正常测试。

验收：core 内存、函数调用和回调均通过客户机断言，在多线程及 GC 负载下稳定；生成代码权限正确，无 RWX、use-after-free 或用关闭 GC/JIT 掩盖错误的做法。

此阶段是后续 GLFW 事件的依赖。回调未验证前，不继续堆叠图形模块来掩盖问题。

## 阶段 3：GLFW 与 OpenGL 端到端

工作：

- 将 LWJGL GLFW 绑定接到现有 `libglfw.so`；不要另编译一份上游 X11/Wayland/Null GLFW 作为默认后端。按锁定版本核实 JNI wrapper 的实际组成，不凭名称假定必须生成某个 `.so`。
- GL 函数通过当前 GLFW/EGL 路径取得；处理 LWJGL 的 GL library provider 和 capabilities，避免误走 GLX/X11。保持 Mesa 唯一 GL dispatch/TLS。
- Java 主线程创建和呈现窗口，通过 GLFW poll/wait 分派 Java 回调。现有 GLFW 通知线程继续只负责唤醒，不在那里执行窗口回调。
- Java 测试创建 OpenGL 3.3 core 上下文，编译 GLSL、上传 VBO/VAO 和纹理，绘制可确定的图案，经 `glReadPixels` 核验。
- 宿主截取客户区并比较真实像素；至少两个不同画面由实际输入触发，证明呈现和事件链路有效。
- 覆盖键盘按下/松开/字符、鼠标按钮/移动/滚轮、窗口关闭、错误回调、`glfwWaitEventsTimeout`、跨线程 `glfwPostEmptyEvent`。
- 覆盖隐藏共享窗口、工作线程共享纹理、上下文切换与 Java capabilities 的线程隔离；所有窗口 swap 保持在所属线程。
- 验证默认与显式库路径、非根 cwd、重复 init/terminate、失败初始化清理。不支持的窗口功能如实报错。

验收：通过真实 Java/LWJGL 绘制与输入回归，且原有 C GLFW/OpenGL 测试继续通过。

## 阶段 4：STB 与独立综合示例

工作：

- 构建所选发行版的 STB 绑定及本地组件，核对模块全部导出，不随意裁剪其可移植实现。列明实际包含的功能及未验证部分。
- 用可再分发的确定性 fixture 验证 image 解码、image_write 编码往返、truetype 字体度量/栅格和 vorbis 音频采样解码；音频测试只检查解码结果，不要求设备播放。
- 至少覆盖一条 Java IO callback 路径，验证分配失败、损坏输入、边界条件和资源释放；避免跨 allocator 释放数据。
- 提供独立 Java 综合示例：STB 解码纹理或栅格化字形，经 LWJGL 上传到 OpenGL，在 GLFW 窗口中绘制，并通过键鼠事件切换可核验的画面。
- 示例资源随测试发布，来源与许可证明确，不依赖外部应用、在线资源或私有文件。

验收：STB 数据断言通过，综合示例的客户区像素与输入响应可自动验证；四个必做模块能在同一 JVM 中共同工作并正确清理资源。

## 阶段 5：打包与自动回归

建议交付位置，最终按现有构建机制收敛：

- `apps/lwjgl/`：版本/校验清单、`UPSTREAM.md`、许可证、补丁、薄构建入口、Java 回归源码及必要 fixture。
- `scripts/build-lwjgl.py`：仅在复杂生成流程确有需要时新增，负责宿主生成与目标构建，产物进入 `apps/out/`。
- `scripts/test-lwjgl.py`：复用现有 QEMU/JDK 测试能力，避免复制完整启动、磁盘、QMP 和截图框架。
- `doc/lwjgl.md`：最终支持矩阵、构建/打包/运行命令、模块依赖、平台设计与验证边界。

打包要求：

- 接入现有原生构建图，为 x86_64 暴露 `lwjgl` 目标；i386 明确报不支持，其他 i386 应用仍可正常构建。
- Java JAR、测试资源、native payload 按清单进入附加磁盘；公共运行库仍唯一来自系统 `/lib`。不要把整个 JDK 或无关 native classifiers 放进 initramfs。
- 首期以显式 native 目录为标准部署方式。若实现 natives JAR 自动提取，必须验证临时目录、文件锁或替代互斥、并发提取、校验、权限与失败清理；没有实现则明确记录，不让默认加载悄悄依赖它。
- 测试输出目录可配置，保存模块日志、JVM fatal 日志、像素证据和磁盘结果。每次启动先清除旧结果，用运行标识与退出状态防止旧 PASS 被误认。
- 图形验收同时要求客户机断言、实际帧像素、真实事件和正常退出；串口出现一行 PASS 或一张截图不够。
- 仅通过临时 `kernel/res/init.mst` 执行客户机命令；QMP 只投递交互事件，禁止 monitor `sendkey` 输入命令。测试后恢复启动配置及正常镜像。

拟议回归接口如下；实现完成后以实际 `--help` 和文档为准：

```sh
make -C apps ARCH=x86_64 lwjgl
python3 scripts/test-lwjgl.py --jdk <原生-JDK-JIT-镜像> --firmware bios --accel kvm --out <输出目录>
python3 scripts/test-lwjgl.py --jdk <原生-JDK-JIT-镜像> --firmware uefi --accel kvm --out <另一输出目录>
python3 scripts/test-lwjgl.py --jdk <原生-JDK-JIT-镜像> --accel tcg --repeat 4 --out <TCG输出目录>
```

默认从 4 vCPU、4 GiB 客户机内存开始，允许配置堆、内存、超时和 worker 数；TCG 超时依据实际工作量设置，不降低断言要求。缺少 KVM 时使用 TCG，并如实说明无法执行的矩阵项。

最少分别报告 JNI、CORE、CALLBACK、GLFW、OPENGL、STB 和综合示例结果，不以单一总 PASS 隐藏失败模块。重复回归须检查每轮成功次数。

按实际修改范围运行已有测试：GLFW/OpenGL 改动验证原有对应回归；共享运行库或 ABI 改动覆盖两种架构相关 dynamic/threads/signals 路径；HotSpot 改动遵循 `doc/openjdk.md` 的 C1/C2 和 TCG 重复验收。不要无依据地反复跑全部测试。

## 阶段 6：独立部署复验与交付

模块回归通过后，从构建清单生成独立部署目录，使用本文的 Java 示例复验完整安装流程。

- 用已发布的 JAR、native 和资源清单组装 classpath 与启动命令，尊重 Plant 路径列表规则及现有 JVM 的 GC 支持。
- 使用固定窗口、swap interval 0 和小分辨率，记录实际加载的原生库、GL vendor/renderer/version、JVM 和 LWJGL 版本。
- 从非构建目录启动，验证部署不依赖源码缓存、宿主绝对路径、遗留产物或隐式环境配置。
- 复验多次启动、窗口关闭、初始化失败和进程正常退出。发现缺陷后定位真实调用链，修复并补充必要回归。
- 不通过关闭所有错误检查、吞异常或假造功能成功来推进；图形能力以实际查询和测试为准。

最终交付必须包含：

1. 固定版本的源码来源、许可证、补丁与可重建产物规则。
2. 原生 Java JAR/本地库的部署清单，及可直接运行的构建、打包、测试命令。
3. 真正 JNI、core/callback、GLFW/OpenGL、STB 和独立综合示例的实际回归结果。
4. C1/C2 默认运行、线程/GC、图形生命周期和原有受影响路径的验证证据。
5. 四个模块的支持矩阵与部署复验结果；明确区别已实现、已验证、未验证和暂不支持。
6. `doc/lwjgl.md` 及开发导航、子系统边界、测试入口的必要更新。
7. 干净的 `git diff --check`，无生成镜像、日志、第三方构建缓存和无关改动；不要自动提交。

执行中可以根据证据调整实现顺序，但必须保持上述完成边界、验收强度与真实状态说明。出现环境阻塞时给出具体错误、已尝试的恢复动作和所需输入，不以未经验证的推测宣称无法实现。
