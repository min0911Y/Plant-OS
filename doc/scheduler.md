# 多核调度与基准

调度器位于 `kernel/dos/task/mtask.c`。任务注册表维护稳定 TID/generation 和
任务生命周期，每 CPU 的 intrusive runnable 链表负责调度。链表包含当前运行
任务，不包含 idle、阻塞、构造中或退出后的任务；负载使用 64 位累计权重。
所有修改仍由现有 kernel lock 串行化，不能只靠关闭本地中断实现跨 CPU 互斥。

状态切换统一维护 runnable 成员关系，外部等待路径使用
`task_fall_blocked_reason`，运行中任务改权重使用 `task_set_weight`。
迁移在同一临界区转移成员关系和负载。fork 不继承父任务的链表指针。
非 waitable 的当前任务在切换前进入退休链表，后续调度才释放其内核栈；
waitable 任务由 join/wait 回收。不能在离开旧栈之前发布可回收记录后调用
可能调度的资源清理代码。

选任务只扫描本 CPU 的 runnable 集合，按 urgent、vruntime 和 TID 选择，
显式 next 提示保留同分优先。复杂度为 O(R_cpu)；负载查询 O(1)，唤醒选核
O(C)，周期均衡 O(C + R_busiest)。这消除了普通调度对任务注册表历史容量、
其他 CPU 任务和阻塞线程数量的直接依赖。生命周期枚举、IPC timeout 扫描等
其他路径仍可能与任务总数有关，不能把此次改动解释为整个内核均已无全局扫描。

本次保留 tick 记账、urgent 优待、周期单任务迁移和全局内核锁，以隔离数据
结构优化。运行时间精确记账、idle 拉取、带迁移代价的均衡及拆锁须分别评估；
当前实现不保证这些问题已经解决。

## 原生基准

源码为 `apps/schedbench/schedbench.c`，可执行文件使用 FAT 8.3 名称
`schbench.bin`。通过应用构建图注册和打包，无私有运行时或 Linux ABI。

```sh
make -C apps/schedbench ARCH=x86_64
# 系统内：参数依次为阻塞线程数、重复次数，默认 256、7
schbench.bin 256 7
# 宿主自动构建、冷启动并收集串口，恢复 init.mst 与正常镜像
python3 scripts/test-x86_64.py --sched-bench 256 --accel kvm --cpu host \
  --cpus 4 --memory 2048 --timeout 600 --out /tmp/scheduler-new
python3 scripts/compare-scheduler.py /tmp/scheduler-base /tmp/scheduler-new \
  --out /tmp/scheduler-comparison.json
```

每次冷启动运行 before、parked、after 三阶段：先测量，然后创建指定数量的
无限期 futex 阻塞线程，最后回收全部阻塞线程再测量。每阶段先预热一轮，再
保留指定轮数。通过任务快照确认线程实际已进入 WAITING，而不只检查用户态
ready 标志。各测量项为：

| 字段 | 工作负载与含义 |
| --- | --- |
| handoff_ns | 主线程和一个 worker 完成 1000 次 futex 往返的总时间，逐次校验交接值 |
| p50_ns / p95_ns / p99_ns | 单轮内 1000 次往返延迟的分位数；比较工具报告各轮分位数的中位数 |
| yield_ns | 1000 次 api_yield 的总时间；包含 syscall 和选任务，不能当作 1000 次上下文切换 |
| compute_ns | C 个 worker 各执行固定 200 万次整数 xorshift，逐 worker 校验结果；包含启动通知和完成等待 |

分配、线程创建、等待确认、join、串口输出均在测量区间之外。worker 完成后
先阻塞，避免退出清理污染计时。计算状态为 worker 私有局部变量，循环内无
共享原子计数或时间 syscall。失败输出 SCHEDBENCH FAIL 并终止进程，由正常
进程退出路径回收已创建的线程和地址空间。

`monotonic_ns()` 自身是 syscall，延迟样本包含读钟和 futex 协议开销；因此
这不是裸上下文切换指令的测量。没有用户线程绑核接口，不声称区分同核/跨核
交接。i386 同地址空间线程固定在一个 CPU，线程基准只能用于单核调度回归，
不能据此计算 i386 多核并行效率。

