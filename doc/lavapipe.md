# 原生 lavapipe 渲染器

## 支持状态

x86_64 已提供原生 Mesa lavapipe Vulkan 软件渲染器，包含 LLVM MCJIT、
SPIR-V compute、光栅化、GUI swapchain 和 SDL3 Vulkan renderer。
`lvptest.bin` 在 BIOS 与 UEFI 下验证真实 compute 结果、三角形读回、
窗口像素和键盘事件。它使用 CPU，不依赖宿主 Linux 库或 GPU 驱动。
i386 共用线程、TLS、C/C++ 运行库，图形仍使用 SDL 软件后端。

正常 SDL renderer 应用通过 `SDL_CreateRenderer(window, NULL)` 选择后端，
x86_64 优先使用 `vulkan`；显式指定 `"vulkan"` 可以禁止回退。
`SDL_GetWindowSurface` 默认继续使用 GUI 的直接共享缓冲，避免把 CPU surface
再上传为纹理。`sdltest.bin` 显式验证软件 renderer，`lvptest.bin` 通过默认选择并
断言 renderer 名称来验证 x86_64 优先使用 Vulkan。

| 边界 | 当前实现 |
| --- | --- |
| Vulkan | 单一原生 lavapipe ICD，`liblvp.so`；所有 Vulkan 命令保持标准函数指针 ABI |
| 显示 | 原生 GUI surface、BGRA8 UNORM、FIFO 提交，固定窗口尺寸；独立支持 headless surface |
| 着色器 | Mesa NIR、llvmpipe、LLVM 21 MCJIT；生成代码使用受支持的 SIMD、无 red zone 和 W^X |
| 动态链接 | SDL 应用经 `DT_NEEDED` 引入 ICD，解释器负责完整装载；ICD 依赖 `libp.so` 和 `libcpp.so` |
| 并发 | 原生 pthread/futex/TLS；当前地址空间固定在一个 CPU，默认不另建 llvmpipe 光栅工作线程 |
| 范围外 | i386 Vulkan、硬件加速、通用 Khronos Vulkan Loader、运行时装载其他 ICD、磁盘 shader cache、Unix fd 导入导出 |

未执行完整 Vulkan CTS；这里的验证覆盖下述实际运行路径，不代表一致性认证。

## 构建

除仓库的 GCC/G++、binutils、NASM、mtools、QEMU、ISO 工具和 os-terminal
依赖外，还需要宿主 Clang、CMake、Ninja、Meson、`patch`、`glslangValidator`，
以及 Python `mako`、`yaml`、`packaging` 模块。LLVM TableGen 必须来自与固定
源码匹配的 LLVM 21.1.8。宿主工具运行在构建机，生成的库全部使用 Plant OS ABI。

Python 构建工具可以装在隔离目录并加入 PATH，例如：

```sh
python3 -m venv apps/out/host/venv
apps/out/host/venv/bin/pip install cmake ninja meson mako PyYAML packaging
export PATH="$PWD/apps/out/host/venv/bin:$PATH"

make -C apps ARCH=x86_64
make -C kernel ARCH=x86_64 livecd

# 局部构建
make -C apps ARCH=x86_64 lavapipe
make -C apps/llvmtest ARCH=x86_64
make -C apps/lvptest ARCH=x86_64
```

x86_64 默认构建包含渲染器。首次构建下载并编译 LLVM，后续复用缓存；
`scripts/build-mesa.py` 默认给 LLVM/Mesa 使用两个编译任务。
可通过 `MESA_JOBS=N` 调整依赖构建并行度。
所有归档、生成头文件、交叉配置和日志留在 `apps/out/`。
应用和 `lib/` 经统一构建图打包，不能复制宿主 `.so` 或另建应用清单。
大 initramfs 占用的 RAM 也进入页元数据覆盖范围，仍保持保留状态。

版本和校验值单源维护在 `apps/mesa/sources.json`：

| 源码 | 版本 | SHA256 |
| --- | --- | --- |
| LLVM project | 21.1.8 | `4633a23617fa31a3ea51242586ea7fb1da7140e426bd62fc164261fe036aa142` |
| Mesa | 26.2.2 | `eeb29ca7e56cfaa8e8a79538dcf834e3b18e501c31bef5145e959ea437cc4216` |

