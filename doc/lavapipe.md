# 原生 lavapipe 渲染器

## 支持状态

x86_64 已提供原生 Mesa lavapipe Vulkan 软件渲染器，包含 LLVM MCJIT、
SPIR-V compute、光栅化、GUI swapchain 和 SDL3 Vulkan renderer。
`lvptest.bin` 在 BIOS 与 UEFI 下验证真实 compute 结果、三角形读回、
窗口像素和键盘事件。它使用 CPU，不依赖宿主 Linux 库或 GPU 驱动。
i386 共用线程、TLS、C/C++ 运行库，图形仍使用 SDL 软件后端。

x86_64 的 OpenGL/EGL 接入复用 llvmpipe、Gallivm 和 LLVM，窗口接口及
经典 glxgears 回归见 [OpenGL](opengl.md)。

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
| 并发 | 原生 pthread/futex/TLS；x86_64 线程可跨 CPU，光栅与 compute 默认按在线 CPU 数并行；`LP_NUM_THREADS` 共用同一配置 |
| 范围外 | i386 Vulkan、硬件加速、通用 Khronos Vulkan Loader、运行时装载其他 ICD、磁盘 shader cache、Unix fd 导入导出 |

未执行完整 Vulkan CTS；这里的验证覆盖下述实际运行路径，不代表一致性认证。

## 构建

除仓库的 GCC/G++、binutils、NASM、mtools、QEMU、ISO 工具和 os-terminal
依赖外，还需要宿主 Clang、CMake、Ninja、Meson、`patch`、`glslangValidator`、
`bison`、`flex`、`m4`，
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
可通过 `MESA_JOBS=N` 调整依赖构建并行度。运行时工作线程数由 `LP_NUM_THREADS` 控制，两者无关。
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
  唤醒仅在遇到匹配的有限 deadline 时读取一次时钟；无限等待和空队列不访问 HPET，
  到期等待者仍以超时返回，且不占用成功唤醒的数量。
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

## 旋转立方体与性能回归

`vkcube.bin` 直接使用 Vulkan 图形管线、SPIR-V 顶点/片元着色器、深度测试和
原生 swapchain 绘制旋转立方体，窗口标题显示 FPS，Escape 退出。
`--workers N` 在初始化 ICD 前设置 `LP_NUM_THREADS`，`0` 在提交线程内完成光栅化。
光栅与计算线程池默认按在线 CPU 数创建，单 CPU 默认在提交线程内执行；
`--workers 0` 显式选择串行，构建并行度不影响运行时线程数量。

```sh
make -C apps/vkcube ARCH=x86_64
python3 scripts/test-x86_64.py --cube --accel kvm --cpu host --cpus 4 \
  --memory 3072 --timeout 600 --out /tmp/cube-before
# 完成待比较的修改后，使用相同配置再次运行，输出到 /tmp/cube-after。
python3 scripts/test-x86_64.py --cube --accel kvm --cpu host --cpus 4 \
  --memory 3072 --timeout 600 --out /tmp/cube-after
python3 scripts/compare-cube.py /tmp/cube-before /tmp/cube-after
```

基准固定为 640×480，预热 60 帧，三轮各 3600 帧使用同一旋转序列。
计时包含图像获取、命令记录、提交、等待渲染完成和同步窗口呈现，另外记录命令
记录至渲染完成的耗时。初始化、JIT 预热、事件检查、日志和截图握手不纳入采样。
报告每轮实际帧数、总纳秒数、FPS、帧时间中位数与 P95；跨轮总 FPS 按总帧数除以
总时间计算。测试前后还各渲染一个固定角度，经 QMP 检查背景、三个可见面和键盘事件。

输出目录保留 `serial.log`、`configuration.json`、两张 PPM 截图及去掉窗口边框的
RGB 像素。比较工具要求 QEMU 配置、图像大小和采样帧数相同，像素逐字节相同，
且总 FPS 不下降超过 5%（可用 `--max-regression` 设置）；失败返回非零状态。
运行对照时使用同一宿主且避免其他 CPU 密集任务。TCG 可以验证功能，其帧率不能
与 KVM 或原生硬件混比。`--cube-workers 0` 可单独测量优化后内核上的串行对照。
原始帧率记录见 [CSV](benchmarks/lavapipe-cube.csv)。

