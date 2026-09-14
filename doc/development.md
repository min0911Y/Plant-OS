# Plant OS 开发指南

本文及所链接的开发约束适用于整个仓库。Plant OS 是支持 i386 和 x86_64 的 freestanding 操作系统，使用自定义 ABI；不能默认依赖宿主 libc、Linux ABI 或宿主运行时。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [构建与打包](build.md) | 工具链、架构产物、构建图与镜像打包 |
| [启动、架构与 ABI](architecture.md) | 启动协议、内存映射、系统调用与运行库边界 |
| [用户异常与信号](signals.md) | 可恢复异常、信号、备用栈与线程上下文 |
| [并发与资源生命周期](concurrency.md) | 锁、线程、调度、时钟、TLB 与 DMA |
| [子系统边界](subsystems.md) | Shell、输入、GUI、文件系统、设备、网络与计时 |
| [LWJGL 3 原生移植](lwjgl.md) | x86_64 Java core、GLFW、OpenGL、STB 的构建、部署与回归 |
| [验证与交付](testing.md) | 构建验证、QEMU 回归及交付检查 |

各文档汇总对应领域的长期约束，并链接到具体专题的实现说明和验证细节。文中的源码路径与命令均以仓库根目录为基准。

## 开发原则

- 以当前 Makefile、源码及调用链为准。两个架构都在 `kernel/`，没有 `kernel64/`；历史 README、迁移记录及 `scripts/build_rootfs.sh` 不能作为当前构建依据。
- 项目自有 C/C++ 遵循根目录 `.clang-format`：2 空格缩进、不使用 Tab、左大括号同行、指针星号靠变量。只格式化本次触及的代码，不机械重写第三方目录。
- 使用项目现有类型、分配器、锁和日志接口；地址使用 `uintptr_t` 等原生宽度类型，检查溢出、范围、对齐和分配失败。构造失败要完整回滚，不发布半初始化对象。
- 不随意执行全量 `clean`，不提交 `apps/out/`、`apps/libs/`、`loader/out/`、`kernel/obj/`、`kernel/img/` 及生成的镜像、日志；明确要求交付的产物除外。
- 完成修改后同步 doc/ 中受影响的长期规则；实现细节和验证步骤放入对应专题文档，不追加修复流水账、过时注释或重复约束。

## 代码位置

| 路径 | 职责 |
| --- | --- |
| `kernel/dos/` | 初始化、任务、内存、IPC、系统调用和通用服务 |
| `kernel/arch/x86/{i386,x86_64,common}/` | 架构后端及共享 x86 实现 |
| `kernel/platform/pc/`、`kernel/drivers/` | PC 固件与设备实现、存储/网络/USB 驱动 |
| `kernel/fs/`、`kernel/io/`、`kernel/net/` | VFS/文件系统、TTY/输入/显示、lwIP/socket |
| `kernel/include/` | 公共接口；架构布局放在对应 `arch/` 头文件中 |
| `kernel/boot/`、`loader/`、`kernel/res/` | 磁盘启动扇区、独立 DOSLDR、镜像资源 |
| `apps/include/`、`apps/libp/`、`apps/ldso/` | 用户 ABI、C/C++ 运行库、动态链接器 |
| `apps/<name>/`、`scripts/`、`doc/` | 用户程序、宿主构建/测试工具、专题文档 |