脚本校验下载内容，先在临时目录解包、应用补丁，再发布变更。
移植代码位于 `apps/mesa/`，上游修改维护为该目录下的补丁；不要直接编辑缓存源码。
CMake 使用原生 toolchain 构建 libc++/libc++abi 和 X86 LLVM。
Meson 使用独立 sysroot、目标头文件、原生库和 LLVM 元数据桥接，禁止从宿主
pkg-config 搜索目标依赖。能力探测必须实际链接，不能用静态 try-compile 或
丢弃探测函数的链接参数制造成功结果。

LLVM 组件闭包由 Ninja 构建图得到并合并为内部静态归档；ICD 隐藏这份归档的
符号，保留真正的 Vulkan 入口和原生 surface 入口。发布前检查所需导出。
应用链接使用原生库目录解析传递依赖。

## 原生窗口路径

```text
SDL3 Vulkan renderer / 原生 Vulkan 应用
  → liblvp.so：Vulkan → NIR → llvmpipe → LLVM MCJIT
  → CPU 图像
  → 按行复制到 GUI 绘图缓冲
  → window_present
  → GUI 已提交画面 → framebuffer
```

`apps/mesa/include/plant_vulkan.h` 声明原生窗口适配函数：

```c
VkResult plant_vulkan_create_surface(
    VkInstance instance, window_t window, const VkRect2D *client,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface);
```

instance 必须启用 `VK_KHR_surface`。`client` 是 GUI 窗口缓冲中的客户区，
SDL 平台层传入边框内的区域；Mesa 不硬编码窗口装饰尺寸。
该函数属于 Plant OS 平台接口，不占用或伪造 Khronos 扩展编号。
使用标准 `vkDestroySurfaceKHR` 销毁 surface，且必须先销毁 surface 再关闭窗口。
GUI 操作遵守窗口所属线程的 RPC 规则。

`window_get_buffer` 返回规范 ARGB8888 像素、尺寸和实际 pitch。原生 WSI 只公布
可直接呈现的 `VK_FORMAT_B8G8R8A8_UNORM`，不假定物理 framebuffer 的颜色位序。
通用 WSI 先等待软件渲染 fence，原生后端随后复制客户区，并等待
`window_present` 应答后才将图像归还可获取集合。窗口失效时返回 surface 错误。
图像获取使用单调条件变量处理等待、唤醒和超时；headless 后端使用同一运行库的
阻塞队列，耗尽时不自旋。构造失败销毁已经创建的图像、同步对象和 swapchain 状态。
替换 swapchain 时旧链先进入 out-of-date 状态并唤醒等待者，即使新链随后创建失败。

SDL 的 Vulkan loader 钩子返回已由 ELF 解释器链接的 ICD，不调用尚未支持的
`dlopen(path)`。SDL 原生 surface 钩子调用上述适配函数，其他 Vulkan 操作均走
标准 API。`SDL_Vulkan_LoadLibrary` 只接受默认驱动或 `liblvp.so` 路径。

## JIT 与运行库

- LLVM、Mesa 和 libc++ 都是原生用户态库，禁用异常和 RTTI。旧的手写 C++ ABI
  与 GNU C++ 头文件副本已移除，C++ 应用统一使用配套 libc++ 头文件。
- MCJIT 使用 `SectionMemoryManager`，先分配 RW，再将代码设为 RX、常量设为 R。
  不开放 W+X。使用大代码模型处理任意用户地址，优化后为所有生成函数设置
  `noredzone`。Mesa 的 CPUID/XCR0 检查控制 SIMD 特性，当前 XCR0=3，不使用 AVX。
- `libp` 提供原生 pthread、mutex/condvar/rwlock/barrier/once、TSS 和 C++ TLS 析构。
  ELF TLS variant II 的装载与重定位见 [动态链接](dynamic-linking.md)。errno、locale
  和线程数据隔离；新线程继承浮点环境，i386 使用 x87，x86_64 同步 x87/MXCSR。