2026-09-08 的对照使用上述命令：WSL2/KVM、Intel Core Ultra 7 270K Plus、
4 vCPU、3072 MiB、BIOS、QEMU 10.2.1。基线内核及运行库为 `69f3296`，加入同一
立方体基准后先完成测量，再修改线程和 TLB 实现。结果与配置另见
[JSON](benchmarks/lavapipe-cube.json)。

| 配置 | 光栅 / compute 工作线程 | 总 FPS | 相对基线 | 渲染阶段 ms/帧 |
| --- | --- | ---: | ---: | ---: |
| 修改前默认 | 0 / 0 | 799.770 | — | 0.574 |
| 修改后默认 | 0 / 4 | 820.513 | +2.59% | 0.594 |
| 修改后显式 `--workers 4` | 4 / 4 | 670.772 | −16.13% | 0.862 |

默认配置另一次冷启动测得 827.015 FPS（+3.41%），各次固定角度画面均与基线逐字节
一致。总 FPS 的小幅改善不能说明多核光栅更快：本例渲染阶段没有加速，强制四个光栅
工作线程仍有负收益，因此当时的默认策略保留串行光栅，同时开放真正的跨核计算与显式光栅
并行。其他场景需要独立测量，不能用这组帧率外推复杂着色器或真机的加速比。

本次回归覆盖 x86_64 BIOS/UEFI 的 `--lavapipe`、两架构的 `--threads` 与基础回归，
以及 x86_64 四种 PCID/INVPCID 组合配合跨核 VM 探针。跨核探针覆盖单页与 64 页同址
重映射、远端写保护、并发 VM 操作中的进程退出；运行库回归覆盖条件变量、barrier、
TLS、C/C++ 和浮点环境。未执行完整 Vulkan CTS 或真机性能测试。

### 光栅 worker 的场景同步

当前实现在 `apps/mesa/patches/mesa/native-raster-workers.patch`：场景准备完成后
发布共享代际号，一次 futex 广播唤醒 worker。每个 worker 每代只消费一次场景，
通过原子索引领取图块，空闲时阻塞。完成计数的最后一个到达者汇集所有 worker 的
写入，推进场景队列后只发布一次 fence；不再逐线程发送开始/完成信号，也不再使用
两轮全员 barrier。setup 不保存第二份线程数量，scene fence 的完成不依赖 worker 数。

队列发布与退出共用 rasterizer mutex，但可能等待队列空间的 enqueue 必须在该锁
外执行，让最后一个 worker 可以取走下一场景。销毁等待场景排空，发布退出代际，
join 全部 worker 后才释放缓存和同步对象；线程池构造失败同样完整唤醒、join 和回滚。
共享代际在上一场景所有参与者完成后才推进，不用自旋、轮询 sleep 或固定图块阈值
掩盖唤醒成本。全局时钟改用可校验的 KVM pvclock 后，小窗口的并行收益也得到确认，
已删除强制串行和独立 compute 配置补丁，恢复上游按在线 CPU 数设置两种线程池的策略。
`LP_NUM_THREADS=0` 仍可用于显式串行对照。

OpenGL 的 `--test` 额外使用四个独立上下文并发提交分条画面，连续 flush 场景，
每轮读回并验证全部像素，最后带未等待的提交退出。可通过下列命令选择光栅 worker，
并用单 vCPU 检查多个 worker 都能通过阻塞等待完成工作：

```sh
python3 scripts/test-x86_64.py --opengl --gears-workers 4 --accel kvm \
  --cpu host --cpus 1 --memory 3072
python3 scripts/test-x86_64.py --gears-bench --gears-workers 4 --accel kvm \
  --cpu host --cpus 4 --memory 3072
```

