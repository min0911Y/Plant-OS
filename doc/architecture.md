# 启动、架构与 ABI

[开发指南](development.md)

- 通用内核通过 `arch.h`、`platform.h`、`irq.h` 使用后端；x86 汇编、寄存器、GDT/IDT/TSS 布局及 PC 端口操作留在架构/平台目录。共享代码不引入固定指针宽度、私有 CPUID/port I/O 包装或 BIOS 细节。
- i386 磁盘链为启动扇区 → DOSLDR → `kernel.bin`；Limine LiveCD 通过 Multiboot2 直接加载内核与 FAT initramfs。保持链接脚本的地址断言和前 32 KiB 内的 Multiboot2 header；initramfs 物理页须及时保留，不能被内存探测覆盖，作为可写但不持久化的 `R:` 根盘。
- x86_64 使用 Limine native/base revision 3 的内存图、HHDM 和固件信息，不探测写 RAM、不扫描 BIOS 区或调用实模式服务。MP request 必须声明 x2APIC 支持，保留固件已启用的模式。AVX 按 CPU 能力启用，任务及入口/信号帧须完整保留 YMM，按需保存时依据硬件 XINUSE，导出信号帧前清理未使用数据与保留区；基础二进制保持 SSE2，JIT 按 CPUID/XCR0 选择指令，AVX-512 未启用。启动协议、SIMD/XSAVE 与 syscall/信号帧约束见 [多架构说明](multiarch.md)。
- 页元数据须覆盖启动器和模块占用的 RAM，不能只覆盖最高空闲区；这些页保持保留，不纳入分配。MMIO 和普通固件保留地址不能被当作 RAM 扩大范围。
- 内核保持 freestanding 编译约束，C++ 禁用异常和 RTTI。i386 使用 x87、禁用 MMX/SSE；x86_64 两侧使用 `-mno-red-zone -msse2 -mfpmath=sse -mlong-double-64`。i386 执行字符集为 GB2312，x86_64 应用为 UTF-8；修改文字输出时检查编码和字节长度。
- 用户 syscall 编号和参数顺序统一维护在 `apps/libp/arch/syscalls.inc`。修改 ABI 时同步内核处理器、`apps/include/`、两种架构包装及调用方，保留结构大小断言；i386 包装必须保存 EBX、ESI、EDI、EBP。用户指针及其完整范围必须验证，变长结果使用 query + capacity。
- 匿名 VM 的预留占用与访问权限分开表示；`PROT_NONE` 不释放地址或内容，`MADV_DONTNEED` 保留权限并丢弃内容。fork、exec、回收及空闲地址查找必须识别非 present 用户叶映射；替换和撤销提交须在 TLB 同步后回收旧页，保持 W^X。文件映射的 backing 另按 address space 登记，与 VFS cache 页共用同一份数据：私有映射经 COW、共享映射直接别名，普通 read/write 与映射写必须互相可见，范围、映射期间 truncate 及生命周期边界如实报错，不静默返回未定义内容。ABI、HotSpot 内存生命周期及当前限制见 [动态链接](dynamic-linking.md#虚拟内存-abi)。
- 汇编保存顺序与 C 结构是内部 ABI；修改时检查任务初始栈、切换、fork、信号和返回路径。返回用户态前校验完整 frame；除合法 COW 和 lazy-FPU 恢复外，普通用户异常经原生信号处理，未处理或不可恢复时终止所属进程；内核异常及 NMI/双重故障/机器检查停机。处理器、备用栈与受校验的上下文返回见 [用户异常与信号](signals.md)。
- 正式应用使用原生 `ET_DYN` PIE、`/lib/ld.so` 和 `libp.so`，C++ 另用 `libcpp.so`；统一经 `apps/libp/entry.c` 初始化后调用 `main`。静态自举解释器与 i386 TCC SDK 单独构建，不混用 PIC/非 PIC 归档。
- `libcpp.so` 使用配套的原生 libc++/libc++abi，禁用异常和 RTTI；C++ 头文件取对应架构的构建产物，不恢复旧 GNU C++ 头文件或第二套 ABI。每个 ELF 对象拥有独立 hidden `__dso_handle`，依赖装载与 TLS 保持在现有解释器中。
- 动态链接只在 `apps/ldso/` 中实现。内核经 `loader_start_t` 交付原 ELF fd 和路径，从系统启动盘加载解释器；ELF 与启动 ABI 由 `apps/include/elf.h`、`loader.h` 单源定义。库搜索、重定位、构造/析构及 VM 权限规则见 [动态链接](dynamic-linking.md)。
- ELF/VM 映射须检查大小、溢出、地址冲突和权限，失败回滚，拒绝 W+X；i386 保持 CR0.WP，区分 COW 与真正只读页。用户初始栈和堆用共享零页按需分配，写入时走 COW；COW 缺页分配失败终止所属任务。用户页表先分离内核共享映射，物理页引用只经正式 page API 维护；线程退出不能回收地址空间仍持有的页。
- 系统盘必须含 `init.bin`、`psh.bin`、`sys.cfg`、`lib/ld.so` 和 `lib/libp.so`，探测失败必须 panic。启动扇区、加载地址、ELF 入口或磁盘布局变化须检查 loader/kernel 两侧并完整构建、冷启动。
