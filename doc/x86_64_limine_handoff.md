# x86_64 + Limine 迁移交接文档
> 历史记录：本文描述已经删除的 `kernel64/` 实验，与当前实现无关。当前 i386/x86_64 后端共用 `kernel/`，请以 [多架构构建与 ABI](multiarch.md)、根目录 `AGENTS.md` 和现行 Makefile 为准。

本文档用于下一次对话直接续接当前 `x86_64` 重构工作。内容以当前仓库实际代码为准。

## 当前结论

当前 `kernel64/` 已经从“64 位最小启动骨架”推进到“有基础抽象层、可挂载 Alpine tar.zst module rootfs、可运行真实 Alpine `/bin/sh`/coreutils/bash/gcc smoke，并具备可写 ramfs、devtmpfs、最小 PTY、浅 poll/epoll 和可交互 tmux smoke 的 64 位内核雏形”。

2026-06-14 本轮进展：

- task 调度器已从旧 tickless CFS/RT/root-domain 骨架替换为 MUQSS 风格 per-CPU ordered runqueue：每个 CPU 拥有独立 skiplist-like deadline 队列，按动态分配的 level/head 管理 READY task，不再按固定 `TASK_MAX_CPUS` 静态数组扫描 runnable task；普通 task 以 deadline + priority 排序，`sched_yield` 会把当前 task deadline 后推，阻塞/唤醒只做队列摘挂，不保留旧 `vruntime/weight/rt_priority/sched_class` 兼容字段。
- task registry 从旧 `created[TASK_MAX_CPUS]` 固定槽改为动态 task 链表，短命 task 仍复用 kernel stack 和 runqueue link storage；`task_spawn_kernel()`、`task_spawn_kernel_on_cpu()`、`task_spawn_user()` 的公开 API 已删除 CFS/RT 参数，调用方只请求创建普通 task；fork/vfork/exec/wait/exit 路径改为直接维护 MUQSS 队列状态，`wait4` 无 ready child 时会阻塞等待父 task wake，不再 yield 忙等。
- 构建系统补上 `-MMD -MP` 头文件依赖生成，避免改 `struct task` 这类公开布局后增量构建只重编部分对象造成旧偏移链接；本轮曾用 clean rebuild 复现并排除该类半旧对象问题。
- 本轮验证：`make -C kernel64 clean all`、`scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst`、`make -C kernel64 img build/OVMF_VARS.fd`、`git diff --check` 均通过；残留扫描确认 `TASK_SCHED*`、`sched_class`、`rt_priority`、`vruntime`、`weight`、`task_refresh_runqueues`、`root_domain`、`TASK_MAX_CPUS` 等旧调度符号在 `kernel64/src/task.c`、`kernel64/include/task.h`、`kernel64/src/process.c` 无命中。临时 `/bin/sh -c 'for i in 1 2 3 4 5; do /bin/echo MUQSS_LOOP_$i; done; wait; echo MUQSS_DONE'` smoke 在 `/tmp/kernel64-muqss-command.log` 输出 `MUQSS_LOOP_1..5` 和 `MUQSS_DONE`，5 个 `/bin/echo` 子进程均正常 exec/exit；验证后已恢复默认 `/bin/sh` 启动并重建镜像，`/tmp/kernel64-muqss-final-shell.log` 短跑进入 `/ #`，无临时 marker、page fault 或 panic。
- kernel heap 对外接口保留 `kernel64/include/kheap.h` / `kernel64/src/kheap.c`：新增通用 `krealloc()`，初始化和统计接口为 `kheap_init()`、`kheap_base()`、`kheap_used_bytes()`、`kheap_mapped_bytes()`；分配接口继续提供 `kmalloc/kzalloc/kmemalign/krealloc/kfree`。
- 现有可用的手写扩容路径已替换为 `krealloc()`：devtmpfs node 表、PCIe device 表、epoll watch 表、用户 VMA 表、用户 fd 表、task runqueue link storage 和 ramfs owned file buffer。依赖空槽扫描的表在扩容后显式清零新增尾部，线性 append 表只保留原内容。
<!-- 过时内容（2026-06-14 复审后 kheap 重新作为正式 heap 接口）：- allocator 清理验证：`make -C kernel64 all` 通过；残留扫描确认 `kernel64/src` 和 `kernel64/include` 无 `#include <kheap.h>`、公开 `kheap_*` 接口、`kernel64/include/kheap.h` 或 `kernel64/src/kheap.c`。 -->
- allocator 清理验证：`make -C kernel64 all` 通过；残留扫描确认编译路径无旧 `<kmem.h>` 头和 `kmem_init/kmem_base/kmem_used_bytes/kmem_mapped_bytes` 旧命名，`kheap` 是正式 heap 接口。
- `sysinfo(2)` 已删除早期固定 `512MiB/256MiB/procs=1` 兼容值，改为复用 frame allocator 的 `frame_total_bytes()/frame_free_bytes()` 和动态 task 计数；临时 `/bin/sh -c 'free -h; cat /proc/meminfo'` smoke 已验证 `free(1)` 的 total/free/used 与 `/proc/meminfo` 的 `MemTotal/MemFree/FrameUsed` 同源，验证后已恢复默认 `/bin/sh` 启动并重建镜像。
- syscall 返回热路径已删除旧内联 `Lsyscall_iret_return` fallback：`syscall_entry_stub` 不再对用户 `RIP` 做 canonical 比较，普通 syscall 从统一 frame 恢复寄存器后直接 `swapgs; sysretq`；需要完整恢复 `rcx/r11` 的 `rt_sigreturn` 改为通过独立 `syscall_iret_return()` 慢出口 `iretq` 返回，后续 ptrace 这类需要全寄存器恢复的 syscall 可复用同一出口。
- syscall 返回路径验证：`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd` 通过；临时把 `process_spawn_first_user()` 改为 `/bin/sh -c` 自动用 builtin `printf` 生成、gcc 编译并运行 SIGSEGV handler 测试，handler 中 `mprotect(PROT_READ|PROT_WRITE)` 后经 `rt_sigreturn` 回到原 faulting write 并输出 `SIGRETURN_OK`，随后输出 `SYSCALL_IRET_SMOKE_DONE`。`/tmp/kernel64-syscall-iret-smoke.log` 负向扫描无 `page fault`、`syscall unhandled`、`panic`、`exception` 或 `failed`；验证后已恢复默认 `/bin/sh` 启动并重建镜像。
- syscall 入口不再显式 `cld`，依赖 `IA32_FMASK` 中的 `RFLAGS_DF` 在 `syscall` 入内核时清 DF；中断入口的 `cld` 仍保留，因为 `IA32_FMASK` 只作用于 `syscall`。`rt_sigreturn` 读取/校验 sigframe 失败时不返回 `-EFAULT` 给 trampoline，而是按坏 signal frame 递送 SIGSEGV；无可用 handler 时任务以 `128 + SIGSEGV` 退出。临时 bad `rt_sigreturn` smoke 把用户 `%rsp` 清零后直接执行 syscall 15，`/tmp/kernel64-bad-rt-sigreturn.log` 显示 `/tmp/badrt` 退出状态 139 和 `BAD_RT_SIGRETURN_DONE`。
- `execve` 仍走普通 `sysretq` 返回路径，但 ELF loader 会拒绝非用户 canonical 的 program/interpreter entry、PHDR 和最终用户栈指针，避免把非法新 `RIP` 放进 `sysretq` 的 `RCX` 后在 ring0 #GP。临时 smoke 编译 `/tmp/noncanon` 后把 ELF header `e_entry` 改成 `0x7fffffffffffffff`，执行失败并回到 shell 输出 `NONCANON_EXEC_DONE`，日志无 #GP、panic 或 exception；验证后已恢复默认 `/bin/sh` 启动。

2026-06-13 本轮进展：

- kernel64 用户资源表清理：`struct task` 现在直接持有 opaque `user_memory_table *memory` 和 `user_signal_table *signals`，删除旧全局 task-keyed memory/signal 表和按 task 扫描；signal 表在 task 发布前分配，`user_signal_queue()` 不再 lazy allocation；VMA storage 改为按需扩容的动态数组，保留线性查找但删除固定 VMA 槽上限。
- 浅 epoll 已改为真实 fd-backed 对象：`epoll_create/create1` 通过 `user_file` 安装普通 fd entry，epoll 对象 refcount 随 close/fork/exec/fd-passing 生命周期释放，watch list 按需动态扩容；删除旧全局 8 个 epoll、32 watch 上限、1000 起 fake fd 和 `close` fallback 兼容路径。
- AF_UNIX stream socket 对象改为 `kzalloc/kfree` 生命周期，删除固定 `sockets[32]` registry；pathname bound/listening socket 用动态链表维护，accept queue 改为连接 socket 链表并按 listen backlog 限流，pending rights 改为链表节点，保留每条 `SCM_RIGHTS` 小批量上限。
- reviewer blocker 修复：AF_UNIX 全局 bound/listening 链表、accept queue、pending rights、peer/refcount mutation 现在由 socket 全局 mutex 保护，pathname lookup 在锁内 retain listener 后再 connect；epoll watch 持有 `user_file_ref`，close watched fd 后复用不会把旧 `event.data` 报给新 fd 对象，watch vector 由 epoll mutex 保护，`epoll_wait` 会在锁内取得临时 retained ref 后再 poll，DEL/release 解锁后释放 refs；fork/vfork signal table 改为继承父 action table 和 blocked mask，pending 清空，exec 仍 fresh init。

<!-- 过时内容（2026-06-13 后续 worker 已把 fd table 改为 task-owned O(1) 动态表，fd ref 改为 kzalloc/kfree，pipe 空读/满写改为最小 task blocking/wakeup；下列三条保留为上一轮吞吐优化历史，不代表当前 fd/pipe 完整状态）：
- pipe fd 层已去掉固定 16 项静态池和每 pipe 内嵌 4KB 数组；`user_file_pipe()` 现在为每个 pipe 动态 `kzalloc` pipe 对象并 `kmalloc` 64KB data buffer，最后一个 reader/writer 关闭时 `kfree(data)` + `kfree(pipe)`。fork/dup/fd-ref 仍通过 readers/writers 计数 retain/release，分配失败返回 `ENOMEM`，fd 表耗尽仍返回 `EMFILE`。
- pipe read/write 已从逐字节搬运改为环形 buffer 两段 `memcpy`，readiness 使用 `pipe->capacity`，不再依赖 4KB 固定宏或 `used` 静态槽状态。`syscall_read()`、`syscall_write_flags()`、`syscall_pread64()`、`syscall_sendfile()` 的临时 copy buffer 统一提升到 `PAGING_PAGE_SIZE`，避免 128/256 字节小循环限制吞吐。
- pipe/dd 验证使用临时 `/bin/sh -c` argv 自动执行，验证后已恢复默认 `/bin/sh` 启动。`yes | dd of=/dev/null status=progress count=200000` 在 `/tmp/kernel64-pipe-dd-default.log` 中完成 102399490 bytes，progress 约 `30.1 MB/s -> 24.6 MB/s`，最终 `24.4 MB/s`；补充 `yes | dd of=/dev/null status=progress iflag=fullblock bs=1M count=200` 在 `/tmp/kernel64-pipe-dd-1m-fullblock.log` 中完成 209715200 bytes，progress 约 `31.4 MB/s -> 28.3 MB/s`，最终 `28.3 MB/s`。原始 `bs=1G count=1` 因当前 pipe 短读语义只读到 2 bytes 后按 partial record 退出，`bs=1G iflag=fullblock count=1` 在 15s 内未输出有效 progress。dd 日志仍有 `syscall unhandled nr=278` 探测/回退，但未见 page fault 或 panic。
-->
- fd table 已改为 task-owned O(1) 访问：`struct task` 持有 opaque `struct user_file_table *files`，`user_file.c` 内部定义/分配表；删除旧固定 table 数宏、全局 table array 和按 task O(N) 扫描。`user_file_init_task()` 现在可失败，`task_spawn_user()` 会处理失败并回滚资源，fork/vfork 通过 task-owned table clone，destroy 释放并置空。
- fd entry storage 和 SCM_RIGHTS ref 都已动态化：fd 表初始容量 32，`open/dup/dup2/ref_install` 需要更大 fd 时扩容，不再把 fd 限死在 32；`/proc/self/fd` 改为遍历动态 fd 表并支持多位 fd 名。旧静态 fd-ref pool 已删除，`struct user_file_ref` 改为 `kzalloc/kfree` 生命周期。
<!-- 过时内容（2026-06-13 本轮 mutex + 精准唤醒已替代上一轮 pipe 最小 blocking/wakeup 描述；下列内容保留为历史，不代表当前 pipe 锁和普通 I/O 唤醒策略）：
- pipe 空读/满写已接入最小真实 task blocking/wakeup：task 层暴露薄 `task_block_current()` / `task_wake()`，pipe 内记录 reader/writer waiters；空 pipe 且仍有 writer 的阻塞 read、满 pipe 且仍有 reader 的阻塞 write 会睡眠等待对端写/读或关闭唤醒，不再向用户态返回 0 触发 `yes/dd` syscall 风暴。pipe 锁不跨 task block，pending signal 返回 `EINTR`，`O_NONBLOCK` 返回 `EAGAIN`。
- reviewer blocking 收尾：`task_block_current()` 在没有可切换 next task 或 next 尚无 saved rsp 时不再把当前任务恢复 RUNNING 并返回，而是在保持 `TASK_STATE_BLOCKED` 的状态下释放 scheduler lock 后等待中断/唤醒；`wake_pending` 只在 scheduler lock 下消费，不再在切回后无锁清零。
- pipe 语义补齐本轮关键边界：no-reader write 返回 `EPIPE`；`pipe2` 支持 `O_CLOEXEC | O_NONBLOCK` 并把 `O_NONBLOCK` 写入读写两端 status flags；read readiness 现在在有数据或 writers 为 0 时为真，write readiness 要求仍有 readers 且有空间；poll/select/epoll 通过 fd hangup/readiness 能看到 pipe EOF/HUP，但等待路径仍是浅轮询/yield，后续需要接入统一 wait queue 才是真正的事件唤醒。
- 本轮验证：`git diff --check`、`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd` 通过；残留扫描确认旧全局 fd table、旧静态 ref pool 和 32-fd 硬上限符号在 `kernel64/src`、`kernel64/include` 和本文档中均无命中。临时 `/bin/sh -c` 自动测试后已恢复默认 `/bin/sh`：`yes | dd of=/dev/null status=progress iflag=fullblock bs=1M count=200` 在 `/tmp/kernel64-pipe-dd.log` 完成 209715200 bytes，最终 `2.02465 s, 104 MB/s`；默认 bs 的 `yes | dd of=/dev/null status=progress count=200000` 在 `/tmp/kernel64-pipe-dd-default.log` 完成 102399490 bytes，最终 `6.79736 s, 15.1 MB/s`。最终 `/tmp/kernel64-default-shell.log` 短跑进入 `/ #`，无临时 marker；dd 日志仍有既有 `syscall unhandled nr=278` 探测/回退，但未见 page fault 或 panic。
-->
- 新增最小 sleeping mutex：`kernel64/include/mutex.h` 暴露 `struct mutex`、`mutex_init/lock/unlock`，`kernel64/src/mutex.c` 用栈上 waiter + task wake/block 实现；只在 mutex 内部 guard 的极短临界区保护状态和等待队列，pipe 数据路径不再使用 IRQ-off 自旋锁。
- pipe 锁已从 `volatile bool lock_flag` + `user_pipe_lock()/unlock()` 替换为 `struct mutex lock`；旧 wrapper、`lock_flag` 和 `user_pipe_waiter_contains()` 已删除。pipe waiter 入队由调用方用本地 `waiter.task == NULL` 控制，摘出/唤醒时清空 waiter，避免重复入队和栈 waiter 残留。
- pipe 普通 I/O 不再广播对端等待者：read 释放空间后只唤醒一个 writer，write 写入数据后只唤醒一个 reader；若同侧仍有可推进条件，仅 baton 唤醒一个同侧 waiter。close/release 走到 EOF/EPIPE 终态时 wake-all，让全部等待者退出；`user_file_hangup()` 只是 readiness/HUP 状态查询。
- reviewer 阻塞复查：task spawn/fork/vfork/reap 已重排为 scheduler lock 下只保留/发布 task slot，`user_file_init_task/clone_task/destroy_task` 和其他可能睡眠的用户资源操作不再在 `task_sched_lock()` 持有期间执行；pipe release wake-all 也改为 pipe mutex 下摘 waiter、解锁后逐个 `task_wake()`。
- 本轮验证：`git diff --check`、`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd` 通过；残留扫描 `user_pipe_waiter_contains|pipe->lock_flag|user_pipe_lock|user_pipe_unlock` 在 `kernel64/src` 和 `kernel64/include` 无命中。临时 `/bin/sh -c` 自动测试 `yes | dd of=/dev/null status=progress iflag=fullblock bs=1M count=200; echo PIPE_DD_DONE` 在 `/tmp/kernel64-pipe-mutex-dd.log` 完成 209715200 bytes，最终 `2.13678 s, 98.1 MB/s` 并输出 `PIPE_DD_DONE`；验证后已恢复默认 `/bin/sh` 启动并重建镜像，`/tmp/kernel64-pipe-mutex-default-shell.log` 短跑进入 `/ #` 且无临时 marker。

2026-06-13 上轮进展：


2026-06-12 本轮进展：

