# 并发与资源生命周期

[开发指南](development.md)

- 普通临界区成对使用 `irq_save()`/`irq_restore()`，保留调用者中断状态。等待路径在同一临界区检查条件、发布等待并处理 ready 竞态；调度启动后用 waiter/timer 阻塞，不持有 kernel lock 忙等。
- 用户态地址等待统一使用 `apps/include/futex.h` 的进程私有 futex，使用绝对单调 deadline 和原子谓词，不能消费 IPC 消息或自旋替代阻塞；任务回收前必须摘除内核栈上的等待记录。运行库与渲染器依赖见 [lavapipe 移植](lavapipe.md)。
- 原生线程通过 `native_thread.h` 管理 TLS 指针及可选托管栈/TLS 映射；退出后由内核释放映射。pthread 和 `AddThread` 共用 TLS 初始化，errno、locale、TSS、C++ 线程析构与浮点环境按线程处理；x86_64 使用 FS，i386 使用 GS。修改线程运行库时同步 fork 的锁交接和子线程身份恢复。
- IRQ、异常和 syscall 入口按现有约定进入/离开 kernel lock；可能调度后重新读取当前 CPU。IRQ 回调不分配、不阻塞、不自行 EOI 或切换任务，由统一分派器完成 EOI 和调度；ISA 使用独占注册，PCI INTx 使用共享注册。
- 任务资源全部构造完成后调用 `task_publish`，失败用 `task_abort_creation`；启动参数及输入队列归新任务所有；任务退出取消 waiter/timer 并释放所属资源，内核栈只在切离后随任务槽回收。任务注册表通过迭代器访问，TID/页引用不得收窄为 8 位，异步引用用 TID/generation 识别。
- 保持每 CPU 的 current、idle 和运行队列，运行队列、累计权重和可迁移任务计数随状态、绑核、迁移和权重变化同步维护；空闲拉取与周期均衡共用迁移判定，只迁移未运行且未绑核的任务并严格减小负载差；选任务与选核不扫描任务注册表，外部阻塞统一走 task_fall_blocked_reason，权重修改走 task_set_weight。设计和基准见 [调度器](scheduler.md)。只有 BSP 推进全局时钟及 timeout。调度器不可自切换；BSP 在资源就绪且释放最外层 kernel lock 后唤醒 AP，AP 等待 release 时休眠。
- 调度运行时间在切换边界按每 CPU 时钟结算，迁移不得比较不同 CPU 的原始时间戳；任务统计继续使用毫秒 ABI。硬件/虚拟化时钟和 CPU 身份快路径留在架构后端，启用前完成相关 CPU 的初始化。
- 全局单调时钟保持启动时间基准，按可用的 pvclock、经频率与跨 CPU 对齐校验的 invariant TSC、平台时钟依次选择及回退；使用虚拟化快时钟前须验证跨 CPU 单调保证，切换后保持时间前进。平台硬件读钟与架构时钟选择分离，不能复用未校验的 CPU 局部时间戳。
- x86_64 同一地址空间的线程可跨 CPU 调度；页表修改统一经同步 TLB shootdown 处理活动 CPU 和缓存的 PCID，确认完成后才能回收旧页，共享内核映射须覆盖所有 CPU/PCID。TLB IPI 不获取 kernel lock，等待该锁的 CPU 也须处理失效请求；进程回收须等远端线程退出。i386 尚无同步 shootdown，同一地址空间仍固定到一个 CPU，BIOS/VBE 仅在 BSP 执行；跨进程共享映射仍只修改未在 CPU 上运行的目标。
- DMA 使用正式 page/DMA/MMIO API 和驱动持有的缓冲，不指向等待调用者的栈或用户地址。硬件等待使用单调 deadline；失败不自动重放写入。停止设备并确认不再 DMA 后才释放资源，无法确认时禁用 bus master 并隔离相关页。
