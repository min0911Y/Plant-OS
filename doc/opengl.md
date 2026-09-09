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
  → 等待绘制完成 → 按 window_get_buffer 的实际 pitch 复制到当前绘图平面
  → window_present_frame 同步交接 GUI 平面 → 交换 Mesa front/back 缓冲
```

EGL 每帧重新取得当前 GUI 绘图平面。`window_present_frame` 直接将该平面作为
已提交画面，只保留客户区外的窗口装饰，不再复制完整客户区到第二份 GUI 图像。
应答交还上一块已提交平面，旧绘图指针立即失效；EGL 下一帧完整覆盖客户区。
GUI 和 Mesa 各自的 front/back 是两个独立生命周期，不交换 Mesa 资源的所有权。
缓冲协议、基准及逐像素验证见 [GUI 性能](gui-performance.md#整帧提交与所有权)。

颜色为 BGRA8 UNORM，配置可选无深度或 D24S8；窗口尺寸固定。
上下文、绘图缓冲与 display 分别保留引用。销毁当前对象或终止 display
后，绑定仍保留到线程解绑；最后一个引用释放后才回收 screen、winsys 和
配置。创建或绑定失败回滚已构造资源，不能发布部分完成的 surface。

当前提供 EGL 1.4 所需的基础路径及实际实现的扩展。未启用 OpenGL ES、
GLX/X11、硬件驱动、MSAA 窗口、sRGB 窗口、EGLImage 或 EGL sync 扩展。
没有显示垂直同步能力，SDL 只接受 swap interval 0。Mesa 的渲染 worker
策略与 lavapipe 共用，默认按在线 CPU 数并行，`LP_NUM_THREADS` 可显式配置；构建并行度仍为
`MESA_JOBS`，两者无关。未执行完整 OpenGL/EGL CTS，不宣称一致性认证。

## glxgears 与回归

`glxgears.bin` 保留 Brian Paul 经典程序的齿轮几何、固定功能光照、显示列表
和三齿轮变换；仅将 X11/GLX 窗口层替换为 SDL3/EGL。来源、版本、校验和
及许可见 [UPSTREAM.md](../apps/glxgears/UPSTREAM.md)。

先运行 `gui.bin`，再从它的终端运行 `glxgears.bin`，显示红、绿、蓝三个
旋转齿轮，并每五秒报告帧率。方向键改变视角，空格暂停，Escape 关闭。
帧率用于观察软件渲染表现，不能作为通用图形性能基准。

### 性能对照

`glxgears.bin --benchmark` 自动启动 GUI，固定 300×300 客户区和旋转序列，
预热 180 帧后记录三轮、每轮 3600 帧。`GLXGEARS BENCH` 分别报告总时间、
绘制完成时间和同步呈现时间。基准用 `glFinish` 分隔绘制与呈现；总时间还包含
事件轮询和计时成本，不能把它与 WSL 上普通 glxgears 的 FPS 直接比较。
测试模式仍单独负责 GL 读回、实际窗口像素和键盘事件验证。

```sh
python3 scripts/test-x86_64.py --gears-bench --accel kvm --cpu host \
  --cpus 4 --memory 3072 --out /tmp/gears-before
python3 scripts/test-x86_64.py --cube --cube-workers 0 --accel kvm --cpu host \
  --cpus 4 --memory 3072 --out /tmp/cube-before