- 内核日志输出路径已和 framebuffer 终端解耦：`console_write_len()` 现在只写串口，不再写 flanterm framebuffer，也不再缓存/回放 early log；TTY/shell 输出通过显式 `console_framebuffer_write_len()` 写活动 framebuffer，同时继续镜像到串口。因此 kernel log 只在串口可见，framebuffer 保留给活动 TTY/shell。
- TTY 重绘只清理/重放 framebuffer，不再向串口发送清屏或历史输出；切换 TTY 时不会污染串口日志。
- PS/2 扩展方向键已接入普通输入流：`E0 48/50/4B/4D` 分别转换为 `ESC [ A/B/D/C`，供 shell/readline/终端程序按标准 ANSI 序列消费。
- 清理了本轮相关旧接口残留：删除 framebuffer early log buffer、`console_record_early()`、旧 `console_clear()`、未使用 `console_has_framebuffer()` 和未使用 `INPUT_KEY_CTRL_C` 枚举。

2026-06-09 本轮进展：

- 删除了 `kernel64` 遗留存储驱动和历史文件系统路径：`ahci.c/h`、legacy CF8/CFC `pci.c/h`、`fat.c/h`、`diskio.c` 和 `thirdparty/fatfs/` 已移除，`user-test-rootfs` FAT smoke target 也已删除。当前不保留 `pci_*` 兼容接口，不再暴露 `/dev/sd0`。
- 新增 ACPI MCFG + PCIe ECAM 枚举和最小 NVMe polling 驱动：ACPI 暴露全部 MCFG allocations；`pcie_init()` 逐 allocation/segment/bus range 枚举，设备 registry 和 ECAM virtual windows 按需动态扩展，不再固定一个 segment 或固定设备上限；`nvme_init()` 识别 PCIe class `01/08/02`，controller registry 动态链表管理，用 `frame_alloc_contiguous()` 分配 admin/io queue、identify buffer 和 PRP DMA 页，按 CAP.MQES clamp queue depth，按 CAP.DSTRD 和实际 queue 数映射 doorbell 范围，完成 identify controller/namespace、单队列 polling read，并通过 devtmpfs 注册 `/dev/nvme0n1`。启动顺序调整为 `acpi_init()` -> `pcie_init()` -> `nvme_init()` -> `storage_init()`。
- reviewer 非阻塞收尾：`devtmpfs` 节点表已从固定 16 项改为 `kmalloc/kfree` 动态增长数组；NVMe `/dev/nvme0n1` read path 对 `offset + done` 增加显式 overflow/bounds 防护，极端 offset/len 会按 EOF/已读字节返回，不再允许 wraparound。
- 本轮 legacy storage 清理和 reviewer blocking 复查已完成：`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`、`git diff --check` 均通过；`/tmp/kernel64-nvme-read.log` 验证 `/dev/nvme0n1` sector read/hexdump，`/tmp/kernel64-nvme-final-shell.log` 验证最终默认 `/bin/sh` 进入 `/ #`；残留扫描无固定 PCIe/NVMe registry 上限、单 MCFG range 字段、临时 smoke marker、未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、TTY/PTY/tmux 错误、AHCI/legacy PCI 或 `/dev/sd0` 残留。
- 清理了 allocator 旧兼容接口：`bootmem_alloc(size, align)`、`bootmem_usable_bytes()`、`bootmem_allocated_bytes()`、`frame_skipped_bytes()`、`FRAME_MAX_FRAMES`、`FrameSkipped` 统计已删除；`bootmem` 只保留 HHDM/early allocation/reserved boundary，运行期页分配统一走 `frame_*`。NVMe 队列/identify/PRP DMA buffer 直接使用 `frame_alloc_contiguous()`，不再通过 bootmem 兼容转调。
- `kernel64/src/frame.c` 的 buddy frame allocator 现在按 Limine memmap 最高物理页动态分配 metadata，order 0..10，order-0 每 CPU pool（32 页容量，批量 refill/drain）。运行期页表页、用户页、ELF/stack 页、SLUB slab/large 页均走 `frame_alloc_page()/frame_free_page()`，用户 COW 页仍通过 `page_ref_release()` 释放。
<!-- 过时内容（早期说明只列出 kmalloc/kzalloc/kmemalign/kfree，2026-06-14 已新增 krealloc）：- SLUB 清理了固定 range-node 池和 CPU 数硬编码：每 CPU cache 使用 `SMP_MAX_CPUS`，heap virtual range node 按需从 frame 页扩展；释放后的 heap virtual page/range 会复用。外部模块已收敛到 `kmalloc/kzalloc/kmemalign/kfree`，`kheap_*` 只作为 heap 内部/`kmem.c` 实现接口。 -->
- SLUB 清理了固定 range-node 池和 CPU 数硬编码：每 CPU cache 使用 `SMP_MAX_CPUS`，heap virtual range node 按需从 frame 页扩展；释放后的 heap virtual page/range 会复用。对外暴露 `kheap` 初始化/统计接口和 `kmalloc/kzalloc/kmemalign/krealloc/kfree` 分配接口。
- 删除默认启动残留 smoke：`kernel_start_smoke_tasks()` 默认调用和历史 `user-test-rootfs`/`kernel64/user/test_init.S` 已删除；默认镜像只保留 Alpine tar.zst rootfs 启动路径。`/proc/meminfo` 现在只暴露 frame/SLUB 运行期统计：`MemTotal/MemFree/MemAvailable/FrameUsed/KheapUsed/KheapMapped`。
- 本轮清理验证已完成：`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`、`git diff --check` 均通过；`/tmp/kernel64-cleanup.log` 中 30 次 gcc loop 到达 `GCC_LOOP_30`，tmux detached smoke 输出 `tmux-inside` 并到达 `CLEANUP_DONE`，负向扫描无未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。最终镜像已恢复默认 `/bin/sh` 启动，`/tmp/kernel64-cleanup-final-shell.log` 短跑进入 `/ #`，没有临时 smoke 残留。

2026-06-08 本轮进展：

- bash `cd` 缺失 syscall 已修复：新增 per-task `cwd`，`chdir(80)`、`fchdir(81)`、`getcwd(79)` 和 `AT_FDCWD` 路径解析现在使用当前 cwd；fork/vfork 会继承 cwd，exec 不改变 cwd。QEMU smoke 已验证 `/bin/bash -lc 'cd /tmp && pwd && printf x > cd-ok && cat /tmp/cd-ok'` 输出 `/tmp` 和 `x`
- 旧 `devfs` 已替换为 `devtmpfs`：源码/头文件改为 `kernel64/src/devtmpfs.c`、`kernel64/include/devtmpfs.h`，设备注册和启动挂载同步改名；`/proc/mounts` 现在显示 `devtmpfs /dev devtmpfs rw,nosuid 0 0`
- `devtmpfs` 现在支持 `/dev`、`/dev/pts`、`/dev/null`、`/dev/zero`、`/dev/tty`、`/dev/tty0..3`、`/dev/ptmx`、动态 `/dev/pts/N` 和 NVMe `/dev/nvme0n1`；`/dev/null` 写入会丢弃，设备打开时 `O_TRUNC` 不再误返回只读错误
- 新增最小 PTY：`/dev/ptmx` open 分配 pty master，`/dev/pts/N` open slave；master/slave 双向缓冲读写，支持 `TIOCGPTN`、`TIOCSPTLCK`、`TIOCGWINSZ/TIOCSWINSZ`、`TIOCGPGRP/TIOCSPGRP` 和浅 termios ioctl。TTY/PTY 现在会保存 `TCGETS/TCSETS/TCSETSW/TCSETSF` 的 termios；PTY slave 输出按 `OPOST|ONLCR` 做 `\n -> \r\n`，修复 tmux 内反复回车后 prompt 阶梯式右移；PS/2 input 会把 Ctrl+字母转成 ASCII control code，Ctrl-b 可透传给 tmux prefix。QEMU smoke 已验证 `/dev/ptmx` 创建 `/dev/pts/0`，slave->master 和 master->slave 双向 `dd` 读写均成功
<!-- 过时内容（2026-06-13 epoll 已改为 fd-backed 对象和动态 watch vector，不再是 syscall.c 内全局表驱动）：- `poll/ppoll` 已改为走 fd readiness，pipe/PTY/普通文件/标准 fd 有最小可读可写判断；新增浅 `epoll_create/epoll_create1/epoll_ctl/epoll_wait` 表驱动实现，供事件循环探测使用 -->
- `poll/ppoll` 已改为走 fd readiness，pipe/PTY/普通文件/标准 fd 有最小可读可写判断；浅 `epoll_create/epoll_create1/epoll_ctl/epoll_wait` 现在是 fd-backed epoll 对象，watch storage 按需扩容且由对象 mutex 保护，供事件循环探测使用
- 最小 Ctrl+C/SIGINT 已接入并修正 TTY 空输入阻塞语义：PS/2 input 会把 Ctrl+字母转成 ASCII control code，Ctrl-C 作为 `0x03` 进入输入流后由 TTY/PTY 按 `ISIG/VINTR` 发给 foreground pgrp；`kill(2)` 支持 `sig=0` 存在性检查、1..64 浅 signal 接收和负 pgrp；默认动作是让 SIGHUP/SIGINT/SIGQUIT/SIGKILL/SIGTERM 目标任务以 `128+sig` 退出。最新 QEMU monitor smoke 已验证前台 `cat` 阻塞后收到 `^C`，退出 status=130，shell 回到 `/ #` 后可继续执行 `echo ok`
- ramfs/VFS 现在保存和返回基本 mode bits，tar mode 会导入，运行期 `mkdir/open/chmod/umask/stat/statx` 会反映模式。这个修复让 tmux 通过了 `/tmp/tmux-0` 安全权限检查
- rootfs 已用 `ALPINE_PACKAGES='coreutils bash gcc musl-dev tmux' scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst` 重建，确认安装 `tmux 3.6-r0`；`tmux -V` 可运行输出 `tmux 3.6`
- 最小 AF_UNIX stream socket 已接入 syscall/fd/VFS/ramfs 路径，`flock(73)` 已按浅兼容 no-op 返回成功；pathname socket 的旧 `EROFS` 已定位为 `unlink(87)` 删除不存在 socket path 时错误映射成 `EROFS`，现在改为返回 `ENOENT`，tmux 已能越过 `/tmp/tmux-0/<name>` socket bind/connect。默认用户环境 `TERM` 从自定义 `plos64` 改为 rootfs 自带 `xterm`，并补了 `setfsuid(122)`/`setfsgid(123)` 固定 root 兼容返回 0。本轮 reviewer blocking 已修：非 TTY `read(2)` 对 AF_UNIX/PTY/pipe 空读不再误报 EOF，会按 waitable fd 语义阻塞、`O_NONBLOCK` 返回 `EAGAIN`、pending signal 返回 `EINTR`；`sendmsg/recvmsg` 支持最小 `SCM_RIGHTS` fd passing，`getsockopt(SOL_SOCKET, SO_PEERCRED)` 返回浅 root `ucred`，未知 socket control/option 不再静默成功
- bash interactive 的 `syscall unhandled nr=270` 已修复：`pselect6(270)` 现在按浅 select 语义复用 fd readiness，支持 NULL/zero/finite timeout，忽略 `exceptfds` 和完整 signal mask 语义但会保留 `EINTR` 行为；QEMU smoke 已验证 `/bin/bash -i` 可进入交互提示符并执行 `echo bashok`
- tmux 当前 detached smoke 已跑通：task 增加最小 `sid` 和 controlling tty 状态，`setsid()` 会建立 session 并清 ctty，PTY slave 上的 `TIOCSCTTY` 会设置当前 task controlling PTY 和 foreground pgrp；`/dev/tty` 会按当前 task ctty 映射到真实 TTY 或 `/dev/pts/N`；补了 `TIOCGPTPEER` 和 Linux ioctl request 32 位规范化，修复 musl/tmux 把 `TIOCGPTN` 以符号扩展 64 位值传入导致误返回 `ENOTTY` 的问题；`TIOCSPTLCK` 作为无锁状态浅 no-op。最新 `/tmp/kernel64-tmux-detached-final4.log` 中 `tmux -L plos -f /dev/null new-session -d 'echo inside-tmux > /tmp/tmux-out'` 后 `cat /tmp/tmux-out` 输出 `inside-tmux`
- tmux 交互路径已推进到可用 smoke：`/proc/self/fd/N` 现在会枚举/链接当前 fd，标准 fd 的 `fstat/readlink` 会映射到 controlling tty；补了 `getsid(124)`、`TIOCGSID`、浅 `lgetxattr/llistxattr`、更宽的 signal 接受范围、`kill(pid, 0)` arbitrary pid/pgrp 探测和 ctty-aware `wait4` foreground handoff；`SCM_RIGHTS` 传递标准 TTY fd 时会安装为显式 `/dev/ttyN` 或 `/dev/pts/N` fd，保留 syscall TTY/PTY 路径而不走普通 devtmpfs 文件写。本轮进一步修复 PTY 默认 termios 和输出列状态：新 PTY 默认使用 cooked termios 而不是继承外层 raw termios，PTY slave 输出和 ECHO 路径统一按 `OPOST/ONLCR/OCRNL/ONOCR/ONLRET` 做最小 CR/NL 转换，并保持 CRLF 原子写入。最新 `/tmp/kernel64-tmux-layout.log` 中 `/usr/bin/tmux new` 进入 alternate screen，`seq 1 80` 滚屏后输出仍从左侧开始，`Ctrl-b d` 可 detach 回外层 `/ #`，`tmux attach` 重绘已有 pane 历史时 `43..80` 和 prompt 保持左对齐，再次 `Ctrl-b d` 可 detach；日志扫描无 page fault、panic、未处理 syscall、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`
- 多次程序运行导致的 page fault 已定位并修复主要泄漏：ELF loader 不再 `kmalloc()` 整个 gcc/cc1/ld ELF 文件，而是只读 ELF header/phdr 并按 PT_LOAD segment 从 VFS 流式读入用户页；task exit/exec replace 在释放 VMA 后会遍历并释放剩余用户 leaf pages，覆盖 ELF PT_LOAD 页和用户栈；短命 task slot 会复用已有 kernel stack；ramfs 对运行期自有文件数据会复用或通过 `krealloc()` 增长，unlink/rmdir/rename 覆盖删除的临时节点会进入小 freelist 复用。历史 gcc/tmux smoke 已通过；当前 `/proc/meminfo` heap 字段为 `KheapUsed/KheapMapped`。
- reviewer blocking 已收尾：`struct linux_epoll_event` 改为 x86_64 Linux ABI 的 packed 12 字节布局，并用 `_Static_assert` 固定 size 和 `data` offset；`ppoll` 现在会读取 timespec timeout，支持 NULL 无限、`{0,0}` 非阻塞、有限 timeout 近似毫秒等待，坏用户指针返回 `EFAULT`
- signaled task 的默认终止路径现在保证 slot 复用前走一次 `user_file/user_signal/user_memory/address_space` cleanup，避免 fd/VMA/signal/PTY refs 和 task-pointer keyed 表污染；正常用户退出也复用同一 cleanup helper
- `scripts/build_rootfs.sh` 默认包已包含 `tmux`，导出校验也检查 `P:tmux` 和 `usr/bin/tmux`；根目录临时生成物 `a.out` 已删除

2026-06-07 本轮进展：

- 已用 `scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst` 重新构建最新 `docker.io/library/alpine:latest` rootfs：容器内 `/etc/apk/repositories` 切到 `https://mirror.sjtu.edu.cn/alpine/v3.23/{main,community}`，`apk update` 后安装 `coreutils 9.8-r1`、`bash 5.3.3-r1`、`gcc 15.2.0-r2`、`musl-dev 1.2.5-r23`、`tmux 3.6-r0`，并打包为 tar.zst；脚本容器内命令已 `set -eu` fail-fast，导出后会检查 `lib/apk/db/installed` 中存在 `P:coreutils`、`P:bash`、`P:gcc`、`P:musl-dev`、`P:tmux`，artifact 中确认存在 `bin/sh`、`bin/bash`、`lib/ld-musl-x86_64.so.1`、`usr/bin/{cat,stat,sha256sum,sort,wc,gcc,cc,as,ld,tmux}` 和 `usr/include/stdio.h`
- `kernel64/build/kernel64-uefi.img` 已重新打包该 Alpine tar.zst rootfs；QEMU 日志显示 `compressed=60132384 tar=215382528 nodes=1941`
- `kernel64/Makefile` 已固定使用 vendored `kernel64/thirdparty/limine`，不需要也不支持通过 `LIMINE_DIR` 覆盖；`make -C kernel64 img` 生成 UEFI 镜像，`make -C kernel64 run` 会生成镜像并启动正常 QEMU 图形窗口
- 启动期首进程现在直接 `execve("/bin/sh", {"/bin/sh", NULL}, envp)`；不再使用旧的 `-c "echo shell-smoke..."`
- `read(0, ...)` 已从默认 TTY 阻塞读取并做最小回显，`kernel_bootstrap_tty()` 只轮询键盘并 halt，不再消耗/抢走用户输入
- QEMU serial + monitor smoke 已验证真实 Alpine `/bin/sh` 能启动到交互提示符 `/ #`；monitor 注入按键后 shell 会收到并执行输入
- 已补最小用户态进程生命周期：`fork(2)`、受限 `vfork/clone`、用户态 `execve(2)`、直接子进程 `wait4(2)`、fd 继承与 `FD_CLOEXEC` 清理
- 已验证 shell 内建和简单外部命令可连续执行并返回 prompt：`echo hi`、`/bin/busybox echo ext`、`ls /`、`pwd`
- 已验证 GNU coreutils smoke：`printf`、`cat`、`wc`、`stat`、`realpath`、`sha256sum`、`sort`、`tr`、`truncate` 在当前 rootfs 上可执行并返回 `COREUTILS_DONE`
- 已验证 bash/gcc smoke：`/bin/bash --version`、`/bin/bash -c` 变量/重定向、`/usr/bin/gcc /tmp/hello.c -o /tmp/hello`、`/tmp/hello` 可执行并输出 `hello-gcc` / `BASH_GCC_DONE`
- 已补最小控制终端进程组语义：`setpgid/getpgid/getpgrp/setsid` 维护 task pgrp，`TIOCGPGRP/TIOCSPGRP` 使用 Linux `pid_t *` 宽度，默认 TTY 有 foreground pgrp gate，后台 shell 不再和前台子进程抢 stdin
- 已验证 `ash` 可由 `/bin/sh` 启动；`dd bs=1 count=3` 启动后会阻塞等待 stdin，输入 3 字节后退出并恢复 shell prompt
- 仍未实现完整 PTY/job control；当前有最小 `/dev/ptmx`、`/dev/pts/N` 和 Ctrl+C -> SIGINT 默认退出，但不支持完整 session、SIGTTIN/SIGTTOU、异步 signal handler 递送或真实 line discipline
- 当前 fork/COW 路径够支撑简单外部命令；fork child exec/exit 的 COW 引用释放仍偏保守，后续需要整理成真正的页表销毁引用回收语义

