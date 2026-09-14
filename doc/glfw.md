# GLFW 原生移植

Plant OS x86_64 提供 GLFW 3.4 的 `libglfw.so` 和 `glfwtest.bin`。
上游版本、校验和及修改清单见 [UPSTREAM.md](../apps/glfw/UPSTREAM.md)。
本阶段建立原生 GLFW 窗口与 OpenGL 路径，并作为 [LWJGL 3](lwjgl.md) 的窗口后端；Minecraft 仍不在交付范围内。

## 构建与运行

```sh
make -C apps/glfw ARCH=x86_64
make -C apps/glfwtest ARCH=x86_64
make -C kernel ARCH=x86_64 livecd
```

先启动 `gui.bin`，再从终端运行 `glfwtest.bin`。程序显示红底蓝色矩形，
收到 A 的按下/松开及字符、鼠标移动、左键点击和向上滚轮后切换为绿底。
Escape 或窗口关闭按钮退出。自动回归使用 `--test`，冷启动时自行启动 GUI。

i386 不构建 GLFW，显式请求 `glfw` 或 `glfwtest` 会报不支持架构。
库和测试程序分别进入 `apps/out/x86_64/lib/` 与 `apps/out/x86_64/`；
测试应用通过 `application` 宏进入 `applications.list` 和 LiveCD。

应用包含 `apps/glfw/include/GLFW/glfw3.h` 并链接 `libglfw.so`；
若直接调用 GL 函数，还需对应架构 Mesa 头文件和 `libGL.so`。
保留原生应用的 freestanding、PIE、无 red zone、SSE2 和动态入口规则。

## 平台与上下文

默认自动选择 `GLFW_PLATFORM_PLANTOS`，也可通过 `glfwInitHint(GLFW_PLATFORM, ...)`
显式选择。该常量是本移植新增的平台标识。上游的显式 Null 平台保留，
不会被自动选择，也不代表真实显示设备。

- 复用 GLFW 上游通用核心、上下文校验、事件回调和 EGL 实现。
- native context 与显式 EGL context 均使用原生 Mesa EGL，可以共享对象。
- 支持 OpenGL compatibility/core 上下文和 `glfwGetProcAddress`。
- EGL 上下文可在不同线程间解绑、绑定；GUI 窗口操作和窗口 swap
  仍须在创建窗口的主线程完成。工作线程可使用共享上下文处理 GL 对象。
- 共享上下文、GL dispatch 和 TLS 遵循 [OpenGL](opengl.md) 中的生命周期。
- 使用 GUI 当前绘图平面，在 `window_present_frame` 同步应答后才复用。
- `glfwSwapInterval(0)` 可用；其他值报功能不可用，没有伪造垂直同步。

`libglfw.so` 的 `DT_NEEDED` 显式包含原生 `libp.so`、`libEGL.so` 和 `libGL.so`。
GLFW 的 Plant module hooks 按 `/lib/libEGL.so`、`/lib/libGL.so` 取得真实模块句柄
和符号作用域，入口来自现有 ELF 解释器的 `dlopen/dlsym`；不装载 Linux 库。
LWJGL 的 Java 平台适配、JNI native 库和联合回归见 [LWJGL 3](lwjgl.md)。

## 窗口和输入

支持多个固定大小的有装饰窗口、标题、位置、显隐、聚焦、关闭请求、客户区及
framebuffer 尺寸查询。隐藏窗口在创建时不显示、不获取焦点；可见但不聚焦的
窗口使用 `GLFW_FOCUSED = GLFW_FALSE`。创建后的 `GLFW_RESIZABLE` 属性为 false。

窗口尺寸受 GUI 的共享映射容量、坐标范围和装饰尺寸约束，创建失败返回错误。
位置和鼠标坐标在 GLFW 中以客户区为基准。监视器分辨率来自 framebuffer；
固件没有提供的物理尺寸和刷新率返回 0，内容缩放为 1。

GUI 公共接口的变化：