本轮 KVM 累计对照中，四 worker 立方体从 676.417 FPS 提高到 954.169 FPS；
再消除公共读钟的虚拟 HPET 开销后为 1434.712 FPS。最终默认 glxgears 两次冷启动
为 2927.115 FPS，各轮均超过 1500 FPS。完整分阶段数据和验证范围见
[OpenGL 性能对照](opengl.md#场景同步与全局时钟对照)。

## 多核 compute 吞吐基准

`lvptest.bin --benchmark [--workers N]` 使用 Vulkan compute 生成 TEA 计数器数据块：
16,777,216 个独立的 64-bit 块，每块执行标准 TEA 的 32 轮无符号整数运算，
固定零密钥、输入为 `(块索引, 0)`，每次 dispatch 输出 128 MiB。工作组大小为 512，
共 32,768 组，计算量均匀且各组之间无需通信。这是计算吞吐场景，单位为 ms/dispatch
和输出 MiB/s，不包含窗口显示。

基准与已有小型 compute 测试共用 Vulkan 资源及提交代码。每个进程预热三次，
随后采样五轮、每轮四次 dispatch；计时包含 fence 重置、队列提交及等待完成，
排除初始化、JIT 预热、缓冲区清零、日志及结果校验。每次计时后将全部输出与
CPU 参考实现逐字节比较，参考实现还检查零密钥/零输入的已知结果
`41ea3a0a 94baa940`。每次采样前清零输出，避免遗漏执行时误用上次结果。

```sh
python3 scripts/test-x86_64.py --compute-bench --accel kvm --cpu host --cpus 4 \
  --memory 3072 --timeout 600 --out /tmp/compute-a
python3 scripts/test-x86_64.py --compute-bench --accel kvm --cpu host --cpus 4 \
  --memory 3072 --timeout 600 --out /tmp/compute-b
python3 scripts/compare-compute.py /tmp/compute-a /tmp/compute-b \
  --min-speedup 2 --out /tmp/compute-results.json
```

每次启动依次运行 `0, 1, 2, 4, 4, 2, 1, 0` 个 worker，各模式使用同一个内核、
ICD、着色器和四 vCPU 配置；`0` 在提交线程中同步计算，`1` 使用一个计算 worker。
正反顺序均保留，避免只选最快结果。测试自动保存配置、串口日志和 `results.json`，
比较工具拒绝缺失采样、校验不完整及配置/工作量不同的结果。
`--min-speedup 2` 要求四 worker 相对两种串行模式都达到两倍吞吐，否则返回非零。
性能测试须串行运行，避免宿主同时执行编译或其他 CPU 密集任务。

2026-09-08 实测使用 Intel Core Ultra 7 270K Plus、WSL2/KVM、QEMU 10.2.1、
BIOS、4 vCPU、3072 MiB。两次冷启动合计每种模式 80 次计时 dispatch，按总工作量
除以总耗时汇总；全部 320 次输出通过完整校验。

| 计算 worker | 平均 ms/dispatch | 输出 MiB/s | 相对同步模式 | 相对单 worker |
| ---: | ---: | ---: | ---: | ---: |
| 0（同步） | 202.876 | 630.93 | 1.000× | 0.997× |
| 1 | 202.340 | 632.60 | 1.003× | 1.000× |
| 2 | 101.572 | 1260.19 | 1.997× | 1.992× |
| 4 | 74.091 | 1727.61 | **2.738×** | **2.731×** |

四 worker 的耗时降低 63.48%，两次冷启动单独统计分别为 2.906× 和 2.587×。
原始采样见 [CSV](benchmarks/lavapipe-compute.csv)，完整配置、二进制 SHA-256、
所有样本与汇总见 [JSON](benchmarks/lavapipe-compute.json)。构造场景时曾以
32 MiB、128 大小工作组试跑，四 worker 约两倍；最终采用上述更大任务，所有对照
模式统一使用同一工作量。四 worker 仍未达到理想四倍，具体调度和宿主开销需要
进一步采样才能归因。这组计算吞吐结果不能用于推断小立方体或窗口呈现的 FPS。

新增基准完成两次 BIOS 冷启动对照，并运行原有 `--lavapipe` 回归验证小型 compute、
headless swapchain、SDL 三角形像素与实际键盘事件。正式吞吐数据来自当前版本中
并行关闭/开启的比较，不是重跑旧版本内核；历史立方体修改前后的结果保留在上一节。

## 调查与上游

- [Mesa 官方源码](https://archive.mesa3d.org/mesa-26.2.2.tar.xz)：lavapipe、llvmpipe、Gallivm、WSI 和 Meson 构建图。
- [LLVM 21.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-21.1.8)：MCJIT、SectionMemoryManager、X86 后端和 libc++。
- [SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)：Vulkan renderer、随源码发布的 SPIR-V 和 Vulkan 头文件。
- [动态链接](dynamic-linking.md)、[多架构及显示](multiarch.md)。