现在已经具备：

- Limine `x86_64` 入口
- HHDM / memmap / framebuffer / MP / RSDP 请求
- early physical allocator
- 基本分页抽象与运行时单页映射
- SLUB kernel heap / `kmalloc` / `kfree`
- 串口 + flanterm framebuffer 终端
- PS/2 键盘输入
- 多实例 TTY
- ACPI MADT 解析
- APIC / x2APIC 初始化
- IDT + TSC-deadline one-shot timer / reschedule IPI / spurious 中断
- 基于 Limine MP 的 SMP bring-up
- 基于 `GS_BASE` 的 percpu
- SSE / FXSR 初始化与 FXSAVE/FXRSTOR 任务状态
- MUQSS 风格 per-CPU deadline runqueue 调度器
<!-- 过时内容（2026-06-14 已替换为 MUQSS 风格 per-CPU deadline runqueue）：- tickless CFS/RT/root-domain 负载均衡调度骨架 -->
- 可创建内核任务的最小上下文切换入口
<!-- 过时内容：默认 kernel task smoke 已删除。 -->
- ELF64 loader：`PT_LOAD` / `PT_INTERP` / 动态链接器入口 / Linux 初始用户栈布局
- 最小 Linux 用户态执行链路：独立用户 CR3、`execve` 启动、首次 ring3 `iretq`、x86_64 `syscall` 入口 / `sysretq` 返回和一批 Alpine/musl 启动所需基础 syscall
- 最小 shell 进程链路：父子 task 关系、`fork/vfork/clone` 受限子进程创建、用户态 `execve` 替换镜像、`wait4` 回收直接子进程、fd 继承和 close-on-exec
- Alpine tar.zst module rootfs；启动期首个用户进程直接 exec `/bin/sh`
- 薄的 blockdev 抽象
- 极小 VFS
- tar.zst module rootfs + ramfs
- 最小 devtmpfs
- ACPI MCFG + PCIe ECAM 设备枚举
- NVMe polling read 驱动；首个 namespace 通过 `blockdev` 抽象和 `/dev/nvme0n1` 设备节点暴露
<!-- 过时内容（2026-06-09 已删除 AHCI/legacy PCI/FatFs/diskio）：
- FatFs 代码和历史路径仍在；当前默认 Makefile 不编译 `src/fat.c` / `src/diskio.c`，也不把 FAT 作为 rootfs 路径
- PCI 存储控制器枚举
- AHCI BAR 映射 / SATA 端口识别 / IDENTIFY / 多 sector 只读 I/O
- AHCI 盘作为 `blockdev` 和 `/dev/sd0` 设备节点
-->

也就是说，`kernel64/` 已经不只是能打印日志和起多核，而是已经把“Limine module tar.zst -> ramfs -> VFS -> 文件读取”这一条默认 rootfs 链路打通，并且已经能通过 ACPI MCFG 枚举 PCIe 设备、识别 QEMU NVMe 控制器并从 `/dev/nvme0n1` 做只读 sector smoke。在此基础上，ELF64 loader 已能把用户镜像加载进独立用户地址空间，构造 Linux 风格初始栈，并通过 task 调度链路 `iretq` 进入 ring3；最小 `syscall` ABI 已通过默认生成的静态 ELF64 测试程序验证 `write`、`sched_yield`、`brk`、匿名 `mmap`、file-backed `mmap`、file-backed 只读页缓存、COW 私有写、`mprotect`、`munmap` 和 `exit`。

## 用户要求与当前策略

用户明确要求：

- 把 OS 重构成 64 位
- 改动尽量精简、抽象清晰，不堆无意义自由函数和成员变量
- 需要有抽象层，例如 VFS / blockdev
- 早期要求文件系统优先可用 FatFs；当前 `kernel64` 默认构建已转向 Alpine tar.zst/ramfs，FatFs 已作为旧路径删除
- 需要继续做 TTY、PCIe/NVMe、devtmpfs（早期文档中称 devfs）
<!-- 过时内容（2026-06-09 已删除 AHCI/IDE 方向）：
- 需要继续做 TTY、PCI/PCIe、AHCI、IDE、devtmpfs（早期文档中称 devfs）
-->
- 调试/运行时需要申请沙箱外执行；纯读代码和改代码不需要

当前采用的策略是：

- 保留现有 32 位主线，不把旧路径强行硬改成 64 位
- 独立推进 `kernel64/`
- 优先做薄抽象，不照搬 32 位历史大结构
- 先把“能读文件”的链路打通，再考虑真实磁盘驱动
- `main.c` 继续保持 orchestration-only

## 已完成工作

### 1. 32 位主线清理

这部分已经完成，目标是去掉旧 BIOS boot sector 语义对系统盘选择的耦合，为后续更干净的启动路径清场。

涉及文件：

- `kernel/dos/task/basetask.c`
- `kernel/dos/task/mtask.c`
- `kernel/drivers/vdisk.c`
- `kernel/fs/vfs.c`
- `kernel/include/dos.h`
- `kernel/include/drivers.h`
- `kernel/include/fs.h`

核心变化：

- 去掉了从 `0x7c00` BPB / 驱动号推导默认盘的路径
- 默认盘状态只保留 `default_drive`
- 新任务 `drive_number` 由 `default_drive - 'A'` 推导
- 增加 `first_vdisk()` / `next_vdisk()` 统一枚举磁盘
- 增加 `vfs_mount_all_disks()` 统一挂载
- `basetask.c` 改成扫描 `init.bin` / `psh.bin` / `sys.cfg` 所在盘作为系统盘

### 2. `kernel64/` 独立 64 位内核路径

新增目录：

- `kernel64/`

当前主要文件结构：

- `kernel64/Makefile`
- `kernel64/linker.ld`
- `kernel64/limine.conf`
- `kernel64/include/*.h`
- `kernel64/src/*.c`
- `kernel64/src/interrupts_asm.S`
- `kernel64/thirdparty/flanterm/`
- `kernel64/thirdparty/zstd/`
<!-- 过时内容（2026-06-09 已删除 FatFs 历史路径）：
- `kernel64/thirdparty/fatfs/`（历史路径，当前默认 Makefile 不编译）
-->

### 3. boot / memory 抽象

已完成组件：

- `kernel64/include/bootinfo.h`
- `kernel64/src/bootinfo.c`
- `kernel64/include/bootmem.h`
- `kernel64/src/bootmem.c`
- `kernel64/include/frame.h`
- `kernel64/src/frame.c`
- `kernel64/include/paging.h`
- `kernel64/src/paging.c`
- `kernel64/include/kheap.h`
- `kernel64/src/kheap.c`

当前能力：

- 从 Limine 汇总 `bootinfo`
- 从 memmap 做 early physical allocator
- 提供 buddy frame allocator：order 0..10，每 CPU order-0 pool，metadata 按 Limine memmap 动态 sizing，运行期单页/连续页分配走 `frame_*`
- 使用现有 CR3 做页表查询
- 支持运行时单页映射 `paging_map_page()`
- 提供带每 CPU 小对象 cache、虚拟 range 复用和 page-aligned `kmemalign` 的 SLUB kernel heap；对外 API 包含 `kheap_init/kheap_base/kheap_used_bytes/kheap_mapped_bytes`、`kmalloc/kzalloc/kmemalign/krealloc/kfree`

### 4. console / input / tty 抽象

已完成组件：

- `kernel64/include/console.h`
- `kernel64/src/console.c`
- `kernel64/include/input.h`
- `kernel64/src/input.c`
- `kernel64/include/tty.h`
- `kernel64/src/tty.c`
- `kernel64/src/kernel.c`

当前能力：

- 串口输出内核日志
- framebuffer 上通过 flanterm 输出活动 TTY/shell
<!-- 过时内容（2026-06-12 已删除 framebuffer kernel log/early log 回放路径）：
- framebuffer 上通过 flanterm 输出
- framebuffer 初始化前缓存 early log，初始化后回放
-->
- 统一处理 `\n -> \r\n`
- PS/2 键盘 polling 输入，支持 Ctrl+字母和方向键 ANSI escape 序列
- 4 个 TTY 实例
- `F1` 到 `F4` 切换活动 TTY
- 用户态 stdin 通过默认 TTY 读取并做最小回显

注意：

- 当前 `kernel_bootstrap_tty()` 只负责持续 `tty_poll()` 并 `hlt`，避免抢走 `/bin/sh` 的 stdin
- 最终目标仍然是用户态 `apps/psh`

### 5. arch / ACPI / APIC / 中断 / SMP / percpu / task 分层

已完成组件：

- `kernel64/include/arch.h`
- `kernel64/src/arch.c`
- `kernel64/include/acpi.h`
- `kernel64/src/acpi.c`
- `kernel64/include/apic.h`
- `kernel64/src/apic.c`
- `kernel64/include/interrupts.h`
- `kernel64/src/interrupts.c`
- `kernel64/src/interrupts_asm.S`
- `kernel64/include/smp.h`
- `kernel64/src/smp.c`
- `kernel64/include/percpu.h`
- `kernel64/src/percpu.c`
- `kernel64/include/task.h`
- `kernel64/src/task.c`

当前能力：

- `arch`：CPUID / APIC / TSC / TSC-deadline / SSE / FXSR / NX 能力探测，开启 CR0/CR4 的 SSE/FXSR 支持和 EFER.NXE，建立 per-CPU GDT/TSS，配置 `SYSCALL/SYSRET` 相关 MSR（入口使用 `syscall`，正常返回路径使用 `sysretq`），`rdtsc`、TSC 频率读取、`FS_BASE` / `GS_BASE` / `KERNEL_GS_BASE` 访问、IRQ save-restore / `pause` / `hlt`
- `acpi`：RSDP 校验、RSDT/XSDT 选择、MADT 解析、CPU / IOAPIC / LAPIC 基址提取
- `apic`：本地 APIC 初始化，优先走 `x2APIC`，BSP/AP 都会配置 TSC-deadline one-shot timer；不接 HPET，不做 periodic 回退
- `interrupts`：最小 IDT，timer / reschedule / spurious 向量，timer 和 reschedule 入口会把完整中断栈帧交给 task 层，由 task 层选择 next 后返回保存槽和 next context
<!-- 过时内容（2026-06-14 syscall 热路径已删除非 canonical fallback，iretq 只保留为独立慢出口）：
- `interrupts_asm.S`：timer / reschedule / spurious 中断会先按统一中断帧完整保存 64 位通用寄存器；若从 ring3 进入，会先 `swapgs` 进入内核 GS/percpu 上下文。调度切换已经拆成轻量 `task_switch_to()`，只保存/恢复 `rbx/rbp/r12-r15` 和 `rsp`；从中断切换时先把完整中断帧包装成一个返回到 `task_iret_resume` 的小 context，再走同一个 callee-saved restore。`syscall_entry_stub` 使用 `syscall` 指令入口和 `swapgs`，按 Linux x86_64 syscall ABI 接入当前 task 的内核栈；正常返回从当前 task 栈上的统一帧取 `RIP/RFLAGS/RSP` 并用 `sysretq` 回 ring3，非 canonical `RIP` 走保守 `iretq` fallback
-->
- `interrupts_asm.S`：timer / reschedule / spurious 中断会先按统一中断帧完整保存 64 位通用寄存器；若从 ring3 进入，会先 `swapgs` 进入内核 GS/percpu 上下文。调度切换已经拆成轻量 `task_switch_to()`，只保存/恢复 `rbx/rbp/r12-r15` 和 `rsp`；从中断切换时先把完整中断帧包装成一个返回到 `task_iret_resume` 的小 context，再走同一个 callee-saved restore。`syscall_entry_stub` 使用 `syscall` 指令入口和 `swapgs`，按 Linux x86_64 syscall ABI 接入当前 task 的内核栈，IF/DF/TF 由 `IA32_FMASK` 自动清除；普通 syscall 返回从统一 frame 取 `RIP/RFLAGS/RSP` 后直接 `swapgs; sysretq`，`rt_sigreturn` 等需要完整恢复 `rcx/r11` 的路径通过独立 `syscall_iret_return()` 慢出口走 `iretq`
- `smp`：通过 Limine MP `goto_address` 启动 AP，并在 AP 上完成 SSE / IDT / LAPIC / TSC-deadline timer 初始化
- `percpu`：基于内核态 `GS_BASE` 绑定当前 CPU 状态；用户态入口/返回通过 `swapgs` 在用户 GS 与内核 percpu GS 之间切换
- `task`：每 CPU 一个 bootstrap task；调度器为 MUQSS 风格 per-CPU deadline runqueue，READY task 按 deadline/priority 进入动态分配的 ordered queue，删除旧 CFS vruntime、RT priority 和 root-domain 负载均衡接口；提供 `task_spawn_kernel()` 创建普通内核任务，也提供 `task_spawn_kernel_on_cpu()` 用于固定到指定在线 CPU；`task_spawn_user()` 创建用户任务并构造 ring3 `iretq` 帧；调度切换会同步 task CR3、FS base、FPU 状态和本 CPU TSS.RSP0/syscall 栈；无 runnable 竞争时关闭本 CPU deadline，创建/唤醒任务时通过 deadline 或 reschedule IPI 唤醒调度；合成的新任务 `iretq` 帧已补齐 `RIP/CS/RFLAGS/RSP/SS`；每个 task 预留 16 字节对齐 FXSAVE 状态；用户 task 保存最小 `exec_path`、`fsbase`、`clear_child_tid`；`sched_yield` syscall 已接入 `task_yield_current()`，直接复用轻量 `task_switch_to()`，避免走完整中断帧切换
<!-- 过时内容（2026-06-14 已替换为 MUQSS 风格 per-CPU deadline runqueue，旧 CFS/RT API 已删除）：
- `task`：每 CPU 一个 bootstrap task；已有基于 TSC cycles 的 CFS vruntime、RT priority、root-domain 负载均衡骨架；提供 `task_spawn_kernel()` 创建内核任务，也提供 `task_spawn_kernel_on_cpu()` 用于固定到指定在线 CPU；新增 `task_spawn_user()` 创建用户任务并构造 ring3 `iretq` 帧；调度切换会同步 task CR3、FS base、FPU 状态和本 CPU TSS.RSP0/syscall 栈；无竞争时关闭本 CPU deadline，创建/迁移/阻塞任务时通过 deadline 或 reschedule IPI 唤醒调度；合成的新任务 `iretq` 帧已补齐 `RIP/CS/RFLAGS/RSP/SS`；每个 task 预留 16 字节对齐 FXSAVE 状态；用户 task 保存最小 `exec_path`、`fsbase`、`clear_child_tid`；`sched_yield` syscall 已接入 `task_yield_current()`，直接复用轻量 `task_switch_to()`，避免走完整中断帧切换
-->
<!-- 过时内容：默认 kernel task smoke 和 parallel-sum demo 已删除。 -->
- `kernel`：parallel-sum demo 和默认 smoke kernel task 已删除；当前只保留启动后的 `kernel_bootstrap_tty()` 轮询入口

### 5.1 首个用户进程 smoke

默认 bring-up 用例现在直接启动 `/bin/sh`：

<!-- 过时内容（历史静态 FAT smoke rootfs 和 user/test_init.S 已删除）：
- 历史静态 FAT smoke rootfs 中，`/bin/sh` 和 `/bin/busybox` 都是 `kernel64/user/test_init.S` 生成的静态 smoke ELF；当前 `make -C kernel64 img` 不把它作为 rootfs 打包
-->
- Alpine tar.zst rootfs 中，`/bin/sh -> /bin/busybox`，通过动态链接器 `/lib/ld-musl-x86_64.so.1` 启动真实 busybox shell
- `process_spawn_first_user()` 传入 argv：`/bin/sh`

Alpine 路径当前期望输出包含：

```text
kernel64: rootfs tar.zst module[0]=/boot/rootfs.tar.zst ...
kernel64: execve /bin/sh ...
kernel64: execve interp=/lib/ld-musl-x86_64.so.1 ...
/bin/sh: can't access tty; job control turned off
/ #
```

<!-- 过时内容（parallel-sum demo 和默认 RT/CFS kernel task smoke 已删除）：
历史 parallel-sum demo 与默认 RT/CFS kernel task smoke 记录；对应实现和默认启动路径均已删除。
-->

验证命令：

```sh
make -C kernel64 img build/OVMF_VARS.fd
timeout 12s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-no-delay.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
```

### 6. module / blockdev / VFS / rootfs

已完成组件：

- `kernel64/include/limine.h`
- `kernel64/include/blockdev.h`
- `kernel64/src/blockdev.c`
- `kernel64/include/vfs.h`
- `kernel64/src/vfs.c`
- `kernel64/src/storage.c`
<!-- 过时内容（2026-06-09 已删除 FatFs/diskio）：
- `kernel64/include/fat.h`
- `kernel64/src/fat.c`
- `kernel64/src/diskio.c`
- `kernel64/thirdparty/fatfs/ff.c`
- `kernel64/thirdparty/fatfs/ff.h`
- `kernel64/thirdparty/fatfs/ffconf.h`
- `kernel64/thirdparty/fatfs/diskio.h`
-->

