# Plant OS 的 i386 与 x86_64 构建

两个架构共用 `kernel/` 下的调度器、VFS、IPC/RPC、网络和平台服务，架构后端位于 `kernel/arch/x86/{i386,x86_64}`。没有第二套 `kernel64` 内核，也没有运行 i386 程序的长模式兼容层。

## 构建与启动

```sh
# 原有 i386 磁盘与 LiveCD
make -C apps ARCH=i386
make -C loader
make -C kernel ARCH=i386
make -C kernel ARCH=i386 livecd

# x86_64：不需要构建 DOSLDR 或预先准备 boot.img
make -C apps ARCH=x86_64
make -C kernel ARCH=x86_64 livecd
```

x86_64 使用 GCC/G++、binutils、NASM、mtools、xorriso 和固定版本的 Limine 12.6.1。生成的 `kernel/plant-os-x86_64.iso` 同时包含 BIOS 和 UEFI 启动项。内核、应用和库分别位于 `kernel/obj/x86_64/`、`apps/out/x86_64/`、`apps/libs/x86_64/`；切换架构不会覆盖另一架构的对象。

```sh
qemu-system-x86_64 -accel tcg -cpu max -smp 4 -m 1024 \
  -cdrom kernel/plant-os-x86_64.iso -boot d -serial stdio
```

UEFI 启动可使用 `/usr/share/OVMF/OVMF_CODE_4M.fd` 和一份可写的 `OVMF_VARS_4M.fd` 副本，分别作为只读/可写 pflash drive。ISO 使用 EFI 分区及 El Torito 元数据；不能通过键盘注入绕过 Limine 的卷识别错误。

## 启动、地址空间与 ABI

| 项目 | i386 | x86_64 |
| --- | --- | --- |
| 启动协议 | DOSLDR 或 Limine Multiboot2 | Limine native，base revision 3 |
| 固件 | BIOS | BIOS、UEFI |
| 内核链接地址 | `0x280000` | `0xffffffff80000000` |
| 用户入口地址 | `0x70000000` | `0x100000000` |
| 可执行格式 | ELF32 / EM_386 | ELF64 / EM_X86_64 |
| 用户调用入口 | `int 0x36` | `syscall`，普通返回 `sysretq` |
| 浮点运算 | x87 | SSE2，binary64 `long double` |
| 默认终端 | TextMode，可选 HighTextMode | Limine framebuffer + flanterm |

x86_64 使用四级页表和 Limine HHDM，不探测写物理内存。物理页元数据按内存图分配；用户页优先从高物理地址分配，低地址保留给有 DMA 地址限制的设备。用户页按需建立，并支持 COW、共享映射和 NX；只接受静态原生 ELF，检查段、入口和用户地址边界。

系统调用保留 Plant API 的语义编号，但使用完整的 64 位参数：`RAX` 为编号，参数为 `RDI, RSI, RDX, R10, R8, R9`。这不是 Linux ABI。入口通过 `swapgs` 取得每 CPU 内核栈，屏蔽 IF/TF/DF/NT/AC；NMI、双重故障和机器检查有独立 IST。用户中断系统调用门未开放，32 位程序会在 ELF 检查时被拒绝。

用户与内核都以 `-mno-red-zone -msse2 -mfpmath=sse -mlong-double-64` 构建。入口在执行 C 代码前保存 FXSAVE64 状态并装入内核 MXCSR，返回恢复用户状态；调度和 fork 保留全部 XMM 寄存器。FSGSBASE 和 OSXSAVE 未启用；用户 GS 固定为零，AVX 不属于当前 ABI。信号返回使用编号 `0x65`，检查用户 frame 后经 `iretq` 恢复完整寄存器。

## 显示与终端

Limine 请求 `1024x768x32`，以固件实际提供的尺寸、pitch 和颜色布局为准。x86_64 的 `set_mode(w, h)` 返回 `intptr_t`：取得当前显示、清屏并映射 framebuffer，硬件模式保持不变。程序通过 `framebuffer_info()` 取得真实布局。GUI 的目标 stride 独立于窗口的逻辑宽度，并支持 RGB 位序转换。

flanterm 自己处理 ANSI/VT100，默认 TTY 不再经过内核旧解析器。旧 GUI 文本 TTY 仍可使用原解析器；`VT100=0` 可以在构建时将它移除。i386 的 TextMode、HighTextMode、VGA 与 BIOS/VBE 操作继续由原后端提供，x86_64 对旧模式请求返回负状态。显示所有者退出后恢复 flanterm。

## SDL2 与桌面程序

两种架构使用同一份 `apps/sdl2` 后端，lite、Doom、invader 不再链接 `sdl2_old`。原生构建同时提供 SDL2、SDL2_ttf、SDL2_image、FreeType、zlib、libpng 和 JPEG 库。

