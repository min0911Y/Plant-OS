# 验证与交付

[开发指南](development.md)

- 仅文档修改运行 `git diff --check`，无需重建镜像。代码修改先做相关构建；涉及启动、驱动、文件系统、调度或 ABI 时，再完整构建并做 QEMU 串口回归，共享逻辑覆盖两种架构。
- i386 磁盘冒烟使用 `make -C kernel img_run`，LiveCD 使用 `livecd_run`；旧 `run`/`full_run` 引用当前不生成的软盘镜像，不能作为唯一依据。无 KVM 时使用 TCG 和合适的 CPU 型号，不修改内核迁就宿主；QEMU 用 `-serial stdio` 收集结果。
- 自动执行系统内命令只临时修改 `kernel/res/init.mst`；测试后恢复它、涉及的 `sys.cfg`/Limine 配置及正常镜像。禁止 QEMU monitor `sendkey`；QMP 输入事件只用于键鼠交互验证，不能注入测试命令。
- LiveCD 启动改动同时检查 ELF/Multiboot2 布局、默认 initramfs 冷启动及 i386 菜单的 DOSLDR 硬盘 chainload。图形/输入改动须验证实际事件和像素，不能只靠编译或单张截图。

常用回归入口：

```sh
python3 scripts/test-x86_64.py --firmware bios --memory 6144
python3 scripts/test-x86_64.py --firmware uefi --capacity
python3 scripts/test-x86_64.py --arch i386 --dynamic --memory 512
```

| 改动范围 | 回归入口或脚本选项 |
| --- | --- |
| FAT 长文件名与 initramfs | `--lfn`，覆盖 FAT12/16/32、重挂载及 mtools 互操作，见 [FAT 长文件名](fat-lfn.md) |
| IPC/RPC、磁盘、网络 | `rpctest.bin`、`dktest.bin`、`nettest.bin` |
| 可恢复异常、信号与备用栈 | `--signals`，两种架构覆盖上下文恢复、线程信号和非法帧，完成后 shutdown；见 [用户异常与信号](signals.md) |
| 任务、异常、浮点 | `guitest.bin stress`/`capacity`、`--memory-pressure`、`exc_test.bin`、i386 `fputest.bin`、x86_64 `simdtest.bin` |
| 用户态线程、TLS、运行库 | `--threads`、`--futex`，覆盖同步、分配、C/C++、stdio、浮点环境和动态链接/VM |
| LLVM、Vulkan 与 WSI | x86_64 `--llvm`、`--lavapipe --memory 3072 --timeout 600` 验证着色器、窗口像素与输入；`--compute-bench` 比较多个 worker 数并逐块核验结果，性能数据须固定宿主与 QEMU 配置 |
| OpenGL、EGL 与 llvmpipe | x86_64 `--opengl --memory 3072 --timeout 600`，覆盖离屏、GLSL、上下文共享、并发场景及 glxgears 两帧像素和键盘事件；`--gears-workers N` 选择光栅线程，性能另用 `--gears-bench`，固定宿主与 QEMU 配置 |
| GUI、输入、SDL、工具 | `--mouse`、`--console`、`--editor`、`--sdl`、x86_64 `--glfw`、`--terminal-load COUNT`、`--desktop-app`（`lite` 或 `nk`）、`--tools` |
| 动态链接与全部应用装载 | `--dynamic`、`--all-apps`，见 [动态链接验证](dynamic-linking.md#验证) |
| OpenJDK 启动与路径 | x86_64 附加 JDK 磁盘，验证 `java -version`、无参数启动和 `Startup.java`，见 [OpenJDK](openjdk.md) |
| USB、PCI、AHCI | `--usb`、`--usb-hubs`、`--usb-irq`、`--usb-root-bus`、`--ahci --machine q35`；故障与模式组合见对应专题文档 |
| APIC、SIMD 与 TLB 后端 | `--apic`、`--cpu`、`--simd`、`--tlb`，见 [多架构说明](multiarch.md) |

脚本选项按需选择，完整参数用 `python3 scripts/test-x86_64.py --help` 查看。交付前确认只包含任务需要的文件、ABI/构建/打包规则已同步，运行 `git diff --check`，说明实际完成和未完成的验证。