当前能力：

- Limine module request 已接入
- `bootinfo` 已携带 module 列表与数量
- 默认 rootfs 从 Limine module `boot():/boot/rootfs.tar.zst` 解压并挂载为 ramfs
- VFS 保持极小，只做当前需要的 root mount 和路径分发
- `/dev` 走单独挂载分流
- `kernel64/Makefile` 默认把 `kernel64/build/alpine-rootfs.tar.zst` 作为 `ROOTFS_TAR_ZST` 打进 UEFI 镜像；当前不支持 `ROOTFS_IMG` 或 FAT fallback 构建变量
<!-- 过时内容（2026-06-09 已删除 AHCI/FatFs fallback 路径）：
- AHCI blockdev 仍可作为设备路径继续推进，但不是当前默认 rootfs 构建路径
- FatFs/diskio 历史路径曾通过极薄 shim 接到 `blockdev`；当前默认 Makefile 不编译 `src/fat.c` / `src/diskio.c`
- 历史上已验证能够读取 FAT 镜像中的文件

历史 FatFs 配置：

- 使用官方 `ff16.zip` 对应的 FatFs R0.16
- 去掉 exFAT
- 支持 LFN，配置为 `FF_CODE_PAGE=437`、`FF_USE_LFN=2`、`FF_MAX_LFN=255`、`FF_LFN_UNICODE=2`
- `ffunicode.c` 只保留 CP437/UTF-8 路径需要的 OEM/Unicode 转换和大小写转换
- `FF_FS_READONLY=0`，当前仅通过 VFS 暴露覆盖已有文件范围的薄写路径
- `FF_FS_TINY=1`
-->

设计约束：

- 不移植 32 位旧 VFS 大接口
- 不在 VFS 层提前做大而全语义
- 目前重点是“可靠读文件”，不是“完整 POSIX”

### 7. devtmpfs / tty 设备 / PCIe / NVMe

已完成组件：

- `kernel64/include/devtmpfs.h`
- `kernel64/src/devtmpfs.c`
- `kernel64/include/pty.h`
- `kernel64/src/pty.c`
- `kernel64/include/pcie.h`
- `kernel64/src/pcie.c`
- `kernel64/include/nvme.h`
- `kernel64/src/nvme.c`
<!-- 过时内容（2026-06-09 已删除 legacy PCI/AHCI）：
- `kernel64/include/pci.h`
- `kernel64/src/pci.c`
- `kernel64/include/ahci.h`
- `kernel64/src/ahci.c`
-->

当前能力：

- 旧 devfs 已替换为最小 devtmpfs，`/proc/mounts` 显示 `devtmpfs /dev devtmpfs rw,nosuid 0 0`
- devtmpfs 设备节点表按需动态增长，不再有固定 16 项节点上限
<!-- 过时内容（2026-06-09 已删除固定 devtmpfs 节点 cap）：
- devtmpfs 设备节点表固定为 16 项。
-->
- 当前已有：
  - `/dev/null`
  - `/dev/zero`
  - `/dev/tty`
  - `/dev/tty0`
  - `/dev/tty1`
  - `/dev/tty2`
  - `/dev/tty3`
  - `/dev/ptmx`
  - `/dev/pts`
  - `/dev/pts/N`（动态 PTY slave）
  - `/dev/nvme0n1`（当 NVMe namespace 1 识别成功时）
- `/dev/ptmx` 可分配 PTY master，`/dev/pts/N` 可打开 slave；master/slave 双向缓冲读写、最小 termios/winsize/pgrp ioctl 和 readiness 已接入
- ACPI MCFG 解析已接入，PCIe config space 通过 ECAM 枚举
- PCIe BAR 读取和 MMIO/bus-master enable 已提供给驱动使用
- 能按 class / subclass / prog_if 输出 PCIe 设备类别信息
- NVMe 能解析 BAR0、关闭/启用 controller、建立 admin/io queue、identify controller/namespace
- NVMe 能执行单队列 polling read，并通过 `/dev/nvme0n1` 暴露首个 namespace 的只读 sector 读取
<!-- 过时内容（2026-06-09 已删除 AHCI/IDE/legacy PCI）：
- AHCI 能解析 BAR5 / ABAR，建立高半区 MMIO 映射
- AHCI 能枚举 implemented ports，识别 SATA 签名
- AHCI 能执行 ATA IDENTIFY，读取 sector count
- AHCI 能执行单槽轮询的多 sector 只读 DMA I/O，并把盘包装成 `blockdev`
- AHCI read 路径有更明确的 busy / taskfile / timeout 日志，并会 probe sector 0 签名
- AHCI 盘通过 devtmpfs 暴露为 `/dev/sdN`
-->

说明：

- rootfs 来自 Limine module 中的 `/boot/rootfs.tar.zst`，解压后挂载为 ramfs；NVMe 当前作为独立块设备 smoke 路径，不作为默认 rootfs fallback
- 当前 NVMe 是单 controller/namespace 探索路径、单 io queue pair、轮询、只读实现；不支持 write/flush/interrupt/multi-namespace 完整管理
- NVMe failed probe 会释放已分配 queue/identify/DMA frame 和 controller heap 对象；当前 paging/MMIO window 只有映射路径和单页 unmap API，没有 MMIO range 回收 allocator，失败探测留下的 MMIO virtual window 暂不回收，后续做统一 MMIO mapping 管理时再收敛
<!-- 过时内容（2026-06-09 已删除 AHCI/IDE 路线）：
- 当前 AHCI 仍是单命令槽、轮询、只读实现
- rootfs 来自 Limine module 中的 `/boot/rootfs.tar.zst`，解压后挂载为 ramfs；AHCI 暂不作为当前默认 rootfs 构建路径
- IDE / NVMe 还没有真正驱动实现
-->

### 8. ELF64 loader / 用户镜像加载

已完成组件：

- `kernel64/include/elf64_loader.h`
- `kernel64/src/elf64_loader.c`
- `kernel64/include/paging.h`
- `kernel64/src/paging.c`
- `kernel64/include/arch.h`
- `kernel64/src/arch.c`
- `kernel64/include/process.h`
- `kernel64/src/process.c`
- `kernel64/include/syscall.h`
- `kernel64/src/syscall.c`

当前能力：

- 校验 x86_64 little-endian ELF64，支持 `ET_EXEC` 和 `ET_DYN`
- 支持 `PT_LOAD`：按段权限映射用户页，设置 `USER` / `WRITE` / `NX`，拷贝 file image，并清零 BSS
- 支持 `PT_INTERP`：读取解释器路径并加载动态链接器，最终入口返回解释器入口
- 支持 `PT_PHDR` 和从文件偏移反推 `AT_PHDR`
- 支持 Linux 初始用户栈布局：`argc/argv/envp/auxv`
- 当前 auxv 包含 `AT_PHDR`、`AT_PHENT`、`AT_PHNUM`、`AT_PAGESZ`、`AT_BASE`、`AT_FLAGS`、`AT_ENTRY`、UID/GID、`AT_CLKTCK`、`AT_SECURE`、`AT_RANDOM`、`AT_EXECFN`
- `arch` 已探测并启用 NXE，非执行用户页会使用 `PAGING_FLAG_NO_EXECUTE`
- `paging` 增加 `paging_create_user_space()`、`paging_lookup_in()`、`paging_map_page_in()`、`paging_unmap_page_in()`、`paging_remap_page_in()`、`paging_set_page_flags_in()`、`paging_destroy_user_space()`；用户地址空间复用内核高半区 PML4，低半区独立
- `frame` buddy allocator 负责运行期物理页分配/释放；`bootmem` 只保留 HHDM translation、early metadata allocation 和 reserved boundary；用户页通过 `user_memory` 引用计数跟踪，`munmap`、COW 替换和用户任务退出会归还物理页
<!-- 过时内容（frame buddy 已取代 bootmem 单页 free list）：
- `bootmem` 增加单页 free list；用户页通过 `user_memory` 引用计数跟踪，`munmap`、COW 替换和用户任务退出会归还物理页
-->
- `interrupts` 已接入 #PF gate，page fault 入口正确处理带错误码异常栈和 ring3 `swapgs`；用户态缺页交给 `user_memory_handle_page_fault()`，无法处理时终止当前用户任务
<!-- 过时内容（2026-06-13 user_memory 已改为 task-owned table + 动态 VMA vector，固定 VMA 表上限已删除）：- `user_memory` 使用固定上限的轻量 VMA 表管理 `brk`、匿名 `mmap` 和 file-backed `mmap`；匿名/file-backed 页按需 fault-in；file-backed 页已接入只读页缓存，同一路径+文件页 offset 可跨映射复用物理页；MAP_PRIVATE 可写 file-backed 页先映射只读+COW，首次写入复制私有页 -->
- `user_memory` 使用 task-owned 动态 VMA vector 管理 `brk`、匿名 `mmap` 和 file-backed `mmap`，保留线性查找但不再有固定 VMA 槽上限；匿名/file-backed 页按需 fault-in；file-backed 页已接入只读页缓存，同一路径+文件页 offset 可跨映射复用物理页；MAP_PRIVATE 可写 file-backed 页先映射只读+COW，首次写入复制私有页
- `user_file` 提供最小 per-task fd 表，`0/1/2` 会初始化为可复制的标准 fd entry，普通 VFS 文件和目录默认从 fd 3 开始；当前用于 `open/openat/read/write/close/lseek/pread64/fstat/getdents64/fcntl/dup*`、`O_CREAT/O_TRUNC/O_APPEND/O_EXCL`、`ftruncate` 和 file-backed `mmap`，支持最小 status flags / `FD_CLOEXEC`
- `elf64_load_in()` 支持把 ELF64 明确加载进指定用户 CR3，不再依赖当前 CR3
- `process_execve()` 会创建独立用户地址空间，调用 ELF64 loader，并通过 `task_spawn_user()` 建立用户任务
- `main.c` 在 task 初始化后调用 `process_spawn_first_user()`，主路径直接尝试 `/bin/sh`
- 最小 syscall ABI 已接入 Linux x86_64 调用约定，当前支持 `read`、`write`、`open`、`openat`、`creat`、`close`、`lseek`、`pread64`、浅 `sendfile`、`truncate/ftruncate`、`mkdir/mkdirat`、`rmdir`、`unlink/unlinkat`、`rename/renameat/renameat2(flags=0)`、`exit`、`exit_group`、`fork`、受限 `vfork/clone`、用户态 `execve`、直接子进程/进程组 `wait4`（浅支持 `WNOHANG/WUNTRACED/WCONTINUED`）、`getpid`、`gettid`、`getppid`、`getuid/geteuid/getgid/getegid`、`sched_yield`、`nanosleep` 浅实现、`brk`、匿名/file-backed `mmap`、`munmap`、`mprotect`、浅 `madvise`、`rt_sigaction`、`rt_sigprocmask`、`rt_sigreturn`、`arch_prctl(SET_FS/GET_FS)`、`set_tid_address`、`fstat/newfstatat/stat/lstat/statx`、`access/faccessat`、`readlink/readlinkat`、`getcwd/chdir/fchdir`、`uname`、`clock_gettime`、`gettimeofday`、`getrandom`、浅 `futex`、`fcntl`、`dup/dup2/dup3`、浅 `prctl(PR_SET_NAME/PR_GET_NAME/PR_SET_DUMPABLE/PR_GET_DUMPABLE)`、`ioctl(TIOCGWINSZ/TIOCSWINSZ/TIOCGPGRP/TIOCSPGRP/TIOCGPTN/TIOCSPTLCK/TCGETS/TCSETS*)`、`poll/ppoll`、浅 `epoll_create/epoll_create1/epoll_ctl/epoll_wait`、`getdents64`、浅 `kill(sig=0/SIGINT/SIGTERM/SIGKILL)/getpgid/getpgrp/setpgid/setsid`；未知 syscall 返回 `-ENOSYS`

设计约束与当前限制：

- `ET_DYN` 使用固定 load bias，暂不做 ASLR
- 当前 `execve` 仍是最小替换链路，不具备 credentials、完整 argv/env 限制错误码或失败回滚；成功运行后的用户地址空间会在 `exit` 时释放；cwd 为 per-task 状态，fork/vfork 继承，exec 不改变
<!-- 过时内容（2026-06-13 fd table 已改为 task-owned 动态表）：- 当前 fd 表仍是固定 32 项 per-task 表；标准 fd 已可复制并可被 `dup2` 覆盖，目录 fd、`dup/fcntl/CLOEXEC`、fd 继承、close-on-exec 扫描、普通文件 `write(2)`、devtmpfs 设备 fd 和 PTY fd 已有最小实现；`poll/ppoll` 与浅 `epoll` 已走 fd readiness，但完整阻塞/非阻塞唤醒、公平等待和边沿触发语义仍缺 -->
- 当前 fd table 已是 task-owned 动态表，初始容量 32，并会在 `open/dup/dup2/ref_install` 等路径按需扩容；fork/exec/dup/SCM_RIGHTS 的 fd 生命周期已有最小支持。`poll/ppoll` 与浅 `epoll` 已走 fd readiness，但完整阻塞/非阻塞唤醒、公平等待和边沿触发语义仍缺。
- 默认 module-backed `ramfs` rootfs 已具备内存态可写 VFS：普通文件支持 create/truncate/extend/append/overwrite，目录支持 mkdir/unlink/rmdir/rename；这是运行期内存语义，不会持久写回 NVMe 设备或 rootfs tar.zst
- 当前 `fork` 通过页表 clone + COW 支撑简单 BusyBox 外部命令；fork child `exec/exit` 暂时避免释放共享 COW 页，能避免父 shell 被误释放页破坏，但会留下引用回收技术债
- 当前 TTY/job-control 只实现浅 controlling tty foreground pgrp 和最小 PTY master/slave：`read(0)` 会阻止非前台 pgrp 消耗输入，`wait4` 等待前台进程组时会按当前 task controlling TTY/PTY 临时交出/恢复 foreground；Ctrl+C 会向 foreground pgrp 发 SIGINT 并按默认动作退出目标任务；没有真实 session 引用计数、完整 SIGTTIN/SIGTTOU、异步 signal handler 递送或终端 line discipline
- page fault 当前处理用户态 VMA demand paging / COW；无法处理的用户态同步 #PF 会优先递送最小 SIGSEGV，未安装 handler、被 mask 或构造 frame 失败时仍终止当前用户任务
- file-backed `MAP_SHARED|PROT_WRITE` 已支持 module rootfs 上的覆盖式写回，flush 点是 `munmap`、任务退出和 `MAP_FIXED` 覆盖；普通 `write(2)` 后没有页缓存失效协议，同一文件已 mmap 的页可能继续看到旧缓存；NVMe 当前只读，仍无写回/flush
- file-backed 页缓存目前是固定 128 页的小缓存，暂不做淘汰、页缓存统计或跨文件系统一致性协议；shared writable 写回只刷新硬件 dirty 页，不做 msync 或持久块设备 flush
<!-- 过时内容（2026-06-13 signal table 已改为 task-owned heap object，不再是静态 task-keyed 表）：- signal 当前只有静态 per-task action/mask 表、同步 SIGSEGV 递送、最小 `siginfo_t` 和 `rt_sigreturn`，以及 SIGINT/SIGTERM/SIGKILL 默认终止目标任务的浅 `kill`/Ctrl+C 路径；`rt_sigreturn` 会强制用户 `CS/SS`、净化 `RFLAGS` 并校验用户 `RIP/RSP`；没有异步队列、tgkill、线程组、altstack、pending signal、signalfd 或通用 async handler 递送 -->
- signal 当前只有 task-owned action/mask/pending 表、同步 SIGSEGV 递送、最小 `siginfo_t` 和 `rt_sigreturn`，以及 SIGINT/SIGTERM/SIGKILL 默认终止目标任务的浅 `kill`/Ctrl+C 路径；`rt_sigreturn` 会强制用户 `CS/SS`、净化 `RFLAGS` 并校验用户 `RIP/RSP`，坏 sigframe 会递送 SIGSEGV，递送不了则以 139 退出；没有异步队列、tgkill、线程组、altstack、signalfd 或通用 async handler 递送
- 当前为 musl/Alpine 启动和简单外部命令补齐了一批基础 ABI，并已用真实 Alpine tar.zst rootfs 验证 `/bin/sh` 可启动到交互提示符，且 `echo hi`、`/bin/busybox echo ext`、`ls /`、`pwd`、`cd`、`ls /proc`、`readlink /proc/self/exe`、`cat /proc/{mounts,meminfo,cpuinfo}`、`echo/printf > file`、`>>`、`mkdir/rm/rmdir/mv` 可执行；真实 tmpfs、完整 `/proc/self/fd/N`、`/dev/shm` 嵌套挂载、完整 PTY/session/job-control、真实线程/futex/signal 语义、权限模型和 AF_UNIX socket 的完整多消息/credentials 语义仍缺
<!-- 过时内容（user-test-rootfs target 已删除）：
- `user-test-rootfs` 仍可生成历史静态 FAT smoke 镜像，但当前 `make -C kernel64 img` 不会把它打进 UEFI 镜像，也不支持 `ROOTFS_IMG` 覆盖
-->

## 当前启动流程

`kernel64/src/main.c` 现在仍然是 orchestration-only，顺序大致如下：