在系统中先运行 `gui.bin`，再从 GUI 终端运行 `lite.bin` 或 `nk.bin`。Doom 与 WAD 位于 `/games`，可在该目录运行 `doom.bin`。SDL 软件 surface 直接访问 GUI 提供的共享内存，pitch 包含窗口边框；刷新只提交 damage，不再逐像素复制一份 framebuffer。显示尺寸通过 `framebuffer_info()` 查询，SDL 不请求 BIOS 模式切换。每个窗口独立处理键盘前缀和鼠标状态，支持文字、方向键、滚轮、标题更新与关闭。

当前支持软件渲染、字体、图片、键鼠及纳秒性能计时；窗口尺寸固定。OpenGL/Vulkan、音频设备、SDL 线程和 `SDL_AddTimer` 的异步回调尚未实现，不能把 `SDL_INIT_TIMER` 当成受支持的线程服务。`SDL_GetTicks64`、`SDL_GetPerformanceCounter` 和 `SDL_Delay` 可独立使用。

lite 的渲染器按 surface pitch 寻址，命令缓存按结构对齐；文件查询和路径接口使用真实 VFS 结果。nk 根据实际屏幕尺寸限制窗口，字体来自 `/data/fonts/mono.ttf`。两者均可通过 GUI 窗口关闭按钮退出。

## 日期与时间

RTC 保存 UTC。读取完整、稳定的快照，解码 BCD/二进制与 12/24 小时格式，世纪寄存器直接参与年份计算。缺少世纪寄存器时使用 1970..2069 年窗口，不再额外偏移年份。`time()` 返回 Unix 秒；`gmtime` 使用 UTC，`localtime`/`mktime` 使用当前固定的 UTC+08:00。`tm_year` 从 1900 起算，`tm_yday` 从 0 起算。GUI 每秒更新一次；单调计时仍由 HPET/内核时钟提供。

## 程序与验证

原生构建目前生成 62 个 ELF64 程序，包括 init、psh、GUI、Lua/luac、lite、nk、Doom、kcube、invader、minewep、bitz、C4、Duktape、MY-BASIC、NASM/ndisasm、压缩与网络工具，以及架构、SSE、时间、SDL、异常、IPC/RPC 和磁盘生命周期回归程序。C/C++ 运行库分别为原生 `libp.a` 和 `libcpps.a`，共享静态初始化与退出生命周期；`cpptest.bin` 验证全局/局部静态对象、析构、对齐及退出回调。已接入的应用也可单独构建，例如 `make -C apps/gui ARCH=x86_64`。尚未移植的第三方程序不能复用 i386 二进制或静态库；原生构建规则在 `apps/build-x86_64.mk`，应用及依赖在 `apps/native-apps.mk`。SDL、lite、Doom 和 NASM 的源文件清单与 i386 共用。

`setup1` 的 DOSLDR 安装流程、`tcc`/`tccinst` 的编译器与运行时工具链、`fputest` 的 x87 探针仍仅面向 i386；它们随 i386 全量构建保留。生成 ELF64 不表示所有旧演示程序的 BIOS 显示功能可用：这些调用在 x86_64 仍按约定返回错误。

原生内核模块使用 ELF64 RELA，示例为 `hello.mod`。模块管理与 i386 共用实现，x86_64 将模块映射到独立地址，重定位后设置 W^X。当前 KASAN/PERF 诊断仍只支持 i386；长模式不启用使用固定低地址 DMA 缓冲的旧软驱驱动。

```sh
# 包含 4 GiB 以上物理页的完整基础回归
python3 scripts/test-x86_64.py --firmware bios --memory 6144

# UEFI 与超过 255 个任务的持续 GUI 刷新
python3 scripts/test-x86_64.py --firmware uefi --capacity

# 单 CPU 路径
python3 scripts/test-x86_64.py --cpus 1 --memory 512

# 验证真实 PS/2 移动、左右键、滚轮及共享事件坐标
python3 scripts/test-x86_64.py --mouse --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --mouse --memory 512

# C4 的原生指针/虚拟机、NASM ELF 输出与 JavaScript 对象
python3 scripts/test-x86_64.py --tools --firmware uefi

# SDL2 surface/renderer、FreeType 字体、鼠标、滚轮和按键
python3 scripts/test-x86_64.py --sdl --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --sdl --memory 512

# 保存程序截图并通过鼠标关闭；lite 还验证文件编辑与保存
python3 scripts/test-x86_64.py --desktop-app lite --firmware uefi
python3 scripts/test-x86_64.py --desktop-app nk --firmware uefi

# 检查 GUI 终端提示符、键盘回显、退格和滚屏后的实际像素
python3 scripts/test-x86_64.py --console --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --console --memory 512
```

脚本使用串口判定结果，临时修改后恢复 `kernel/res/init.mst`，并在结束时恢复正常启动 ISO。可用 `--out` 指定日志目录、`--timeout` 调整慢速 TCG 的期限；不使用 `sendkey`。
