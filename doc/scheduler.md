# 多核调度与基准

调度器位于 `kernel/dos/task/mtask.c`。任务注册表维护稳定 TID/generation 和
任务生命周期，每 CPU 的 intrusive runnable 链表负责调度。链表包含当前运行
任务，不包含 idle、阻塞、构造中或退出后的任务；负载使用 64 位累计权重，
另维护未绑核 runnable 数量以快速跳过无法捐出任务的 CPU。
所有修改仍由现有 kernel lock 串行化，不能只靠关闭本地中断实现跨 CPU 互斥。

状态切换统一维护 runnable 成员关系，外部等待路径使用
`task_fall_blocked_reason`，运行中任务改权重使用 `task_set_weight`。
迁移在同一临界区转移成员关系、负载和可迁移计数；绑核前先按旧标志摘除，
再按新标志入队。fork 不继承父任务的链表指针。
非 waitable 的当前任务在切换前进入退休链表，后续调度才释放其内核栈；
waitable 任务由 join/wait 回收。不能在离开旧栈之前发布可回收记录后调用
可能调度的资源清理代码。

选任务只扫描本 CPU 的 runnable 集合，按 urgent、vruntime 和 TID 选择，
显式 next 提示保留同分优先。复杂度为 O(R_cpu)；负载查询 O(1)，唤醒选核
O(C)。均衡扫描 CPU 元数据及可能捐出任务的 runnable 链表，最坏为
O(C + ΣR_donor)，不扫描全局任务槽。全绑核负载和只有当前任务可迁移的 CPU
通过计数直接跳过。这消除了普通调度对任务注册表历史容量、
其他 CPU 任务和阻塞线程数量的直接依赖。生命周期枚举、IPC timeout 扫描等
其他路径仍可能与任务总数有关，不能把此次改动解释为整个内核均已无全局扫描。

CPU 本地没有可执行任务时，在进入 idle 前主动拉取任务；已经 idle 的 CPU
也会在后续调度时检查，不必等约 100 ms 的 BSP 周期均衡。周期均衡仍每次
最多搬一个任务，选择最低负载 CPU，并复用同一拉取逻辑。搜索不会因最忙
CPU 全部绑核或仅有正在运行的任务而中止，会继续检查其他来源。

令来源与目标的权重差为 D，候选权重为 W，仅在 `0 < W < D` 时迁移。
搬移后的负载差为 `abs(D - 2W)`，严格小于 D；选择
`min(W, D - W)` 最大的候选。这既避免搬走过重任务反而加剧失衡，也避免
将某个 CPU 唯一的 runnable 任务在空闲 CPU 间来回搬移。迁移排除 on_cpu、
PINNED 和 terminate_pending，保留既有 vruntime 归一化与 reschedule 通知。

tick 记账、urgent 优待和全局内核锁仍保留。这里的负载是调度权重，并非
实际 CPU 利用率；未来的精确时间记账、缓存迁移代价和拆锁仍须单独测量。
当前也没有 tickless idle 或硬实时唤醒保证。

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
分别报告。计算循环作为对照，其整批完成时间仍包含放置、均衡和通知成本，
不能将该字段的变化直接解释为单核计算速度变化。尾延迟波动大时
应增加冷启动样本，不能仅凭一轮 p99 宣称收益。

## 就绪队列测量结果（2026-09-08）

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

## 长短任务均衡基准

```sh
# 系统内：默认预热 1 轮，测量 7 轮
schbench.bin balance 7
python3 scripts/test-x86_64.py --sched-balance --accel kvm --cpu host \
  --cpus 4 --memory 2048 --timeout 600 --out /tmp/balance-new
python3 scripts/compare-scheduler.py /tmp/balance-base /tmp/balance-new \
  --out /tmp/balance-comparison.json
```

每轮创建 `4 × C` 个 worker，确认全部阻塞后通过同一个 futex 门闩统一唤醒。
每四个任务中一个执行 4000 万次 xorshift，其余执行 100 万次，长任务总数为 C。
固定的长短排列用于暴露轮转式唤醒放置遇到异构工作量时的失衡，不改变系统
选核、不使用绑核测试 syscall，也不引入自旋屏障。每个结果都对照预计算的
校验值，创建、准备和退出清理均在计时区间之外。

`elapsed_ns` 为从发布门闩到全部完成的整批耗时；`long_cpus` 和 `max_long`
分别表示长任务首次运行覆盖的 CPU 数，以及单个 CPU 上首次运行的长任务
数量。它们不是总迁移次数，不能据此判断任务后来是否迁移。i386 额外断言
所有 worker 的首次运行 CPU 与所属进程一致；其结果只用于绑核正确性验证。

基线为 `d5ec5aa` 的就绪队列实现，两侧使用相同的新基准；宿主、QEMU、KVM、
内存等配置与前述实验相同。基线按 1/2/4/8 核、改进版按 8/4/2/1 核运行，
每配置独立冷启动、每轮固定工作量，耗时中位数如下（毫秒）：

| vCPU | 原周期均衡 | 空闲拉取与改进的迁移判定 | 耗时减少 |
| --- | --- | --- | --- |
| 1 | 49.802 | 48.984 | 1.64% |
| 2 | 90.076 | 51.279 | 43.07% |
| 4 | 149.460 | 51.454 | 65.57% |
| 8 | 167.680 | 54.269 | 67.64% |

4 核的 7 轮范围为基线 116.497–162.832 ms、改进版 50.583–52.272 ms。
单核差异很小，不应视为单核算法收益；多核结果支持此混合任务场景下减少了
积压导致的空闲，不能外推为所有应用都有相同加速。首次执行分布也不受测试
强制控制，原始记录包含各轮分布供核查。

原始记录位于 `/tmp/sched-balance-baseline-{1,2,4,8}` 与
`/tmp/sched-balance-new-{1,2,4,8}`；比较结果位于
`/tmp/sched-balance-comparison-{1,2,4,8}.json`。

另一次相邻运行的 4 核复测，基线中位数为 106.262 ms，改进版为 53.914 ms，
耗时减少 49.26%；范围分别为 58.459–163.135 ms 与 52.136–57.001 ms。
基线的首次运行分布有波动，因此不把首次测量的 65.57% 当作固定加速比例。
复测记录为 `/tmp/sched-balance-confirm-{base,new}`，比较结果为
`/tmp/sched-balance-confirm.json`。

原有 slots/handoff 基准也重新运行。yield 批次保持约 0.12 ms；相邻复测的
compute 批次由约 5.2–5.3 ms 降至约 2.9–3.1 ms，包含更及时调度的收益，
并非 xorshift 指令本身变快。futex p99 相对较早的样本曾升高约 18%–39%，
但在相邻基线/改进版复测中又降低约 10%–16%。因此当前不对 futex 尾延迟作
稳定改善或稳定退化的结论，保留各轮记录供进一步控制宿主干扰后评估：
`/tmp/sched-balance-slots-comparison.json`、`/tmp/sched-slots-confirm.json`。

均衡改动完成两种架构的完整构建与正常镜像恢复，并通过 x86_64 UEFI
`--threads`、两种架构 BIOS `--mouse` 完整回归、i386 带显式绑核断言的
`--sched-balance` 和 DOSLDR 磁盘基准。比较器同时验证旧基准与均衡基准，
拒绝混合测试类型或不同 CPU 配置。未添加用户绑核 ABI，未改全局内核锁或
时间记账，也未将此结果外推到裸机。
