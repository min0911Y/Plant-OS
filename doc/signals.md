# 用户异常、信号与线程上下文

[开发指南](development.md) · [并发约束](concurrency.md) · [动态链接](dynamic-linking.md)

Plant OS 使用原生信号 ABI，公开定义位于 `apps/include/signal.h` 和
`apps/include/ucontext.h`，不兼容 Linux 的信号帧或 syscall。i386 与 x86_64
共用信号分派、屏蔽和备用栈管理，架构层负责异常入口及寄存器/FPU 状态转换。

## 接口与生命周期

- `sigaction` 支持 `SA_SIGINFO`、`SA_ONSTACK`、`SA_NODEFER`、`SA_RESETHAND`；
  未实现的 flags 返回 `EINVAL`。`signal` 是原生 `sigaction` 包装，返回完整宽度的
  旧处理器地址，`SIG_DFL` 与 `SIG_IGN` 有明确语义。
- 处理器由 task group leader 持有，组内线程共享；屏蔽字、pending 位集合、
  备用栈和活动信号帧属于线程。标准信号合并重复投递，不提供实时信号队列。
  设为 `SIG_IGN` 时清除组内已有的对应 pending 位。
- `sigprocmask`、`pthread_sigmask`、`sigpending` 和集合操作管理线程信号屏蔽。
  `pthread_sigmask` 与 `pthread_kill` 直接返回错误号，其余标准包装返回 -1 并设置
  errno。`SIGKILL` 不可屏蔽或安装处理器，通过现有任务终止机制结束线程组。
- `raise` 定向投递给调用线程；`pthread_kill` 校验同组 TID 与 generation，信号 0
  只检查线程是否存在。键盘 Ctrl+C 进入同一投递路径。
- 新线程继承创建者的屏蔽字，pending 集合为空，备用栈禁用。fork 复制调用线程的
  屏蔽字、备用栈及活动信号帧，并从组 leader 复制处理器；子进程没有 pending 信号。
  普通执行沿用创建新进程的模型，初始信号状态为空。
- `sigaltstack` 校验完整可写范围及最小容量，处理器运行于备用栈时禁止更换或禁用它。
  不转移映射所有权；调用者须让栈存活到解除注册且所有处理器返回。嵌套信号继续向下
  使用当前备用栈，不重置栈顶。

## 异常与恢复

用户 #DE、#UD、#PF、#GP、#DB/#BP、#MF/#XM、#AC 分别进入相应的
`SIGFPE`、`SIGILL`、`SIGSEGV`、`SIGTRAP` 或 `SIGBUS` 路径；`siginfo_t`
提供信号号、原因和故障地址。页故障先尝试正常 COW/按需分配，再报告用户故障。
默认未处理、被屏蔽或被忽略的同步故障终止所属进程，避免反复执行同一故障指令。
内核异常、NMI、双重故障和机器检查仍按既有策略停机。

三参数处理器的最后一个参数指向原生 `ucontext_t`。处理器可修改
`uc_mcontext` 中的用户 PC、SP、通用寄存器和浮点状态，也可修改返回后的
`uc_sigmask`；`uc_stack` 只描述被中断时的备用栈状态，不能用它重新注册备用栈。
`uc_link` 保留为 NULL；本接口不提供 `getcontext`/`setcontext`/`swapcontext`。

保护页恢复可以在处理器中调用 `mprotect`，返回后重试原指令；栈溢出处理须预先
安装 `SA_ONSTACK` 处理器和独立备用栈。JVM 平台层也可改写 PC/SP 跳到恢复入口。
这提供 HotSpot 移植所需的基础机制，不代表已经验证某个 JVM 的安全点协议。

信号返回统一使用 `0x65` syscall。内核只接受当前线程的活动帧，复制并检查完整
用户范围，检查 PC/SP 用户地址界限，强制用户段选择子并过滤特权 flags。
x86_64 另校验 MXCSR 和可用的 YMM 状态；不接收用户提供的 XSAVE header。
任何非法返回帧或不可写的信号栈均终止用户进程，不能引发内核缺页或执行特权返回。

x86_64 保留全部通用寄存器、RFLAGS、x87、XMM 和已启用的 YMM 高半部；i386
保留通用寄存器、EFLAGS 与 x87，通过现有 lazy-FPU owner 协议保存/恢复。
导出前清零保留字段、空闲 x87 槽及未使用的 YMM 高半部。线程 TLS 指针保持原生
线程 ABI 管理，不能通过信号帧更改。

## 当前边界

异步处理器只在真正返回用户态时运行，不在资源操作尚未结束的内核栈上执行。
futex、poll 和管道阻塞读写支持信号中断：已存在的未屏蔽信号阻止进入等待，等待期间收到信号则
唤醒并返回 `EINTR`；调用者可按自身语义重试。其他阻塞 API 暂保持原有完成语义，
尚未统一实现信号中断、`SA_RESTART`、`sigsuspend`/`sigwait`、跨进程 `kill`、
作业控制或实时信号。处理器仍须遵守异步信号安全约束，避免重入分配器、stdio
或持有相同锁的运行库调用。

## 验证

```sh
python3 scripts/test-x86_64.py --signals --cpu max,-xgetbv1 --memory 3072
python3 scripts/test-x86_64.py --signals --arch i386 --memory 512
python3 scripts/test-x86_64.py --simd --cpu max,-xgetbv1 --memory 3072
python3 scripts/test-x86_64.py --simd --cpu qemu64 --memory 3072
```

`--signals` 运行 `exc_test.bin` 的异常终止及可恢复信号测试，再运行线程、futex、
动态链接/VM 和对应架构的 SIMD/FPU 回归。新增测试覆盖保护页及实际用户栈缺页
恢复、INT3 断点、整数除零、修改 PC/通用寄存器、浮点状态、备用栈和嵌套信号、pending 合并及忽略、
线程屏蔽继承、定向投递、futex 中断和非法返回帧。`--simd` 用真实 Ctrl+C 事件验证
完整 XMM/YMM 与 x87 状态。两种模式均在命令完成后执行 `shutdown` 并确认 ACPI S5；
宿主 deadline 仅用于判定卡死，不作为成功退出条件。

TCG 验证使用 `max,-xgetbv1`，保留 AVX 并覆盖完整 YMM 保存路径。QEMU 10.2.1
的 [`get_xinuse`](https://github.com/qemu/qemu/blob/v10.2.1/target/i386/tcg/fpu_helper.c#L2725)
不跟踪 AVX 的 XINUSE，直接报告使用中；因此其 `max` 模式无法用于断言清空后的
YMM 为未使用。该优化路径须在提供准确 XGETBV(1) 的 CPU/KVM 上验证，不能为迁就
模拟器修改内核语义或放宽测试断言。