1. `console_init()`
2. `bootinfo_init()`
3. `bootmem_init()`
4. `paging_init()`
5. `frame_init()`
6. `kheap_init()`
7. `tty_init()`
8. `devtmpfs_init()` / `devtmpfs_mount()` / `vfs_mount_dev()`
9. `procfs_mount()` / `vfs_mount_proc()`，挂载只读最小 `/proc`
10. `console_init_framebuffer()`
11. `arch_init()`，同时开启 BSP SSE/FXSR/NX
12. `acpi_init()`
13. `pcie_init()`
14. `nvme_init()`
15. `storage_init()`，挂载 Limine module tar.zst rootfs
<!-- 过时内容（2026-06-09 已删除 legacy PCI/AHCI）：
12. `pci_init()`
13. `ahci_init()`
14. `storage_init()`，优先挂载 Limine module rootfs，失败后尝试 AHCI blockdev
-->
16. 重新列出 `/dev`，确认动态设备节点
17. `apic_init()`
18. `interrupts_init()`
19. `smp_init()`
20. `percpu_init()`
21. BSP `percpu_bind_current(0)`
22. `task_init()`，建立 bootstrap tasks、per-task kernel stack、TSS.RSP0/syscall 栈
23. `syscall_init()`
24. `process_spawn_first_user()`，尝试 `execve` 候选 ELF64 用户镜像
25. `smp_start_aps()`，AP 完成 SSE / GDT/TSS / syscall MSR / IDT / LAPIC / TSC-deadline timer 初始化
26. BSP `apic_timer_init()`
27. BSP `task_timer_kick()`
28. `interrupts_enable()`
29. 输出状态日志
30. 进入 `kernel_bootstrap_tty()`

后续继续扩展时，应尽量保持 `main.c` 只做子系统装配，不把实现细节塞回这里。

## 已验证结果

### 编译

已验证：

```bash
make -C kernel64 all
```

可以通过。

### 运行

Makefile 现在区分 `img` 和 `run`：`img` 只生成 `kernel64/build/kernel64-uefi.img`，`run` 会先生成镜像再用正常 QEMU 图形窗口启动；图形窗口显示活动 TTY/shell，内核日志只在串口输出。手动运行 QEMU 时可让 Makefile 生成 `OVMF_VARS.fd`：
<!-- 过时内容（2026-06-12 framebuffer 不再显示内核日志）：
Makefile 现在区分 `img` 和 `run`：`img` 只生成 `kernel64/build/kernel64-uefi.img`，`run` 会先生成镜像再用正常 QEMU 图形窗口启动，内核 framebuffer 输出显示在 QEMU 窗口里。
-->

```bash
mkdir -p kernel64/build
cp /usr/share/edk2/x64/OVMF_VARS.4m.fd kernel64/build/OVMF_VARS.fd
```

已在沙箱外实跑验证：

```bash
make -C kernel64 run
```

最新一次用于避免 stdio 提前退出干扰的 smoke 验证命令：

```bash
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-default-after-gdt.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
```

当前默认 Alpine rootfs `/bin/sh` 验证命令：

```bash
scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-alpine-sh.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "rootfs tar.zst|execve|interp|can't access tty|/ #|syscall unhandled|page fault|exception|panic|exited status" /tmp/kernel64-alpine-sh.log
```

结果：默认 `kernel64/build/alpine-rootfs.tar.zst` 会作为 Limine module 打进 `/boot/rootfs.tar.zst`；日志要点为 `kernel64: rootfs tar.zst module[0]=/boot/rootfs.tar.zst ...`、`kernel64: execve /bin/sh ...`、`kernel64: execve interp=/lib/ld-musl-x86_64.so.1 ...`、`/bin/sh: can't access tty; job control turned off`、`/ #`。当前 stdin 已从默认 TTY 交互读取并做最小回显；这条 prompt-only 记录已被下面的外部命令 smoke 覆盖。

当前 Alpine `/bin/sh` 外部命令 smoke：

```bash
make -C kernel64 img build/OVMF_VARS.fd
timeout 55s qemu-system-x86_64 -machine q35 -accel kvm -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-proc.log -monitor unix:/tmp/kernel64-proc.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
```

通过 QEMU monitor 注入并验证：

```text
/bin/busybox echo ext
ls /
pwd
```

结果：日志包含 `/ #`、`ext`、`ls /` 根目录列表、`pwd` 输出 `/`，并且每条命令后都返回 `/ #`。负向扫描没有 `syscall unhandled`、`page fault`、`exception`、`panic`、`can't fork`；`/bin/busybox echo ext` 与 `ls /` 子进程都以 status 0 退出。

本次 P1 收尾与 P2 最小 procfs 验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 55s qemu-system-x86_64 -machine q35 -accel kvm -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-p2-proc.log -monitor unix:/tmp/kernel64-p2-proc.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork" /tmp/kernel64-p2-proc.log
```

通过 QEMU monitor 注入并验证：

```text
ls /proc
readlink /proc/self/exe
cat /proc/mounts
cat /proc/meminfo
cat /proc/cpuinfo
```

结果：`make -C kernel64 all` 和 `make -C kernel64 img build/OVMF_VARS.fd` 均通过；QEMU 日志显示 `/proc` 列出 `cpuinfo meminfo mounts self stat`，`readlink /proc/self/exe` 输出当前进程路径 `/usr/bin/readlink`，`cat /proc/mounts` 当时输出 `rootfs`、`proc`、`devfs` 三行<!-- 过时内容：当前已改为 `devtmpfs` -->，`cat /proc/meminfo` / `cat /proc/cpuinfo` 输出静态最小内容；负向扫描没有 `syscall unhandled`、`page fault`、`exception`、`panic`、`failed`、`can't fork`。本轮同时补了 BusyBox `cat` 会触发的浅 `sendfile(2)`，只做缓冲读写兼容，不是完整零拷贝语义。

本次最小可写 VFS / ramfs rootfs 验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 55s qemu-system-x86_64 -machine q35 -accel kvm -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-vfs-write.log -monitor none -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|Read-only file system|Permission denied" /tmp/kernel64-vfs-write.log
```

QEMU smoke 用临时 `/bin/sh -c` argv 自动执行下面命令；验证后已恢复默认交互 `/bin/sh` argv，并重新执行 `make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`，所以最终镜像不保留自动 smoke argv：

```text
echo SMOKE_START
printf hi > /tmp/plos-write
cat /tmp/plos-write
echo more >> /tmp/plos-write
cat /tmp/plos-write
mkdir /tmp/plos-dir
echo x > /tmp/plos-dir/file
rm /tmp/plos-dir/file
rmdir /tmp/plos-dir
echo a > /tmp/plos-a
mv /tmp/plos-a /tmp/plos-b
cat /tmp/plos-b
rm /tmp/plos-b
cat /proc/mounts
echo SMOKE_DONE
```

结果：`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd` 均通过；QEMU 日志包含 `SMOKE_START` / `SMOKE_DONE`，`cat /tmp/plos-write` 先输出 `hi`，追加后输出 `himore`，`mv` 后 `cat /tmp/plos-b` 输出 `a`，`cat /proc/mounts` 输出 `rootfs / ramfs rw 0 0`；负向扫描没有 `syscall unhandled`、`page fault`、`exception`、`panic`、`failed`、`can't fork`、`Read-only file system`、`Permission denied`。

本次 coreutils rootfs 重建与 GNU coreutils smoke 验证命令：

```bash
scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst
tar --zstd -xOf kernel64/build/alpine-rootfs.tar.zst etc/apk/repositories
tar --zstd -xOf kernel64/build/alpine-rootfs.tar.zst lib/apk/db/installed | rg '^P:coreutils$'
tar --zstd -tf kernel64/build/alpine-rootfs.tar.zst | rg '^(bin/sh|lib/ld-musl-x86_64.so.1|usr/bin/cat|usr/bin/stat|usr/bin/sha256sum|usr/bin/sort|usr/bin/wc)$'
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 55s qemu-system-x86_64 -machine q35 -accel kvm -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-coreutils.log -monitor unix:/tmp/kernel64-coreutils.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|No such file|not found|Read-only file system|Permission denied|Function not implemented|Error relocating|Error loading" /tmp/kernel64-coreutils.log
```

QEMU coreutils smoke 用临时 `/bin/sh -c` argv 自动执行下面命令；验证后已恢复默认交互 `/bin/sh` argv，并重新执行 `make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`，最终短跑 `/tmp/kernel64-coreutils-final.log` 确认仍进入 `/ #` prompt：

```text
/usr/bin/printf 'abc\n' > /tmp/cu-a
/usr/bin/cat /tmp/cu-a
/usr/bin/wc -c /tmp/cu-a
/usr/bin/stat /tmp/cu-a
/usr/bin/realpath /tmp/cu-a
/usr/bin/sha256sum /tmp/cu-a
/usr/bin/sort /tmp/cu-a
/usr/bin/tr a-z A-Z < /tmp/cu-a
/usr/bin/truncate -s 1 /tmp/cu-a
/usr/bin/cat /tmp/cu-a
echo COREUTILS_DONE
```

结果：rootfs 中 `/etc/apk/repositories` 为 `https://mirror.sjtu.edu.cn/alpine/v3.23/main` 和 `.../community`，`lib/apk/db/installed` 包含 `P:coreutils` / `V:9.8-r1`，关键命令路径存在。`make -C kernel64 all` 和 `make -C kernel64 img build/OVMF_VARS.fd` 通过；QEMU 日志显示 `abc`、`4 /tmp/cu-a`、`stat` regular file 信息、`realpath` 输出 `/tmp/cu-a`、`sha256sum` 输出 `edeaaff3... /tmp/cu-a`、`sort` 输出 `abc`、`tr` 输出 `ABC`、`truncate -s 1` 后 `cat` 输出 `a`，最终 `COREUTILS_DONE` 且 `/bin/sh` status 0；负向扫描没有 `syscall unhandled`、`page fault`、`exception`、`panic`、`failed`、`can't fork`、`No such file`、`not found`、`Read-only file system`、`Permission denied`、`Function not implemented`、`Error relocating`、`Error loading`。

<!-- 过时内容（2026-06-14 sysinfo 已接入 frame allocator 统计，不再返回固定内存值）：本轮为 coreutils 补的最小内核侧修复：`stat/statx/fstat` 现在返回按规范化路径生成的稳定伪 inode，避免 musl 动态链接器用相同 dev+ino 误判不同共享库已加载；用户栈从 128 KiB 调整到 1 MiB；新增浅 `readv(2)`、`fadvise64(2)`、`getrlimit(2)`、`prlimit64(2)`、`sched_getaffinity(2)`、`sysinfo(2)`、`faccessat2(2)` 兼容。`fadvise64` 和查询类 syscall 仍只是浅兼容，不代表完整 Linux 资源限制、CPU affinity 或内存统计语义。 -->
本轮为 coreutils 补的最小内核侧修复：`stat/statx/fstat` 现在返回按规范化路径生成的稳定伪 inode，避免 musl 动态链接器用相同 dev+ino 误判不同共享库已加载；用户栈从 128 KiB 调整到 1 MiB；新增浅 `readv(2)`、`fadvise64(2)`、`getrlimit(2)`、`prlimit64(2)`、`sched_getaffinity(2)`、`sysinfo(2)`、`faccessat2(2)` 兼容。`fadvise64` 和查询类 syscall 仍只是浅兼容，不代表完整 Linux 资源限制或 CPU affinity 语义；`sysinfo(2)` 内存字段已复用 frame allocator 统计。

本次 bash/gcc rootfs 重建与 smoke 验证命令：

```bash
scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst
tar --zstd -xOf kernel64/build/alpine-rootfs.tar.zst etc/apk/repositories
tar --zstd -xOf kernel64/build/alpine-rootfs.tar.zst lib/apk/db/installed | rg '^P:(coreutils|bash|gcc|musl-dev)$'
tar --zstd -tf kernel64/build/alpine-rootfs.tar.zst | rg '^(bin/bash|usr/bin/(gcc|cc|as|ld)|usr/include/stdio.h|usr/lib/gcc/?)$'
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 140s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-bash-gcc.log -monitor unix:/tmp/kernel64-bash-gcc.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|No such file|not found|Read-only file system|Permission denied|Function not implemented|Error relocating|Error loading|cannot execute|gcc: fatal error|out of memory" /tmp/kernel64-bash-gcc.log
```

QEMU bash/gcc smoke 用临时 `/bin/sh -c` argv 自动执行下面命令；验证后已恢复默认交互 `/bin/sh` argv，并重新执行 `make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`，最终短跑 `/tmp/kernel64-bash-gcc-final.log` 确认仍进入 `/ #` prompt：

```text
echo BASH_GCC_START
/bin/bash --version
/bin/bash -c 'x=abc; echo "$x"; printf "%s\n" one two > /tmp/bash-a; cat /tmp/bash-a'
/usr/bin/printf '#include <stdio.h>\nint main(void) { puts("hello-gcc"); return 0; }\n' > /tmp/hello.c
/usr/bin/gcc /tmp/hello.c -o /tmp/hello
/tmp/hello
echo BASH_GCC_DONE
```

结果：rootfs 中 `/etc/apk/repositories` 仍为 `https://mirror.sjtu.edu.cn/alpine/v3.23/main` 和 `.../community`，`lib/apk/db/installed` 包含 `P:coreutils`、`P:bash`、`P:gcc`、`P:musl-dev`，关键路径 `bin/bash`、`usr/bin/{gcc,cc,as,ld}`、`usr/include/stdio.h`、`usr/lib/gcc/` 存在。`make -C kernel64 all` 和 `make -C kernel64 img build/OVMF_VARS.fd` 通过；QEMU 日志显示 bash 版本信息、`abc`、`one/two`，gcc 依次执行 `cc1`、`as`、`collect2`、`ld`，生成并执行 `/tmp/hello` 输出 `hello-gcc`，最终 `BASH_GCC_DONE` 且负向扫描为空。验证使用 `-m 1024M`，因为该 rootfs 解压后较大且 gcc/cc1 会触发更多映射和页分配。

<!-- 过时内容（2026-06-13 固定 VMA 槽已改为动态 vector；“从 128 提高到 512”只是历史过渡，不代表当前资源上限）：本轮为 bash/gcc 补的最小内核侧修复：新增 fd 层匿名 `pipe/pipe2`，用于 musl `posix_spawn` 错误通道和 `FD_CLOEXEC` EOF 语义；`/dev/null` 支持写入丢弃；每任务 VMA 槽从 128 提高到 512，避免 `cc1` 大量动态库/堆映射耗尽 VMA 表；新增浅 `getresuid/getresgid`、`socket` 探测失败、`chmod`、`umask`、`getrusage` 兼容。上述 syscall 仍是浅兼容，不代表真实权限、网络栈、资源统计或完整 pipe 阻塞/唤醒语义。 -->
本轮为 bash/gcc 补的最小内核侧修复：新增 fd 层匿名 `pipe/pipe2`，用于 musl `posix_spawn` 错误通道和 `FD_CLOEXEC` EOF 语义；`/dev/null` 支持写入丢弃；历史固定 VMA 槽问题现已替换为动态 VMA vector；新增浅 `getresuid/getresgid`、`socket` 探测失败、`chmod`、`umask`、`getrusage` 兼容。上述 syscall 仍是浅兼容，不代表真实权限、网络栈、资源统计或完整 pipe 阻塞/唤醒语义。

本次 bash `cd`、devtmpfs、PTY、poll/epoll、Ctrl+C、tmux 首缺口验证命令：

```bash
ALPINE_PACKAGES='coreutils bash gcc musl-dev tmux' scripts/build_rootfs.sh kernel64/build/alpine-rootfs.tar.zst
tar --zstd -xOf kernel64/build/alpine-rootfs.tar.zst lib/apk/db/installed | rg '^P:(tmux|bash|gcc|coreutils|musl-dev)$'
tar --zstd -tf kernel64/build/alpine-rootfs.tar.zst usr/bin/tmux bin/bash usr/bin/gcc usr/bin/cc usr/include/stdio.h
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 120s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tty-pty.log -monitor unix:/tmp/kernel64-tty-pty.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
timeout 160s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tmux.log -monitor unix:/tmp/kernel64-tmux.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
timeout 140s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-ctrlc.log -monitor unix:/tmp/kernel64-ctrlc.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
timeout 90s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tmux-latest.log -monitor unix:/tmp/kernel64-tmux-latest.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
timeout 90s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tmux-session.log -monitor unix:/tmp/kernel64-tmux-session.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
timeout 180s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tmux-termios3.log -monitor unix:/tmp/kernel64-tmux-termios3.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-tmux-termios-final.log -monitor unix:/tmp/kernel64-tmux-termios-final.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|Function not implemented|No such file|not found|Read-only file system|Permission denied|ENOSYS|ENOTTY" /tmp/kernel64-tty-pty.log /tmp/kernel64-tmux.log /tmp/kernel64-ctrlc.log
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|Function not implemented|Address family not supported|Protocol wrong type|ENOTTY|Bad file descriptor|Operation not supported|No such file|Invalid argument|Permission denied|Read-only file system|tmux" /tmp/kernel64-tmux-latest.log
rg -n "syscall unhandled|page fault|exception|panic|failed|can't fork|Function not implemented|Not a tty|ENOTTY|ENXIO|Address family not supported|server exited unexpectedly|create window failed|tmux" /tmp/kernel64-tmux-session.log
rg -n "syscall unhandled|page fault|exception|panic|Not a tty|ENOTTY|server exited unexpectedly|create window failed|tmux" /tmp/kernel64-tmux-termios3.log
rg -n "syscall unhandled|page fault|exception|panic|Not a tty|ENOTTY|server exited unexpectedly|create window failed|tmux" /tmp/kernel64-tmux-termios-final.log
```

临时 QEMU smoke 中执行过：

```text
/bin/bash -lc 'cd /tmp && pwd && printf x > cd-ok && cat /tmp/cd-ok'
ls /dev
ls /dev/pts
cat /proc/mounts
/bin/bash -lc 'exec 7<>/dev/ptmx; ls /dev/pts; exec 8<>/dev/pts/0; printf slave-to-master >&8; dd bs=1 count=15 <&7 2>/dev/null; printf master-to-slave >&7; dd bs=1 count=15 <&8 2>/dev/null'
/bin/bash -ic 'echo bashok; stty size'
tmux -V
tmux -L plos -f /dev/null new-session -d 'echo inside-tmux > /tmp/tmux-out'
cat /tmp/tmux-out
tmux new
# monitor: Enter x3, then Ctrl-b d
tmux attach
tmux detach
tmux ls
```