- futex syscall `0x67` 以进程组和对齐的 32-bit 地址为键，使用绝对单调 deadline；
  `UINT64_MAX` 表示无限等待。谓词检查与等待发布原子进行，退出摘除 waiter。
  wake 不修改用户谓词，调用者需要 acquire/release 原子操作和重试循环。
- 原生线程 syscall `0x68` 管创建、TLS 指针、join/detach、退出与组退出。
  托管的栈和 TLS 映射由内核在线程退出时释放。`_Exit`/`abort` 终止进程组；
  普通用户异常仍由内核处理，不仿造 POSIX 同步异常恢复。
- stdio 使用一个不透明 FILE 实现，包含 VFS 流和内存输出流。共享流通过递归锁
  串行化，注册表按统一顺序加锁；fork 锁定流，子进程重建锁的线程归属。
  格式化按实际长度分配，部分写入保留剩余数据。
- 标量数学使用已有实现及受限引入的 musl 算法，包括软件 FMA 的单次舍入和
  浮点环境控制。来源和修改见 [musl 说明](../apps/third_party/musl/UPSTREAM.md)。
- `clock()` 保持毫秒 ABI，`time_t` 为有符号 64 位秒，标准 `clock_gettime` 和
  `nanosleep` 使用原生时钟。`setenv`/`unsetenv` 的覆盖值在进程内有效并由 fork
  复制，普通 exec 尚不继承这些覆盖值。随机设备采用非加密后备源，entropy 为 0。

原生 mmap 当前支持私有匿名 R/RW/RX，文件映射、共享 POSIX mmap、PROT_NONE
不在支持范围内。不可用的进程执行、Unix socket、匿名 fd、文件锁等宿主能力
在依赖中关闭或明确报错，不提供假成功的兼容层。

## 验证与运行

```sh
python3 scripts/test-x86_64.py --lavapipe --firmware bios --memory 3072 --timeout 600
python3 scripts/test-x86_64.py --lavapipe --firmware uefi --memory 3072 --timeout 600
python3 scripts/test-x86_64.py --threads --memory 1024
python3 scripts/test-x86_64.py --threads --arch i386 --memory 512
python3 scripts/test-x86_64.py --sdl --memory 1536
```

`--lavapipe` 包含：

1. `LLVMTEST PASS`：解析 IR、真实 MCJIT、外部 `sin` 符号、可写全局数据、
   fork 后的代码页写保护、并发引擎创建和销毁。
2. `LVPCOMPUTE PASS`：SPIR-V compute pipeline、storage buffer、descriptor、
   dispatch、内存屏障和 fence，逐项核对 64 个结果。
3. `LVPHEADLESS PASS`：获取全部图像、零超时 NOT_READY、另一线程的有限超时、
   呈现后重新获取同一图像，以及销毁。
4. `LVPTEST PASS`：链接 ICD 的显式路径与引用计数、SDL 默认选择 Vulkan renderer、
   RGB 三角形、多个 readback 像素、两次窗口呈现和真实 Space 事件。宿主同时检查
   两张 framebuffer 图像中的背景、顶点附近颜色及中心插值，不能以截图或清屏单独
   替代着色器验证。

`--threads` 覆盖 TLS、同步、线程资源释放、C++ futures/streams、分配、文件身份、
目录生命周期、内存流并发、FMA、舍入模式与线程浮点环境。
测试仅临时修改启动脚本并在结束后恢复正常镜像，不注入 shell 命令。

在 GUI 终端运行 `lvptest.bin` 可查看三角形；Space 切换背景，Escape 或窗口
关闭按钮退出。无需先启动 GUI 时，该演示会启动单例 GUI 服务。

## 调查与上游

- [Mesa 官方源码](https://archive.mesa3d.org/mesa-26.2.2.tar.xz)：lavapipe、llvmpipe、Gallivm、WSI 和 Meson 构建图。
- [LLVM 21.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-21.1.8)：MCJIT、SectionMemoryManager、X86 后端和 libc++。
- [SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)：Vulkan renderer、随源码发布的 SPIR-V 和 Vulkan 头文件。
- [动态链接](dynamic-linking.md)、[多架构及显示](multiarch.md)。