```

修改后使用相同参数和不同输出目录重跑；总 FPS 按累计帧数除以累计时间计算，
保留各轮结果以观察宿主波动。`--cube-workers 4` 另测显式并行光栅；其收益不能
由计算线程池的吞吐推断。立方体结果用 `scripts/compare-cube.py` 比较，并要求
两个固定角度的像素逐字节一致。

EGL、Vulkan WSI 和 GUI 的图像搬运共用 `libp` 的 `memcpy`。x86_64 使用 SSE2
非对齐向量复制，i386 使用整数复制；两者均只访问请求范围内的完整块和尾部字节，
不要求源、目标具有相同对齐。`libctest.bin` 覆盖对齐组合、块边界、目标哨兵及
紧邻未映射页的读写。这里不引入 AVX，也不改变 GUI 同步提交和缓冲生命周期。

2026-09-09 在 WSL2/KVM、Intel Core Ultra 7 270K Plus、QEMU 10.2.1、BIOS、
`-cpu host -smp 4 -m 3072 -display none` 下比较上述复制实现。内核固定为同一
工作区版本，包含当时已有的调度器修改；基线与优化版使用相同的基准程序。
glxgears 和串行立方体各做两次冷启动、累计六轮；四 worker 各做一次、三轮。
原始各轮采样和配置见 [JSON](benchmarks/graphics-copy.json)。

| 场景 | 修改前总 FPS | 修改后总 FPS | 变化 |
| --- | ---: | ---: | ---: |
| glxgears，当时默认串行光栅 | 1199.812 | 1256.492 | +4.72% |
| vkcube，worker=0 | 720.199 | 771.999 | +7.19% |
| vkcube，worker=4 | 631.321 | 642.449 | +1.76% |

glxgears 的同步呈现从 173.406 降至 154.716 µs/帧（−10.78%）；两种立方体配置的
固定角度像素均与基线逐字节一致。宿主波动影响总 FPS，尤其四 worker 的小幅变化
不能说明并行光栅有稳定加速。优化后四 worker 仍明显慢于串行，复制优化没有解决
光栅线程同步成本，也没有消除与 WSL 原生图形后端、SIMD 能力的差异。
该版本通过 KVM/BIOS 的 x86_64 `--opengl` 和两种架构的 `--threads`，包括上述
复制边界测试、线程/TLS、C/C++、stdio、浮点环境及动态链接/VM；测试后恢复正常镜像。

光栅同步优化及其新对照见 [场景同步](lavapipe.md#光栅-worker-的场景同步)。
`glxgears.bin --workers N` 在 EGL 初始化前设置 `LP_NUM_THREADS`，同时适用于普通、
测试和基准模式；宿主入口用 `--gears-workers N`，可搭配 `--opengl` 或 `--gears-bench`。

后续 GUI 平面交换消除了 GUI 内部完整客户区的复制：同一宿主上，两次冷启动
汇总的 glxgears 从 2941.706 到 3113.520 FPS（+5.84%），呈现从 96.056 到
82.778 µs/帧（−13.82%）。该组基线已经包含下节的场景同步和快时钟优化；它是
独立采样，不能与上表直接拼接计算收益。接口与完整数据见
[整帧提交与所有权](gui-performance.md#整帧提交与所有权)。

### 场景同步与全局时钟对照

同一宿主和上述 KVM 配置下，计时探针测得旧 `monotonic_ns()` 约 27734 ns/次，
取 TID 的 syscall 约 87 ns/次。旧全局时钟每次读取三次虚拟 HPET 寄存器；调度器
虽然已有 CPU 局部 pvclock，应用、SDL 和 Mesa 的公共计时路径仍承担设备模拟成本。
全局时钟启用经过跨核保证校验的 pvclock 后约为 108 ns/次。架构、原点校准和
回退约束见 [多架构说明](multiarch.md)。

下表按累计帧数除以累计时间计算。基线已经包含前面的 SSE2 复制优化；场景同步
中间态仍使用旧 HPET 全局时钟，最终默认策略按在线 CPU 数创建光栅和 compute worker。
未改变齿轮、光照、显示列表、300×300 客户区、旋转序列和同步窗口提交。
各轮原始数据、运行参数和立方体像素校验见 [JSON](benchmarks/graphics-sync-clock.json)。

| 场景与阶段 | 总 FPS | 绘制 µs/帧 |
| --- | ---: | ---: |
| glxgears，基线串行 | 1169.017 | 575.780 |
| glxgears，基线四 worker | 1018.028 | 728.870 |
| glxgears，仅场景同步优化，四 worker | 1255.718 | 489.759 |
| glxgears，再启用快时钟，显式串行 | 2344.188 | 309.237 |
| glxgears，再启用快时钟，显式四 worker | 3085.655 | 228.793 |
| **glxgears，最终默认，两次冷启动** | **2927.115** | **242.670** |
| vkcube，基线四 worker | 676.417 | 853.255 |
| vkcube，仅场景同步优化，四 worker | 954.169 | 516.597 |
| vkcube，再启用快时钟，四 worker | 1434.712 | 402.220 |

最终默认的六轮均为 2882.938–2955.588 FPS，超过 1500 FPS。显式四 worker 与默认
采用相同线程策略，不同冷启动间仍有宿主波动，不能把两次采样的差异当成策略收益。
诊断计时插桩已经移除，性能表使用正常基准代码。

验证包括 KVM 的 BIOS/UEFI OpenGL 像素与输入、单 vCPU 四 worker 的并发场景、
Vulkan 与 SDL 默认后端，以及两种架构的线程、运行库和时钟测试。时间速率与独立的
中断毫秒时钟核对；禁用 `kvmclock-stable-bit` 验证 HPET 回退，x86_64 另临时注入
运行中的 pvclock 读取失败，确认不倒退、保持前进且所有回归通过。故障插桩、启动
配置和正常镜像均已恢复；未把这项测试当成真实宿主休眠或在线迁移验证。

### 功能回归

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
- 四上下文并发 flush 场景、逐像素验证顺序与复用，以及带未等待提交的退出。
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