结果：`cd /tmp` 后 `pwd` 输出 `/tmp`，`/dev` 列出基础 TTY/PTY/null/zero 设备；`/proc/mounts` 显示 `devtmpfs /dev devtmpfs rw,nosuid 0 0`；打开 `/dev/ptmx` 后 `/dev/pts` 列出 `0`，slave->master 输出 `slave-to-master`，master->slave 输出 `master-to-slave`。`/tmp/kernel64-tmux-latest.log` 中 `/bin/stty size` 输出 `40 160`，`mkdir /tmp/probe` 成功且 `ls -ld /tmp/probe` 可见目录；前台 `cat` 阻塞后收到 `^C`，退出 status=130，shell 回到 `/ #` 后 `echo ok` 输出 `ok`。`/tmp/kernel64-tmux-session.log` 中 `/bin/bash -ic 'echo bashok; stty size'` 输出 `bashok` 和 `40 160`，`tmux -V` 输出 `tmux 3.6`；`tmux -L plos -f /dev/null new-session -d 'echo inside-tmux > /tmp/tmux-out'` 已生成 `/tmp/tmux-out`，`cat /tmp/tmux-out` 输出 `inside-tmux`。`/tmp/kernel64-tmux-layout.log` 中 `tmux new` 进入 alternate screen 并画出 status line；`seq 1 80` 滚屏后 pane 内输出仍从左侧开始，`plos64:/#` prompt 回到行首，`Ctrl-b d` 退出 tmux 并回到外层 `/ #`，输出 `[detached (from session 0)]`；`tmux attach` 重绘已有 pane 历史时 `43..80` 和 prompt 保持左对齐，再次 `Ctrl-b d` 可 detach。日志扫描未见 page fault、panic、未处理 syscall、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。验证后重新执行 `git diff --check`、`make -C kernel64 all`、`make -C kernel64 img build/OVMF_VARS.fd`；无残留 QEMU 进程。
<!-- 过时内容（2026-06-09 已删除 `/dev/sd0` AHCI 设备节点）：
历史 `/dev` 列表曾包含 `sd0`。
-->

本轮重复 gcc / 资源回收验证命令：

```text
cat /proc/meminfo
i=0; while [ $i -lt 30 ]; do printf '#include <stdio.h>\nint main(){puts("ok");return 0;}\n' > /tmp/t.c; gcc /tmp/t.c -o /tmp/t; /tmp/t; rm -f /tmp/t /tmp/t.c; i=$((i+1)); echo GCC_LOOP_$i; done
cat /proc/meminfo
tmux -L plos -f /dev/null new-session -d 'seq 1 80 > /tmp/tmux-seq-out; echo tmux-inside > /tmp/tmux-out'
cat /tmp/tmux-out
```

结果：`/tmp/kernel64-gcc-tmux.log` 中 30 次 gcc loop 通过到 `GCC_LOOP_30`，每轮生成的 `/tmp/t` 输出 `ok`，随后 tmux detached smoke 输出 `tmux-inside` 并到达 `GCC_TMUX_DONE`。负向扫描未见 `syscall unhandled`、`page fault`、`exception`、`panic`、`can't fork`、`ENOMEM`、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。当前 heap 统计字段为 `KheapUsed/KheapMapped`；主要修复是 ELF loader 流式读、退出/exec 时释放剩余 user leaf pages、task kernel stack 复用和 ramfs 临时节点/自有文件 buffer 复用。

本次 buddy frame + per-CPU SLUB 验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 240s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-buddy-slub.log -monitor unix:/tmp/kernel64-buddy-slub.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|ENOMEM|can't fork|Not a tty|ENOTTY|server exited unexpectedly|create window failed" /tmp/kernel64-buddy-slub.log
```

临时 `/bin/sh -c` smoke 执行：`cat /proc/meminfo`，50 次 `gcc /tmp/t.c -o /tmp/t && /tmp/t` loop，loop 后再次 `cat /proc/meminfo`，再执行 tmux detached smoke。结果：`/tmp/kernel64-buddy-slub.log` 中 50 次 gcc loop 通过到 `GCC_LOOP_50`，每轮 `/tmp/t` 输出 `ok`；`tmux-inside` 和 `BUDDY_SLUB_DONE` 均出现。负向扫描未见未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。当前 heap 统计字段为 `KheapUsed/KheapMapped`；验证后已恢复默认 `/bin/sh` 启动并重新构建最终镜像，短跑 `/tmp/kernel64-buddy-final-shell.log` 确认进入 `/ #`，无 smoke 残留。

<!-- 过时内容（2026-06-09 旧单全局锁 SLUB 验证，已由 buddy frame + per-CPU SLUB 验证取代）：
本次 SLUB heap 验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
timeout 220s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-slub.log -monitor unix:/tmp/kernel64-slub.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "syscall unhandled|page fault|exception|panic|ENOMEM|can't fork|Not a tty|ENOTTY|server exited unexpectedly|create window failed" /tmp/kernel64-slub.log
```

临时 `/bin/sh -c` smoke 执行：`cat /proc/meminfo`，30 次 `gcc /tmp/t.c -o /tmp/t && /tmp/t` loop，loop 后再次 `cat /proc/meminfo`，再执行 tmux detached smoke。结果：`/tmp/kernel64-slub.log` 中 30 次 gcc loop 通过到 `GCC_LOOP_30`，每轮 `/tmp/t` 输出 `ok`；loop 前 `BootmemUsed=220008 kB / KheapUsed=212251 kB / KheapMapped=212312 kB`，loop 后 `BootmemUsed=223908 kB / KheapUsed=215626 kB / KheapMapped=216204 kB`；`tmux-inside` 和 `SLUB_DONE` 均出现。负向扫描未见未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。验证后已恢复默认 `/bin/sh` 启动并重新构建最终镜像。
-->

本次 legacy storage 删除 + PCIe/NVMe smoke 验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
git diff --check
truncate -s 64M /tmp/kernel64-nvme-test.img
cp /usr/share/edk2/x64/OVMF_VARS.4m.fd kernel64/build/OVMF_VARS.fd
timeout 90s qemu-system-x86_64 -machine q35 -accel kvm -m 1024M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-nvme-read.log -monitor unix:/tmp/kernel64-nvme-read.mon,server,nowait -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive id=boot0,if=none,format=raw,file=kernel64/build/kernel64-uefi.img -device virtio-blk-pci,drive=boot0 -drive id=nvme0,if=none,format=raw,file=/tmp/kernel64-nvme-test.img -device nvme,drive=nvme0,serial=plosnvme0
rg -n "NVME_READ_START|NVME_READ_DONE|nvme0n1|hexdump|syscall unhandled|page fault|exception|panic|ENOMEM|can't fork|Not a tty|ENOTTY|server exited unexpectedly|create window failed|sd0|ahci|pci legacy" /tmp/kernel64-nvme-read.log
```

临时 `/bin/sh -c` smoke 执行：`ls /dev`，`dd if=/dev/nvme0n1 of=/tmp/nvme-sector bs=512 count=1`，`hexdump -C /tmp/nvme-sector`，30 次 gcc loop，再执行 tmux detached smoke。结果：`/tmp/kernel64-nvme-smoke2.log` 中 `/dev` 包含 `nvme0n1`，NVMe sector read + hexdump 完成，gcc loop 到达 `GCC_LOOP_30`，tmux detached smoke 输出 `tmux-inside` 并到达 `NVME_SMOKE_DONE`。负向扫描只命中预期 smoke marker、`nvme0n1`、`GCC_LOOP_30`、`tmux-inside`、`NVME_SMOKE_DONE`，未见未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly`、`create window failed`、AHCI/legacy PCI 或 `/dev/sd0` 残留。验证后已恢复默认 `/bin/sh` 启动。

Reviewer blocking 复查时临时 `/bin/sh -c` sector-read smoke 执行：`dd if=/dev/nvme0n1 of=/tmp/nvme-sector bs=512 count=1`，`hexdump -C /tmp/nvme-sector`。结果：`/tmp/kernel64-nvme-read.log` 中 `/dev` 包含 `nvme0n1`，`dd` 成功读出 512 字节，`hexdump` 输出 sector 内容并到达 `NVME_READ_DONE`；负向扫描未见未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly`、`create window failed`、AHCI/legacy PCI 或 `/dev/sd0` 残留。验证后已恢复默认 `/bin/sh` 启动。

最终默认 shell 短跑使用同一个 NVMe test image，`/tmp/kernel64-nvme-final-shell.log` 显示 `kernel64: nvme nvme0n1 sectors=131072 sector_size=512`、`kernel64: dev nvme0n1 device=1`、Alpine rootfs 解包挂载和 `/ #` prompt；日志中没有 `NVME_SMOKE_*` / `NVME_READ_*` 临时 marker，也没有未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、`Not a tty`、`ENOTTY`、`server exited unexpectedly` 或 `create window failed`。

<!-- 过时内容（user-test-rootfs target 已删除，以下历史 FAT smoke 命令不可再执行）：
本次 file-backed `mmap` / page fault / 只读页缓存 / COW / 用户页回收验证命令：

下面几段 `user-test-rootfs` 记录是历史静态 FAT smoke，不代表当前默认 Alpine `/boot/rootfs.tar.zst` 启动路径。

```bash
make -C kernel64 all
make -C kernel64 -B user-test-rootfs
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-page-cache-smoke-final.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "page fault|failed|user-memory|execve|exited status|syscall unhandled" /tmp/kernel64-page-cache-smoke-final.log
```

结果：`user-memory: ok`、`kernel64: task /bin/sh4 exited status=0`；没有 `page fault`、`failed` 或 `syscall unhandled` 命中。测试程序会触发 `brk` 懒分配、匿名 `mmap` 懒分配、`mprotect`、`munmap`、`openat("/bin/busybox")`、file-backed `MAP_PRIVATE|PROT_READ|PROT_WRITE`、ELF magic 读取、私有写 COW、同一文件页二次映射的页缓存复用，以及 file-backed `MAP_SHARED` 后续 `mprotect(PROT_WRITE)` 被只读 VFS 语义拒绝。

本次共享写回 / 同步 signal 验证命令：

```bash
make -C kernel64 all
make -C kernel64 -B user-test-rootfs
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-shared-signal-smoke.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "shared|signal|user-memory|failed|page fault|syscall unhandled|exited status" /tmp/kernel64-shared-signal-smoke.log
```

结果：日志包含 `user-memory: ok`、`user-shared: ok`、`user-signal: ok`、`kernel64: task /bin/sh4 exited status=0`；没有 `failed`、`page fault` 或 `syscall unhandled` 命中。测试程序会以 `O_RDWR` 打开 `/bin/busybox`，对第二页做 `MAP_SHARED|PROT_READ|PROT_WRITE`，修改一个字节后 `munmap`，再用 `lseek/read` 验证 VFS 能读到写回值，并通过第二次 shared mmap 恢复原字节；signal 测试安装 SIGSEGV handler，把只读匿名页写 fault 转成用户态 SIGSEGV，handler `mprotect(PROT_READ|PROT_WRITE)` 后 `rt_sigreturn`，原写指令重试成功。

本次 syscall 返回路径改为 `sysretq` 后验证命令：

```bash
make -C kernel64 all
make -C kernel64 -B user-test-rootfs
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-sysret-smoke.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "execve|exited status|user-yield" /tmp/kernel64-sysret-smoke.log
rg -n "page fault|failed|syscall unhandled|exception|panic" /tmp/kernel64-sysret-smoke.log
```

结果：日志包含 `kernel64: execve /bin/sh ...`、`user-yield: before sched_yield`、`user-yield: after sched_yield`、`kernel64: task /bin/sh4 exited status=0`；负向扫描没有 `page fault`、`failed`、`syscall unhandled`、`exception` 或 `panic`。

-->

本次 syscall `sysretq` 热路径 + `rt_sigreturn` iret 慢出口验证命令：

```bash
make -C kernel64 all
make -C kernel64 img build/OVMF_VARS.fd
# 临时把 process_spawn_first_user() 改为 /bin/sh -c，用 builtin printf 写入 /tmp/sigreturn.c，
# 编译后触发 SIGSEGV handler，handler mprotect 后经 rt_sigreturn 返回原 faulting write。
timeout 35s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-syscall-iret-smoke.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "SIGRETURN_OK|SYSCALL_IRET_SMOKE_DONE|page fault|syscall unhandled|panic|exception|failed" /tmp/kernel64-syscall-iret-smoke.log
```

结果：`/tmp/kernel64-syscall-iret-smoke.log` 包含 `SIGRETURN_OK` 和 `SYSCALL_IRET_SMOKE_DONE`；负向扫描未见 `page fault`、`syscall unhandled`、`panic`、`exception` 或 `failed`。验证后已恢复默认 `/bin/sh` 启动并重建最终镜像。

<!-- 过时内容（user-test-rootfs target 已删除，以下历史静态 smoke 命令不可再执行）：
本次 musl/Alpine 启动前段 syscall ABI smoke 验证命令：

```bash
make -C kernel64 -B user-test-rootfs
make -C kernel64 img build/OVMF_VARS.fd
timeout 20s qemu-system-x86_64 -accel kvm -machine q35 -m 512M -smp 4 -cpu host,x2apic=on,tsc-deadline=on -serial file:/tmp/kernel64-musl-abi.log -display none -nodefaults -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=kernel64/build/OVMF_VARS.fd -drive format=raw,file=kernel64/build/kernel64-uefi.img
rg -n "user-|syscall unhandled|page fault|exception|panic|failed|exited status" /tmp/kernel64-musl-abi.log
```

结果：日志包含 `user-memory: ok`、`user-shared: ok`、`user-signal: ok`、`user-abi: core/fs/misc ok`、`kernel64: task /bin/sh4 exited status=0`；负向扫描没有 `syscall unhandled`、`page fault`、`exception`、`panic` 或 `failed`。新的 `user-abi` smoke 覆盖 `arch_prctl(SET_FS/GET_FS)`、`set_tid_address`、`fstat/newfstatat`、`access/faccessat` 三参数 ABI（含脏 `r10`）、`pread64` 且不改变 fd offset、`readlink("/proc/self/exe")`、`getcwd`、`uname`、`clock_gettime`、`gettimeofday`、`getrandom`、普通 fd 和标准 fd 的 `fcntl/dup/dup2/dup3`、`getdents64`、`ioctl(TIOCGWINSZ)`、`poll`、浅 `futex` 和 uid/ppid 返回值。
-->

注意：本次实现按要求不接 HPET、不做 periodic timer 回退，并假设 TSC invariant。当前仓库默认 `QEMU_ACCEL=kvm`、`QEMU_CPU=host,x2apic=on,tsc-deadline=on`；本机 QEMU TCG 会提示不支持 `tsc-deadline` CPUID 位，内核会在 `arch_init()` 阶段拒绝启动；已通过 KVM 验证。

### 最新稳定运行日志要点

最新稳定日志要点已由下面的 NVMe smoke 覆盖；历史 AHCI 日志如下，仅保留作时间线参考。

<!-- 过时内容（2026-06-09 已删除 AHCI/legacy PCI/FAT smoke 路径）：
已看到稳定日志：

```text
kernel64: dev null device=1
kernel64: dev tty0 device=1
kernel64: ahci controller 0 abar=0x80000000 pi=3f vs=10000
kernel64: ahci port 0 sig=101 type=sata
kernel64: ahci sd0 port=0 sectors=524288
kernel64: ahci sd0 sector0 head=eb 58 90 4d sig=aa55
kernel64: ahci controllers=1 disks=1
kernel64: rootfs tar.zst module[0]=/boot/rootfs.tar.zst compressed=3199178 tar=8732160 nodes=516
kernel64: execve /bin/sh task=4 entry=0x70000006b4e7 sp=0x7fffffffde80 cr3=0x1882000 brk=0x4c6000
kernel64: execve interp=/lib/ld-musl-x86_64.so.1 base=0x700000000000 entry=0x70000006b4e7
/ #
ash
kernel64: execve /bin/ash task=7 entry=0x70000006b4e7 sp=0x7fffffffde60 cr3=0x1bc0000
/ #
dd bs=1 count=3
kernel64: execve /bin/dd task=7 entry=0x70000006b4e7 sp=0x7fffffffde40 cr3=0x1bc0000
abc3+0 records in
3+0 records out
kernel64: task /bin/sh47 exited status=0
/ #
kernel64: dev sd0 device=1
kernel64: task smoke spawned rt=4 cfs=5
kernel64: arch apic=1 x2apic=1/1 tsc=1 invariant=1 deadline=1 hz=1000000000 fxsr=1 nx=1/1 sse=1 sse2=1 enabled=1
kernel64: pci storage_controllers=1
kernel64: pci ahci 0:1f.2 vendor=8086 device=2922 prog_if=1 pcie=0
kernel64: apic present=1 x2apic=1/1 deadline=1 lapic=0xfee00000 bsp_lapic=0 ioapics=1 timer_vec=240
kernel64: smp cpus=4 online=4 bsp_lapic=0 x2apic=1
kernel64: task bootstrap_cpus=4 sched=cfs+rt domain=root cpus=4
kernel64: runq cpu0 runnable=2 rt=0 cfs_load=2048
kernel64: runq cpu1 runnable=1 rt=0 cfs_load=1024
kernel64: runq cpu2 runnable=1 rt=0 cfs_load=1024
kernel64: runq cpu3 runnable=1 rt=0 cfs_load=1024
kernel64: task smoke rt done cpu=0 steps=64
kernel64: task smoke cfs done cpu=0 steps=96
```
-->

