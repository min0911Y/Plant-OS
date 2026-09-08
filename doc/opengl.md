# 原生 OpenGL 与 llvmpipe

x86_64 在现有 Mesa/LLVM 移植上提供 llvmpipe OpenGL、原生 EGL 和 SDL3
`SDL_GL_*` 窗口接口。i386 不构建这些库和 `glxgears.bin`。

## 构建与接口

版本、源码校验和及 LLVM/JIT 约束沿用 [lavapipe](lavapipe.md)。OpenGL
前端另需宿主 `bison`、`flex` 和 `m4`，用于 GLSL 解析器生成。
`scripts/build-mesa.py` 在同一 Meson 构建中生成三种库：

| 库 | 职责 |
| --- | --- |
| `liblvp.so` | lavapipe Vulkan ICD，保持既有窗口路径 |
| `libEGL.so` | Mesa EGL 核心、原生平台驱动、OpenGL state tracker、llvmpipe 与 LLVM |
| `libGL.so` | 上游 GL 函数入口，使用 `libEGL.so` 中唯一的 GL dispatch TLS |

EGL 平台代码在 `apps/mesa/mesa/egl_plantos.c`，上游接入补丁在
`apps/mesa/patches/mesa/native-egl.patch`。公开的 GL/EGL/KHR 头文件由
固定版本源码发布到 `apps/out/x86_64/mesa/include/`，不复制宿主头文件。
应用和库随统一构建图进入 LiveCD。

运行库提供标准 `strings.h`、可重入 `strtok_r` 和供 GL 查询使用的
`lround`/`llround` 浮点转整数函数。整数舍入独立于当前舍入模式，
不新增 `FE_INEXACT`，越界或 NaN 设置 `FE_INVALID`；`intmax_t` 与
`int64_t` 及对应格式宏保持一致。

```sh
make -C apps ARCH=x86_64 llvmpipe
make -C apps/glxgears ARCH=x86_64
make -C kernel ARCH=x86_64 livecd
```

应用可以直接链接 `libGL.so`/`libEGL.so`，或通过 SDL3 创建 OpenGL 窗口并用
`SDL_GL_GetProcAddress` 获取函数。SDL 复用上游 EGL 上下文实现，EGL 入口由
ELF 解释器经 `DT_NEEDED` 装载。没有第二套运行时库加载器。

## 窗口与资源

默认 `eglGetDisplay(EGL_DEFAULT_DISPLAY)` 选择原生平台；
`EGL_PLATFORM_SURFACELESS_MESA` 支持独立离屏显示。
上下文的版本、profile、属性和绑定检查由 Mesa EGL 与 OpenGL 核心处理。
支持兼容及 core profile、pbuffer、无 surface 的上下文和上下文共享。

`apps/mesa/include/plant_egl.h` 定义原生窗口描述符，包含 GUI 的不透明窗口
句柄和客户区矩形。创建 window surface 时以它作为 `EGLNativeWindowType`。
描述符须活到 surface 销毁；GUI 调用仍须在窗口所属线程进行。EGL 上下文
可在线程间解绑后迁移，离屏上下文可独立在线程运行。

```text
OpenGL / GLSL
  → Mesa OpenGL state tracker → llvmpipe → LLVM JIT → CPU 图像
  → 等待绘制完成 → 按 window_get_buffer 的实际 pitch 复制客户区
  → window_present 同步应答 → 交换 front/back 缓冲
```

颜色为 BGRA8 UNORM，配置可选无深度或 D24S8；窗口尺寸固定。
上下文、绘图缓冲与 display 分别保留引用。销毁当前对象或终止 display
后，绑定仍保留到线程解绑；最后一个引用释放后才回收 screen、winsys 和
配置。创建或绑定失败回滚已构造资源，不能发布部分完成的 surface。

当前提供 EGL 1.4 所需的基础路径及实际实现的扩展。未启用 OpenGL ES、
GLX/X11、硬件驱动、MSAA 窗口、sRGB 窗口、EGLImage 或 EGL sync 扩展。
没有显示垂直同步能力，SDL 只接受 swap interval 0。Mesa 的渲染 worker
策略与 lavapipe 共用，`LP_NUM_THREADS` 可显式配置；构建并行度仍为
`MESA_JOBS`，两者无关。未执行完整 OpenGL/EGL CTS，不宣称一致性认证。

## glxgears 与回归

`glxgears.bin` 保留 Brian Paul 经典程序的齿轮几何、固定功能光照、显示列表
和三齿轮变换；仅将 X11/GLX 窗口层替换为 SDL3/EGL。来源、版本、校验和
及许可见 [UPSTREAM.md](../apps/glxgears/UPSTREAM.md)。

先运行 `gui.bin`，再从它的终端运行 `glxgears.bin`，显示红、绿、蓝三个
旋转齿轮，并每五秒报告帧率。方向键改变视角，空格暂停，Escape 关闭。
帧率用于观察软件渲染表现，不能作为通用图形性能基准。

```sh
python3 scripts/test-x86_64.py --opengl --firmware bios --memory 3072 --timeout 600
python3 scripts/test-x86_64.py --opengl --firmware uefi --memory 3072 --timeout 600
```

`--opengl` 经临时 `init.mst` 执行 `libctest.bin` 和 `glxgears.bin --test`，
测试结束恢复正常镜像：

- EGL pbuffer 清屏与读回、GLSL 编译链接及实际着色结果。
- 运行库的整数格式、可重入分词、舍入模式、边界和异常标志。
- 拒绝不支持的版本、兼容与 core 上下文切换、无 surface 绑定。
- pthread 中共享纹理、GL dispatch TLS 隔离及拒绝同时绑定同一上下文。
- 当前 context/surface 的延迟销毁、terminate/reinitialize 和最终回收。
- SDL 显式、重复加载与卸载；断言 `GL_RENDERER` 为 llvmpipe；实际窗口的两帧 RGB 校验值匹配 GL
  读回，三种齿轮颜色与黑色背景存在，方向键改变画面，Escape 正常退出。

输出目录保留串口日志、两张 PPM 和客户区 RGB 数据。键盘事件仅用于
程序交互，不用于输入系统命令。冷启动测试会自动启动 GUI，等待并关闭
它的默认终端，避免遮挡测试窗口。Vulkan 共用部分用 `--lavapipe` 回归。

已在 QEMU TCG、`-cpu max -smp 4` 下通过 x86_64 BIOS/UEFI 的 `--opengl`
和 BIOS 的 `--lavapipe`（3072 MiB），以及 i386 `--threads`（512 MiB）。
当前 GL 字符串报告 `llvmpipe (LLVM 21.1.8, 128 bits)` 与 OpenGL 4.6；
验证范围为上述路径。两种固件下齿轮客户区的 FNV-1a 校验值均为
`7fe5054d`、`e33873d8`，实际窗口与 GL 读回一致。