比较工具拒绝失败、不完整、重复记录以及 QEMU 配置或工作量不一致的输入。
输出每项耗时中位数、最小/最大值和减少比例 `100 * (1 - new / baseline)`；
负值表示变慢。固定工作量下吞吐提升为 `100 * (baseline / new - 1)`，两者
不可混称。before 与 after 比较可观察历史槽位峰值留下的成本；不同 sleeper
规模必须重新冷启动，不能复用已扩容的基线注册表。

性能比较应固定宿主、加速器、CPU 型号、内存和 QEMU 版本，避免与构建或其他
基准同时运行，覆盖 1/4/8 个 vCPU 并交错运行基线和改进版。TCG 与 KVM 数据
分别报告。纯计算作为对照，不预期本次优化能显著提高其吞吐。尾延迟波动大时
应增加冷启动样本，不能仅凭一轮 p99 宣称收益。

## 测量结果（2026-09-08）

基线为提交 `adb629b` 的内核，基线和改进版使用相同基准工作负载。
宿主报告 Intel Core Ultra 7 270K Plus，宿主本身运行于 Microsoft hypervisor；
客体使用 KVM、QEMU 10.2.1、`-cpu host -machine pc`、BIOS、2048 MiB。
每个配置分别冷启动，256 个 sleeper，每阶段预热 1 轮、测量 7 轮。
基线按 1/4/8 核、改进版按 8/4/1 核运行，测量时不并行构建。
这是该虚拟化环境下的微基准，不是裸机或应用整体加速比。

下表为 1000 次 yield 的耗时中位数，单位微秒：

| vCPU | 阻塞线程存在：基线 → 改进 | 耗时减少 | 全部回收后：基线 → 改进 | 耗时减少 |
| --- | --- | --- | --- | --- |
| 1 | 379.36 → 119.17 | 68.59% | 394.20 → 119.12 | 69.78% |
| 4 | 430.83 → 119.92 | 72.17% | 435.30 → 120.67 | 72.28% |
| 8 | 378.93 → 119.27 | 68.52% | 380.91 → 127.53 | 66.52% |

峰值前，三个配置的 yield 耗时减少 25.92%–28.75%；峰值后的差距更大，
符合移除历史槽位扫描的预期。改进版 parked 阶段的纯计算耗时变化为
-0.00%、+1.76%、-0.11%（正数代表减少），基本未变。

futex 往返总耗时并非稳定改善：parked 阶段在 1/4/8 核分别减少 1.37%、
-2.14%、2.97%；8 核 after 阶段反而增加 3.26%。跨核 IPI、全局内核锁、
时间 syscall 和虚拟机调度仍包含在测量中。当前数据仅支持调度扫描成本下降，
不足以认定 futex 尾延迟或真实应用吞吐有稳定收益。

原始结果目录为 `/tmp/sched-base-{1,4,8}` 与 `/tmp/sched-new-{1,4,8}`，
每目录包含串口和 configuration.json；比较结果为
`/tmp/sched-comparison-{1,4,8}.json`，含各项中位数、范围和比例。
这些日志与镜像属于本地验证产物，不纳入仓库。

## 验证覆盖

本次完成两种架构的完整构建与 LiveCD 打包，并通过：

- x86_64 BIOS 的 1/4/8 核基线和改进版基准，每阶段 7 轮。
- x86_64 UEFI 与 i386 BIOS 的 4 核 `--threads`：pthread/TLS、C/C++、
  stdio/futex 和动态链接。
- 两种架构 4 核 `--mouse` 完整回归：异常、浮点、IPC/RPC、磁盘、动态链接、
  网络及 GUI 输入；鼠标实际事件与图形验证通过。
- i386 BIOS、4 核、512 MiB 的 256 sleeper 基准；以及 DOSLDR 磁盘链启动
  的 32 sleeper 基准。i386 数据仅作正确性验证，不作为多线程并行加速结论。
- 比较脚本的真实结果解析、不同 CPU 配置拒绝及 Python 语法检查。

测试结束恢复 `init.mst` 和正常磁盘/LiveCD 镜像。没有进行裸机性能测试，
也没有将有限重复次数解释为尾延迟改善的统计保证。