- `create_window(title, x, y, width, height, flags)` 显式接受创建标志，0 为
  可见且聚焦；支持 `GUI_CREATE_HIDDEN` 和 `GUI_CREATE_UNFOCUSED`。
- `window_control` 经 owner/TID/generation 校验执行移动、显示、隐藏或聚焦。
- `window_get_state` 读取带序列号的共享状态快照，查询不发送 RPC。
  GUI 在位置、焦点、可见性和鼠标位置变化时发布完整快照。
- `window_set_event_target` 允许将唤醒通知发给同一 task group 的受校验线程；
  `window_set_event_notifications` 使用当前 owner 线程作为目标。

GLFW 仅有一个专用通知线程；它接收 GUI 通知并更新原子谓词，主线程经私有
futex 和绝对单调 deadline 实现 `glfwWaitEvents` / `glfwWaitEventsTimeout`。
`glfwPostEmptyEvent` 可跨线程唤醒。通知线程不执行应用回调，不读取应用线程的
IPC 队列；终止时先关闭窗口，再退出并 join 通知线程。

键盘使用 GUI 统一逻辑扫描码，覆盖常用字母、数字、功能键、方向键、修饰键
和小键盘；按下、松开、重复及字符回调由 GLFW 核心分派。字符转换目前为
US 键盘布局，没有 IME。鼠标沿用 GUI 的位置、左/右键和垂直滚轮事件。
GUI 现有分离按下/松开队列及鼠标协议的能力边界仍然存在。

## 尚未支持

后端无法执行的操作会报告 GLFW 错误，不伪造平台状态；尺寸约束等 hints
仍遵循上游对固定大小窗口的忽略规则。当前缺少：

- 窗口调整大小、全屏、最大化、独立的最小化状态、无装饰或置顶窗口。
- 鼠标隐藏、相对捕获、raw motion、warp、自定义光标及系统剪贴板。
- 游戏手柄、拖放、IME、多显示器模式切换与硬件 gamma。
- OpenGL ES、OSMesa 和 GLFW 的 Vulkan WSI。

箭头光标可用。`glfwRawMouseMotionSupported()` 返回 false，
`GLFW_CURSOR_DISABLED` 请求失败后仍保持 `GLFW_CURSOR_NORMAL`。
GUI 标题栏的原有隐藏按钮表现为窗口隐藏，不宣称独立的最小化状态。

运行 Minecraft 所需的相对鼠标捕获、全屏和其他窗口能力不在本阶段交付中；LWJGL
Java/JNI 链路的实际支持范围见 [LWJGL 3](lwjgl.md)。
没有执行完整 GLFW 一致性测试，也未宣称所有 GLFW 平台功能可用。

## 回归

```sh
python3 scripts/test-x86_64.py --glfw --firmware bios --accel kvm --cpu host \
  --memory 3072 --timeout 600
python3 scripts/test-x86_64.py --glfw --firmware uefi --accel kvm --cpu host \
  --memory 3072 --timeout 600
```

无 KVM 时使用 `--accel tcg --cpu max`。

`glfwtest.bin --test` 检查：

- 平台和显示枚举、单调计时、超时等待、跨线程唤醒及应用 IPC 消息保留。
- 窗口与字符串、EGL 配置数组分配失败，连续上下文创建失败后的资源回收。
- OpenGL 3.3 core context、GL 入口查询、跨线程共享纹理及 context TLS 隔离。
- 隐藏共享窗口不抢焦点、窗口移动和显隐、拒绝不支持的捕获模式。
- 两帧 GL 读回校验和；宿主截取实际客户区，逐字节核对像素并检查颜色面积。
- QMP 投递真实 A 键、左键、移动、滚轮和关闭按钮，确认事件驱动画面变化。
- 关闭后的 GLFW 重复初始化与终止。

测试脚本只通过临时 `init.mst` 执行命令，完成后恢复启动脚本和正常镜像。
共享 GUI ABI 的回归还需覆盖两种架构的 SDL/GUI 路径。