说明当前已经实证：

- tar.zst module rootfs 正常
- tar.zst Alpine rootfs 解压和 ramfs mount 正常
- `process_spawn_first_user()` 已接入启动期用户进程启动；默认 Alpine rootfs 会直接启动真实 `/bin/sh`
- ELF64 loader / 独立 CR3 / ring3 `iretq` / syscall ABI 成功路径已用真实 Alpine `/bin/sh` 验证到交互提示符
- devtmpfs 已挂载并暴露基础设备节点，旧 devfs 已替换
- 多 TTY 基础路径已通，stdin 已接默认 TTY；已有最小 PTY 和 Ctrl+C -> SIGINT，仍缺完整 job control/session/line discipline
- ACPI MCFG + PCIe ECAM 枚举正常
- 已识别 QEMU NVMe 控制器和 namespace 1
- NVMe identify 和只读 sector read smoke 正常，设备节点为 `/dev/nvme0n1`
<!-- 过时内容（2026-06-09 已删除 AHCI/legacy PCI）：
- PCI 存储控制器枚举正常
- 已识别到 AHCI 控制器和 SATA port 0
- ATA IDENTIFY 正常，QEMU 盘识别为 524288 sectors
- AHCI 盘已注册为 `/dev/sd0`
- AHCI 多 sector 读路径可用，sector 0 签名为 `55 aa`
-->
- SSE / FXSR / NX 已启用，`arch` 日志显示 `fxsr=1 nx=1/1 sse=1 sse2=1 enabled=1`
- TSC-deadline 已启用，`arch` 日志显示 `tsc=1 invariant=1 deadline=1`，`apic` 日志显示 `deadline=1`
- SMP 4 核在线，AP 已完成本地初始化并配置 TSC-deadline one-shot timer
- task 层已有 MUQSS 风格 per-CPU deadline runqueue，默认 `/bin/sh` 和短命外部命令 smoke 已覆盖 exec/fork/wait/exit 路径
<!-- 过时内容（2026-06-14 已替换为 MUQSS 风格 per-CPU deadline runqueue）：- task 层已有 CFS/RT/root-domain 调度骨架，当前每 CPU 一个 bootstrap runnable -->
<!-- 过时内容（默认 RT/CFS kernel task smoke 已删除）：
- `task_spawn_kernel()` 已通过一个 RT 和一个 CFS kernel task 实证：两个任务都从 deadline/reschedule 中断切入执行，完成后返回 trampoline 并阻塞，系统继续运行
-->
<!-- 过时内容（parallel-sum demo 已删除）：
- 历史 parallel-sum demo 记录；对应实现已删除，默认启动直接 exec `/bin/sh`。
-->

## 当前工作区状态

当前未提交修改主要包括：

- `.gitignore`
- 32 位主线中与 VFS / drive 相关的几个文件
- `doc/x86_64_limine_handoff.md`
- `kernel64/` 整个目录

注意：

- 当前仓库状态里 `kernel64/` 仍可能整体处于未跟踪状态
- 不要因为它未跟踪就误判为“不存在实际实现”

## 当前明确还没做的事

`kernel64/` 现在仍然没有：

- 完整 Linux 用户态进程模型
- 用户态 `psh` 启动链路
- 完整 Linux syscall 面；当前已为 musl/Alpine 启动和简单外部命令补一批基础 ABI，`statx`、`prctl`、`madvise` 也已有浅实现，但仍缺真实 signal/线程/futex/阻塞语义、更多 syscall 和完整错误码/权限语义
- 完整进程生命周期、阻塞唤醒、线程组、退出回收、优先级调整 syscall；当前只有直接父子 task 和浅 `wait4`
- 完整用户态异常 / signal
- 完整 fd 表、真实阻塞/非阻塞等待和完整 readiness；当前 fd 继承、close-on-exec、cwd、devtmpfs 设备 fd、PTY fd、浅 poll/epoll 只有最小 shell/tmux-smoke 路径
- 真实页缓存、`msync`、通用/持久化文件系统的 create/truncate/extend 语义、symlink/权限模型、持久块设备 write/flush
- 红黑树/堆等更高效的 CFS runqueue；当前 CFS 是线性扫描骨架
- 更完整的任务生命周期；当前已有浅用户 task 退出和直接子进程 wait/reap，但没有完整进程/线程对象模型
- NUMA / socket / core / SMT 等多级调度域；当前只有 root domain
- NVMe write/flush、中断驱动 I/O、多 namespace/多 controller 管理
- rootfs 显式策略配置；当前默认只挂载 Limine module tar.zst rootfs
- IOAPIC 路由 / 外设中断体系
- 更完整的异常处理
- 块设备热插拔 / 动态枚举

所以当前状态是：底座、抽象层、module rootfs、tar.zst Alpine rootfs、可写 ramfs、devtmpfs、最小 PTY、浅 poll/epoll/pselect6、Ctrl+C 默认 SIGINT、最小 AF_UNIX stream socket 和浅 `SCM_RIGHTS/SO_PEERCRED`、PCIe ECAM 枚举、NVMe 只读块设备、ELF64 镜像加载、`PT_INTERP` 动态链接器入口、SSE/FXSR/NX、最小内核任务调度边界、独立用户 CR3、ring3 `iretq` 和最小 syscall ABI 已经成型；真实 Alpine `/bin/sh` 已通过 `/lib/ld-musl-x86_64.so.1` 启动到交互提示符，并已能连续执行 `echo hi`、`/bin/busybox echo ext`、`ls /`、`pwd`、`cd /tmp`、coreutils、bash/gcc smoke 这类命令；tmux detached smoke 已可创建会话并在 pane 内执行命令写出 `/tmp/tmux-out`。后续重点是把当前浅 PTY/session/job-control 扩到可长期交互运行 tmux，而不是只跑 detached smoke。

## 跑 Alpine rootfs + wlroots 还缺的内核侧能力

用户当前目标：

- 直接使用 Alpine rootfs，不考虑用户态内容移植
- 用 llvmpipe，先不考虑 lwIP 和 lwext4
- 长期目标是让 `kernel64` 能跑 wlroots

按优先级建议补齐：

### P0：Linux 用户态 ABI 与进程底座

- ELF64 loader：已支持 `PT_LOAD`、`PT_INTERP`、动态链接器入口、用户栈布局、`argc/argv/envp/auxv`，并已接入独立地址空间和最小 `execve`
- 用户地址空间：独立 `CR3`、用户页权限、`brk`、匿名/file-backed `mmap`、`munmap`、`mprotect`、page fault demand paging、file-backed 页缓存、COW、用户物理页回收和 module rootfs 上的 shared writable mmap 写回已接入；后续要补真实页缓存、msync、持久块设备写回和完整文件写语义
- syscall ABI：x86_64 Linux syscall 调用约定、错误码返回、`syscall` 入口和 `sysretq` 正常返回路径已接入；已为 musl/Alpine 启动补齐一批基础 ABI，下一步要用真实 Alpine rootfs 找缺口并补进程/伪文件系统相关 syscall
- 进程/线程生命周期：内核内 `process_execve`、用户态 `execve`、`fork`、受限 `vfork/clone`、`wait4`、`exit/exit_group`、TLS `arch_prctl`、`set_tid_address` 已有最小实现；后续要补完整进程/线程组、真实 clone flags、退出状态/资源回收、clear-child-tid futex 唤醒和信号/会话/进程组语义
- signal：已接入最小 `rt_sigaction`、`rt_sigprocmask`、`rt_sigreturn` 和同步 #PF -> SIGSEGV；后续要补异步 signal、线程/进程组语义、altstack、pending 队列、kill/tgkill、signalfd

### P1：musl / Alpine 基础 syscall 面

- 文件 I/O：`open/openat`、`read/readv`、`write/writev`、`close`、`lseek`、`pread64`、浅 `sendfile`、浅 `fadvise64`、`pipe/pipe2`、`fstat/stat/lstat/newfstatat/statx`、`getdents64`、`readlink/readlinkat`、`access/faccessat/faccessat2` 已有最小实现；默认 ramfs rootfs 已支持运行期 create/truncate/extend/rename/unlink，后续要补真实权限/类型、open fd 生命周期和持久写回
- 内存：`mmap` 系列已支持匿名映射、私有映射、共享映射、file-backed 映射和权限切换；`madvise` 目前只是参数检查后浅返回，不改变 VMA
- 同步：浅 `futex` 已够单线程 smoke；pthread、Mesa、Wayland、libinput 仍需要真实 wait/wake 队列和超时语义
<!-- 过时内容（2026-06-14 sysinfo 内存字段已接入 frame allocator 统计）：- 时间与基础信息：`clock_gettime`、`gettimeofday`、`getrandom`、`uname`、`getpid/gettid/getuid/getresuid/getresgid`、`getrlimit/prlimit64`、`sched_getaffinity`、`sysinfo`、`getrusage` 等已有浅实现；后续要补更准 wall clock、随机源、真实 rlimit/affinity/meminfo/rusage -->
- 时间与基础信息：`clock_gettime`、`gettimeofday`、`getrandom`、`uname`、`getpid/gettid/getuid/getresuid/getresgid`、`getrlimit/prlimit64`、`sched_getaffinity`、`sysinfo`、`getrusage` 等已有浅实现；`sysinfo` 的 total/free/procs 已接入 frame allocator 和 task registry 统计。后续要补更准 wall clock、随机源、真实 rlimit/affinity/rusage。
<!-- 过时内容（2026-06-13 fd 表已动态化，pipe 已具备 O_NONBLOCK/EAGAIN 和最小 blocking/wakeup）：- fd 属性：`fcntl` 的 `FD_CLOEXEC`、`F_GETFL/F_SETFL` 以及 `dup/dup2/dup3` 已有最小实现，标准 fd 可复制也可被 `dup2` 覆盖，fork/exec 路径已有最小 fd 继承和 close-on-exec；后续要补 `O_NONBLOCK` 真实行为、设备 fd 对象和完整阻塞/唤醒 -->
- fd 属性：`fcntl` 的 `FD_CLOEXEC`、`F_GETFL/F_SETFL` 以及 `dup/dup2/dup3` 已有最小实现，fd table 为 task-owned 动态表并支持按需扩容；pipe 已接入 `O_NONBLOCK`、`EAGAIN/EINTR/EPIPE` 和最小阻塞/唤醒。后续要补设备 fd 的完整非阻塞/阻塞语义、真实权限和更完整的 wake/fairness。

### P2：VFS 与伪文件系统

- VFS 已从只读薄接口推进到默认 ramfs rootfs 运行期可写，并为 `stat/statx/fstat` 返回稳定伪 inode 和基本 mode bits；后续仍要补真实权限检查、完整 open fd 生命周期、非阻塞 I/O、持久写回语义和更完整的挂载模型
- `tmpfs`：支撑 `/run`、`/tmp`、`/dev/shm`；Wayland 和运行时组件通常需要可写临时文件系统
- `devtmpfs`：已有 `/dev/null`、`/dev/zero`、`/dev/tty`、`/dev/tty0..3`、`/dev/ptmx`、`/dev/pts/N` 和 NVMe `/dev/nvme0n1`；`/dev/null` 支持写入丢弃，PTY 具备最小双向 I/O 和 ioctl；后续至少还要补 `/dev/input/event*`、`/dev/dri/card0`、必要时 `/dev/dri/renderD128`
- `procfs`：已有只读最小 `/proc` 挂载，支持 `/proc`、`/proc/self`、`/proc/self/exe` symlink、`/proc/self/fd` 动态枚举与 `/proc/self/fd/N` readlink、`/proc/mounts`、`/proc/stat`、`/proc/meminfo`、`/proc/cpuinfo`；后续要补更多动态内容
- `sysfs`：wlroots/libinput/udev 需要从 `/sys/class/drm`、`/sys/class/input`、`/sys/devices/...` 发现设备和属性

### P3：IPC 与事件模型

- Unix domain socket：已有最小 AF_UNIX stream socket 骨架和相关 syscall/fd/VFS 接入，pathname bind/socket-node 已可越过 tmux 的 `/tmp/tmux-0/<name>` 路径；Wayland socket、seatd socket 后续仍需要补完整多消息、权限、错误码、阻塞唤醒和 edge cases
- fd 传递与 credentials：已有浅 `SCM_RIGHTS` fd passing 和 `SO_PEERCRED` root `ucred`；后续需要补完整 ancillary 队列、`SO_PASSCRED` 真实凭证、截断语义和跨进程生命周期细节
<!-- 过时内容（2026-06-13 pipe 已改为动态 64KB buffer 并具备当前浅阻塞/非阻塞读写路径；后续重点收敛为真实等待队列/唤醒/公平性，而不是补旧 4KB 静态 pipe 读写）：- pipe：已有最小 `pipe/pipe2` 支撑 `posix_spawn` 错误通道，并接入浅 readiness；后续补完整阻塞/非阻塞读写和唤醒 -->
<!-- 过时内容（2026-06-13 后续 worker 已补最小 task blocking/wakeup、O_NONBLOCK 和 EPIPE；仍缺的是 SIGPIPE/fairness/超时/统一 wait queue）：- pipe：已有动态分配 pipe 对象和 per-pipe 64KB ring buffer，`pipe/pipe2` 支撑 `posix_spawn` 错误通道并接入浅 readiness；后续补真实等待队列、唤醒、公平性、SIGPIPE/EPIPE 等完整语义 -->
- pipe：已有动态分配 pipe 对象和 per-pipe 64KB ring buffer，`pipe/pipe2` 支撑 `posix_spawn` 错误通道；空读/满写会通过 task blocking/wakeup 睡眠等待，对端读写或关闭会唤醒；`O_NONBLOCK` 返回 `EAGAIN`，pending signal 返回 `EINTR`，no-reader write 返回 `EPIPE`。后续补 SIGPIPE、统一 wait queue/fairness、超时和更严格并发语义。
- 共享内存：优先实现 `memfd_create`，同时让 `/dev/shm` 走 tmpfs；SysV shm 可后置
- 事件 fd：`eventfd`、`signalfd`、`timerfd`
<!-- 过时内容（2026-06-13 pipe read EOF readiness、HUP 和 O_NONBLOCK 已补；仍缺完整 select/epoll 模型和统一 wait）：- readiness：`poll/ppoll`、浅 `pselect6` 和浅 `epoll_create/epoll_ctl/epoll_wait` 已有最小 fd readiness；后续补完整 select 族、真实等待队列、超时精度、`O_NONBLOCK` 和 epoll 边沿/一次性等完整语义 -->
- readiness：`poll/ppoll`、浅 `pselect6` 和浅 `epoll_create/epoll_ctl/epoll_wait` 已有最小 fd readiness；pipe EOF 读端可读，pipe/socket HUP/readiness 对 poll/select/epoll 可见。等待路径仍是浅轮询/yield，后续补完整 select 族、统一 wait 队列、超时精度和 epoll 边沿/一次性等完整语义。

### P4：输入、TTY/VT 与 seat 管理

- evdev：`/dev/input/event*`、`struct input_event` 读取、`EVIOCG*` ioctl、非阻塞和 poll
- libinput 需要 sysfs/udev 属性配合，不能只有裸键盘输入
- TTY/PTY：已有最小 PTY master/slave、`/dev/ptmx`、`/dev/pts/*`、winsize/pgrp/termios 浅 ioctl、浅 session/controlling tty、`/dev/tty` ctty 映射、显式 TTY fd passing 和 Ctrl+C -> SIGINT；交互 tmux smoke 已可进入界面并在 pane 内执行命令，后续要补完整 line discipline、job-control 信号、阻塞/唤醒和 attach/多窗口等长期交互语义
- VT/console ioctl：wlroots DRM session 通常需要知道/切换当前 VT，并处理 active/inactive
- seatd/libseat：内核侧重点是 Unix socket、设备 fd 权限/凭证、TTY/VT、DRM/input fd 管理；早期可以 root 跑并弱化权限模型，但接口形状要留对

### P5：DRM/KMS 显示栈

- llvmpipe 只解决 CPU 渲染，不替代 DRM/KMS；显示输出仍需要 `/dev/dri/card0`
- 先做 KMS-only DRM 设备：DRM ioctl 分发、mode resources、connector、encoder、CRTC、plane
- dumb buffer 路径：`DRM_IOCTL_MODE_CREATE_DUMB`、`MAP_DUMB`、`ADDFB2`、page flip 或 atomic commit
- modesetting：legacy KMS 可作为早期目标，但 wlroots 更偏 atomic，建议规划 atomic modesetting 最小子集
- GBM/EGL 路径压力较大；如果能让 wlroots 使用 pixman renderer，可先绕开一部分 Mesa GBM/DRI 复杂度
- framebuffer 终端和 DRM scanout 要有所有权切换策略，避免内核 console 与 compositor 同时写同一输出

### P6：块设备、rootfs 与系统稳定性

- Alpine rootfs 如果不是 ext4，就需要明确启动介质策略；可考虑 initramfs/cpio、squashfs 或可写 tmpfs 组合
- 如果暂不做 lwext4，就不能假设历史 FAT 路径能承载 Alpine 的 symlink、权限、设备节点和包管理语义；该路径已删除
- NVMe 当前只读轮询足够启动探索和块设备 smoke，但长期需要写入、缓存、flush、中断驱动 I/O、多 namespace/多 controller 管理
- `ioctl` 架构要尽早抽象好，因为 TTY、evdev、DRM 都重度依赖 ioctl
- 权限/credential 可以先简化，但 `stat`、设备权限、用户/组 ID、`geteuid` 等必须返回合理值

## 下一步最建议的实施顺序

建议按这个顺序继续：

1. 继续收尾 tmux 长期交互模式：direct `tmux new`、detached `new-session -d`、`Ctrl-b d`、`tmux detach`、`tmux attach` 重绘和 `seq 1 80` 滚屏 smoke 已通过；下一步重点是多窗口/窗格操作、长时间 attach、resize/Ctrl+C、PTY line discipline、job-control 信号、foreground pgrp 和会话细节
2. 补 tmpfs/devtmpfs 的最小可写运行时目录和嵌套挂载，尤其是当前 `/dev/shm` 会被 `/dev` devtmpfs 挂载遮蔽，目标是让 Alpine `/bin/sh`、tmux 和更多基础工具稳定运行
3. 整理当前最小 `fork/exec/wait` 的资源释放语义，尤其是 fork child `exec/exit` 后 COW 页引用回收；cwd 已有，后续重点是资源生命周期和错误回滚
<!-- 过时内容（2026-06-13 pipe 已不再是旧静态 4KB 读写实现；仍缺的是真实 wait queue/wakeup/fairness）：4. 补 memfd、eventfd、signalfd/timerfd、完整 pipe 阻塞/唤醒、完整 signal、完整 select 族和更真实的 poll/epoll 等事件模型，目标是让 Wayland/seatd 这类事件驱动用户态能启动 -->
<!-- 过时内容（2026-06-13 pipe 已有最小 task blocking/wakeup；仍缺统一 wait queue/fairness/SIGPIPE/超时和 futex 等其它等待源）：4. 补 memfd、eventfd、signalfd/timerfd、pipe 真实等待队列/唤醒/公平性、完整 signal、完整 select 族和更真实的 poll/epoll 等事件模型，目标是让 Wayland/seatd 这类事件驱动用户态能启动 -->
4. 补 memfd、eventfd、signalfd/timerfd、pipe 统一 wait queue/fairness/SIGPIPE/超时、完整 signal、完整 select 族和更真实的 poll/epoll 等事件模型，目标是让 Wayland/seatd 这类事件驱动用户态能启动
5. 补 evdev、sysfs 设备属性、完整 TTY/VT 和 seat 管理接口，目标是让 libinput/libseat 找到并管理输入与显示设备
6. 做最小 DRM/KMS：`/dev/dri/card0`、mode resources、dumb buffer、page flip/atomic commit，目标是 wlroots pixman 或 llvmpipe 路径能显示
7. 再推进 NVMe 写入/flush/中断、多 controller/namespace、rootfs 挂载策略和动态块设备管理

这个顺序能最大程度减少一次性引入太多复杂度，也符合用户要求的“精简、优雅、抽象清晰”。

## 关于 PCIe / NVMe / devtmpfs 的建议

- PCIe 目前保持 MCFG/ECAM 薄枚举和 BAR/config 辅助能力，不引入旧 CF8/CFC 兼容层
- NVMe 优先级高于 AHCI/IDE 旧路径；当前已有 identify/read smoke，下一步是补写入、flush、中断和更完整 namespace 管理
- devtmpfs 当前已经够基础 shell/PTY smoke 使用，下一步设备侧重点应转向输入、DRM 和更完整的动态设备节点，而不是恢复旧 devfs
<!-- 过时内容（2026-06-09 已删除 AHCI/IDE 路线）：
- PCIe 目前先保持“识别能力”，不必急于为它单独做复杂框架
- AHCI 优先级高于 IDE，因为当前机器上已经枚举到 AHCI 控制器
- IDE 可以后置，只在需要兼容旧设备时补上
-->

## 关键注意事项

- 不要把 32 位内核的大量历史包袱直接搬进 `kernel64`
- `main.c` 应继续保持 orchestration-only
- 抽象优先做薄，不做大而全
- `percpu` 继续沿用内核态 `GS_BASE`，不要再回到 `__thread`；用户态 syscall/interrupt 入口和返回必须成对 `swapgs`
- APIC 继续默认走 `x2APIC`
- timer 继续走 TSC-deadline one-shot；不要接 HPET，不做 periodic 回退；当前假设 TSC invariant
- QEMU smoke 需要暴露 `tsc-deadline`，本机 TCG 不支持该位，已用 KVM 验证
- 编译仍保持 `-mno-sse -mno-sse2`，SSE 只作为显式 CPU/task 状态支持，不让内核 C 代码隐式使用 XMM
- 新 kernel task 的合成中断帧必须包含 `RIP/CS/RFLAGS/RSP/SS`，否则 `iretq` 会把后续栈内容误当作 `SS` 并触发 #GP
- 用户任务也使用统一中断帧，`CS/SS` 为 DPL=3；调度切换必须同步 task CR3 和本 CPU TSS.RSP0/syscall 栈
<!-- 过时内容（2026-06-14 syscall stub 已删除非 canonical fallback）：- syscall 入口已使用 x86_64 `syscall` 指令和 `swapgs`，按 Linux x86_64 参数约定进入；正常返回路径使用 `sysretq`，会从当前 task 栈上的 syscall frame 恢复用户 `RIP/RFLAGS/RSP`，并在 `RIP` 非 canonical 时保守 fallback 到 `iretq` -->
- syscall 入口已使用 x86_64 `syscall` 指令和 `swapgs`，按 Linux x86_64 参数约定进入；普通返回路径直接从当前 task 栈上的 syscall frame 恢复用户 `RIP/RFLAGS/RSP` 并 `sysretq`，不再做内联 fallback 检查；需要全寄存器精确恢复的 `rt_sigreturn` 通过独立 `syscall_iret_return()` 慢出口 `iretq` 返回
- 当前最有价值的继续方向不是再扩 VFS，而是沿现有 task/context、ELF、syscall、PCIe/NVMe 和设备/事件模型往用户态启动链路推进
- 调试/运行需要申请沙箱外执行，读代码和改代码不需要

## 本次交接时的最准确信息

截至当前：

- `kernel64/` 能稳定启动到 64 位内核
- framebuffer 终端已切 flanterm；当前只显示活动 TTY/shell，内核日志只走串口
<!-- 过时内容（2026-06-12 framebuffer 不再作为 kernel log sink）：
- framebuffer 终端已切 flanterm
-->
- newline 排版问题已修正
- SMP 已真实 bring-up，多核已 online
- APIC 已启用 `x2APIC`
- 中断、TSC-deadline one-shot timer 和 reschedule IPI 已工作
- SSE / FXSR 已接入并在 BSP/AP 初始化
- 多 TTY 已具备；PS/2 方向键已转换为 ANSI `ESC [ A/B/C/D` 输入序列
- devtmpfs 已具备，含 `/dev/null`、`/dev/zero`、TTY、`/dev/ptmx`、`/dev/pts/N` 和 `/dev/nvme0n1`
- procfs 已具备只读最小挂载，支持 `/proc/self/exe`、`/proc/self/fd` 动态枚举、`/proc/self/fd/N` readlink 和基础文本文件
- 薄 `blockdev` 抽象已具备，当前 NVMe 通过该抽象提供只读 sector read
- 极小 VFS 已具备；默认 ramfs rootfs 当前支持运行期内存可写，包括 create/truncate/extend/append/overwrite、mkdir/unlink/rmdir/rename
- ACPI MCFG 解析和 PCIe ECAM 枚举已具备，覆盖全部 MCFG allocations/segments/bus ranges，不保留 legacy CF8/CFC `pci_*` 接口
- NVMe polling 驱动已具备最小 identify/read 路径：BAR0 MMIO、CAP.MQES queue depth clamp、CAP.DSTRD doorbell range mapping、admin/io queue、identify controller/namespace、只读 sector read、`/dev/nvme0n1` devtmpfs 节点；probe failure 会释放已分配 frame/heap 资源
- storage 当前只挂载 Limine module tar.zst rootfs；NVMe 暂不作为 rootfs fallback
<!-- 过时内容（2026-06-09 已删除 AHCI/legacy PCI/FatFs/diskio）：
- FatFs 历史路径曾验证可读文件；当前默认构建不编译，也不作为 rootfs 路径
- PCI 存储控制器枚举已具备
- 已识别到 AHCI 控制器和 SATA 盘
- AHCI 已完成 BAR 映射、port 枚举、IDENTIFY、多 sector 只读 sector read
- AHCI 盘已接入 `blockdev` 并通过 `/dev/sd0` 暴露
- storage 已支持 module rootfs 优先、AHCI blockdev 作为候选
-->
- ELF64 loader 已具备 `PT_LOAD` / `PT_INTERP` / 动态链接器入口 / `argc/argv/envp/auxv` 初始栈构造能力
- 独立用户地址空间已接入；用户 CR3 复用内核高半区，低半区由 ELF loader 映射
- `process_execve()` / `process_spawn_first_user()` 已接入启动期用户进程启动；默认 Alpine tar.zst rootfs 会启动真实 `/bin/sh`
- ring3 `iretq` 已接入 task 调度；默认静态 ELF64 用户程序已能进入 ring3
<!-- 过时内容（2026-06-14 syscall stub 已删除非 canonical fallback）：- 最小 syscall ABI 已接入；`syscall` 入口和 `sysretq` 正常返回已通过默认用户程序验证，`RIP` 非 canonical 时保守 fallback 到 `iretq` -->
- 最小 syscall ABI 已接入；普通 `syscall` 返回热路径现在直接 `sysretq`，`rt_sigreturn` 经独立 `syscall_iret_return()` 慢出口 `iretq` 精确恢复 `rcx/r11` 等完整用户寄存器，SIGSEGV handler + `rt_sigreturn` smoke 已验证
- Alpine `/bin/sh` 已验证到交互 prompt，并能连续执行 `echo hi`、`/bin/busybox echo ext`、`ls /`、`pwd`、`ls /proc`、`readlink /proc/self/exe`、`cat /proc/{mounts,meminfo,cpuinfo}` 后返回 `/ #`
- 最小可写 VFS smoke 已通过：`printf/echo > /tmp/file`、`>>`、`cat`、`mkdir`、`rm`、`rmdir`、`mv`、`cat /proc/mounts`；`/proc/mounts` 现在标记 rootfs 为 `rw`
- 默认 Alpine rootfs 已用 SJTU Alpine 源安装 `coreutils 9.8-r1`、`bash 5.3.3-r1`、`gcc 15.2.0-r2`、`musl-dev 1.2.5-r23`、`tmux 3.6-r0` 后重新打包；`/etc/apk/repositories` 为 `https://mirror.sjtu.edu.cn/alpine/v3.23/{main,community}`，rootfs 中确认存在 `usr/bin/{cat,stat,sha256sum,sort,wc,gcc,cc,as,ld,tmux}`、`bin/bash` 和 `usr/include/stdio.h`
- GNU coreutils smoke 已通过：`printf`、`cat`、`wc`、`stat`、`realpath`、`sha256sum`、`sort`、`tr`、`truncate` 能在当前内核上运行到 `COREUTILS_DONE`，负向扫描无 `syscall unhandled`、`page fault`、`Function not implemented` 或动态链接错误
- bash/gcc smoke 已通过：`/bin/bash --version`、`/bin/bash -c` 变量/重定向、`/usr/bin/gcc /tmp/hello.c -o /tmp/hello`、`/tmp/hello` 能在当前内核上运行到 `hello-gcc` / `BASH_GCC_DONE`，gcc 会执行 `cc1`、`as`、`collect2`、`ld`，负向扫描为空；验证使用 `-m 1024M`
- 最小进程链路已接入：`fork`、受限 `vfork/clone`、用户态 `execve`、直接子进程 `wait4`、cwd 继承、fd 继承和 `FD_CLOEXEC` 清理
- P1/P2 小收尾已接入：浅 `madvise(2)`、浅 `prctl(PR_SET_NAME/PR_GET_NAME/PR_SET_DUMPABLE/PR_GET_DUMPABLE)`、最小 `statx(2)`、`chdir/fchdir/getcwd`、基本 mode bits、浅 `poll/ppoll/pselect6`、浅 `epoll`、最小 PTY、Ctrl+C、最小 AF_UNIX stream socket、浅 `SCM_RIGHTS` fd passing 和 `SO_PEERCRED`；BusyBox/GNU coreutils/bash/gcc 所需浅 `sendfile(2)`、`readv(2)`、`pipe/pipe2(2)`、`fadvise64(2)`、`getrlimit/prlimit64`、`sched_getaffinity`、`sysinfo`、`faccessat2`、`getresuid/getresgid`、`chmod`、`umask`、`getrusage` 也已接入；tmux detached smoke 已能创建会话并输出 `inside-tmux`，direct `tmux new` 已可进入界面，PTY CR/NL 输出转换修复后连续 Enter、`seq 1 80` 滚屏和 `tmux attach` 重绘历史不再让 prompt 阶梯式右移，`Ctrl-b d` 和 `tmux detach` 都可以 detach 回外层 shell，`tmux ls` 可返回会话状态
- 重复执行程序的主要资源泄漏已收敛：ELF loader 改为 header/phdr/segment 流式读取，不再把整个 ELF 文件放进 kernel heap；task exit/exec replace 会释放 VMA 外的 ELF/stack user leaf pages；短命 task 复用 kernel stack；ramfs 复用运行期临时节点，并通过 `krealloc()` 扩展自有文件 buffer。`/tmp/kernel64-buddy-slub.log` 已验证 50 次 gcc loop 不再 page fault；当前 `/proc/meminfo` heap 字段为 `KheapUsed/KheapMapped`
- 物理 frame 分配已改为带每 CPU order-0 pool 的 buddy，metadata 动态 sizing，不再有 `FRAME_MAX_FRAMES/FrameSkipped` 静态上限；`bootmem_alloc(size, align)`、`bootmem_alloc_page/free_page` 和旧 `kheap_mark/reset` 兼容接口已删除。连续物理页使用 `frame_alloc_contiguous()`，内核堆是带每 CPU 小对象 cache、可扩展虚拟 range node、可释放/复用虚拟 range、支持 page-aligned `kmemalign` 与通用 `krealloc()` 的 SLUB。`/tmp/kernel64-cleanup.log` 已验证 30 次 gcc loop、tmux detached smoke 和负向扫描通过；最终镜像已恢复默认 `/bin/sh` 并短跑进入 `/ #`
- 遗留 AHCI/legacy PCI/FatFs/diskio 已删除，新增 PCIe/NVMe 后 `/tmp/kernel64-nvme-smoke2.log` 已验证 `/dev/nvme0n1`、sector read/hexdump、30 次 gcc loop 和 tmux detached smoke；负向扫描无未处理 syscall、page fault、panic、`ENOMEM`、`can't fork`、TTY/PTY/tmux 错误、AHCI/legacy PCI 或 `/dev/sd0` 残留
<!-- 过时内容（旧 bootmem/kheap 统计和单全局锁 SLUB 说明）：
- 重复执行程序的主要资源泄漏已收敛：ELF loader 改为 header/phdr/segment 流式读取，不再把整个 ELF 文件放进 kheap；task exit/exec replace 会释放 VMA 外的 ELF/stack user leaf pages；短命 task 复用 kernel stack；ramfs 复用运行期临时节点和自有文件 buffer。`/tmp/kernel64-gcc-tmux.log` 已验证 30 次 gcc loop 不再 page fault，loop 前后 `BootmemUsed` 约从 219940 kB 到 223308 kB，`KheapUsed` 约从 212240 kB 到 215600 kB
- 内核堆已改为 SLUB：固定 size class 小对象 slab 和 large allocation 均可 `kfree()`，ZSTD/rootfs 失败路径会显式释放临时对象；`/tmp/kernel64-slub.log` 已验证 SLUB 下 30 次 gcc loop 和 tmux detached smoke 通过，loop 前后 `KheapMapped` 约从 212312 kB 到 216204 kB
-->
- task 层已有 MUQSS 风格 per-CPU deadline runqueue 和 `task_spawn_kernel()` / `task_spawn_kernel_on_cpu()` 普通内核任务入口；旧默认 kernel smoke/demo 已删除
<!-- 过时内容（2026-06-14 已替换为 MUQSS 风格 per-CPU deadline runqueue）：- task 层已有 tickless CFS/RT/root-domain 调度骨架和 `task_spawn_kernel()` / `task_spawn_kernel_on_cpu()` 内核任务入口；旧默认 kernel smoke/demo 已删除 -->
<!-- 过时内容：默认 RT/CFS kernel task smoke 和多核求和 demo 已删除。 -->
- 默认 `kernel64/build/kernel64-uefi.img` 当前包含 `kernel64/build/alpine-rootfs.tar.zst`
<!-- 过时内容（2026-06-13 pipe 已有最小 task blocking/wakeup；futex 和统一等待模型仍待补）：- 下一阶段最应该先收尾 tmux 控制 client 和 PTY/session/controlling tty 的长期交互细节：当前 `Ctrl-b d`、`tmux detach`、`tmux attach` 和滚屏 smoke 已可用；优先继续补多窗口/窗格、resize、Ctrl+C/job-control、完整 line discipline、foreground pgrp/session 细节，以及 AF_UNIX stream 多消息/SCM_RIGHTS/control-message readiness 的长期语义；然后补 tmpfs/嵌套挂载、真实 fd/device readiness、完整 job control、真实 pipe/futex wait/wake、线程/signal/权限语义，并继续整理当前 fork/COW 的引用回收技术债 -->
- 下一阶段最应该先收尾 tmux 控制 client 和 PTY/session/controlling tty 的长期交互细节：当前 `Ctrl-b d`、`tmux detach`、`tmux attach` 和滚屏 smoke 已可用；优先继续补多窗口/窗格、resize、Ctrl+C/job-control、完整 line discipline、foreground pgrp/session 细节，以及 AF_UNIX stream 多消息/SCM_RIGHTS/control-message readiness 的长期语义；然后补 tmpfs/嵌套挂载、真实 fd/device readiness、完整 job control、futex 和统一 wait/wake、pipe fairness/SIGPIPE、线程/signal/权限语义，并继续整理当前 fork/COW 的引用回收技术债
