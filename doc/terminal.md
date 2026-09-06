# GUI 终端

GUI 启动时和点击工具箱的 `Terminal` 按钮时运行独立的 `term.bin`。term 是普通的 GUI RPC 客户端，默认启动 `psh.bin`，也支持 `term.bin <program> [arguments...]`，参数通过公共命令行构造器传递。

## 构建依赖

应用全部使用 C，直接链接已有的 os-terminal 静态库，不构建或修改该库。默认目录为 `~/os-terminal`，可以通过 `OS_TERMINAL_DIR` 覆盖：

| 文件 | 用途 |
| --- | --- |
| `os_terminal.h` | C 接口，`terminal_process` 接收指针和字节长度 |
| `libos_terminal_x86.a` | i386 PIC 静态库 |
| `libos_terminal_x64.a` | x86_64 PIC 静态库 |

```sh
make -C apps/term ARCH=i386
make -C apps/term ARCH=x86_64 OS_TERMINAL_DIR=/path/to/os-terminal
```

i386 库的浮点辅助调用通过 C 编写的私有适配层接入 x87；其返回值使用 EAX/EDX，适配层单独使用 `-mno-fp-ret-in-387`，不改变 libp 或其他应用的 ABI。两种架构的 term 都是加载 `/lib/ld.so`、依赖 `libp.so` 的原生 PIE。应用清单自动包含 term，i386 磁盘镜像也显式收录它。

## 输出与输入

term 的主任务注册 fartty RPC handler 并分配 TTY，工作线程绑定该 TTY 后同步执行 shell。子进程的标准输出、标准错误以及 TTY 操作通过 fartty 回到 term；主任务保留自己的 TTY，避免服务调用自身。

fartty 设置 `native_ansi`，`print` 的批量输出和 `putch` 的单字节输出都不经过内核旧 VT100 解析器。os-terminal 负责 ANSI、颜色、备用屏幕、自动换行、光标和回滚历史。传统光标、清屏、滚屏和区域填色 RPC 由 term 转成 ANSI 操作。

提供的 C 接口没有光标 getter。term 使用 os-terminal 的光标报告同步 fartty 状态，只在完整控制序列和 UTF-8 字符之后查询；边界跟踪不解释控制参数，不缓存或改写应用输出。跨 RPC 消息、逐字符写入的转义序列和控制字符串始终原样交给 os-terminal。

`auto_flush` 始终关闭。RPC handler 只处理输出、标记需要重绘并应答；主循环按 16 ms 帧间隔调用 `terminal_flush()`，再用 `window_present()` 提交画面。等待提交完成后才继续复用绘图缓冲。窗口内容使用 GUI 的 RGB 像素布局和窗口行距，显示设备的颜色转换归 GUI 合成器处理。

`window_set_event_notifications()` 可让窗口事件通过 `GUI_RPC_EVENT_READY` 通知唤醒 `rpc_serve_once()`。term 订阅此通知；空闲时阻塞，有待提交画面时才使用 16 ms 帧间隔。shell 工作线程退出也通知主循环。

键盘沿用 Plant OS 的逻辑键码和 `getch` ABI。GUI 将按键写入窗口共享队列，term 用该队列回答 fartty 输入读取，并调用 `tty_notify_input` 唤醒等待者；不把 os-terminal 的 ANSI 键盘输出当成 PS/2 字节。滚轮交给 os-terminal 的历史滚动接口。

关闭窗口时释放 TTY，终止其 shell 会话和工作线程，再释放线程栈、终端对象和窗口。shell 自然退出也走同一回收路径，GUI 按启动线程的 TID/generation 确認其退出后再释放用户栈，不会误杀复用同一编号的新线程。

## 资源与容量

图层对象独立分配，显示顺序数组动态增长，像素归属保存图层指针。合成从前向后确定可见像素，完全遮住的窗口不参与后续合成，不受 8 位图层编号限制。

GUI 的共享绘图缓冲和已提交画面使用独立 `vm_map()` 区域；关闭窗口时解除映射，物理页按引用计数释放。GUI 定期检查 owner 的 TID/generation，清理异常退出客户端的窗口；其他地址空间尚持有的共享页在最后一个引用释放前不会复用。

每个任务使用 64 KiB 内核栈，切离该栈后才回收。两种架构保留 2 MiB 用户栈和 2 MiB 初始堆的虚拟空间，通过只读共享零页及 COW 按需提供物理页。libp 扩容只申请现有堆空间的不足部分；分配时仅清零复用过的区间与空闲链表元数据，保持尚未使用的零页共享。普通执行创建新的用户地址空间；启动参数、输入队列及线程启动请求均在发布前准备并由新任务持有，失败统一回滚。

窗口数最终受可用内存约束。os-terminal 每个实例申请约 2 MiB 内部容量，未触及的初始堆页面保持共享，默认窗口的两份像素缓冲另需约 2.1 MiB；扩大窗口或运行更多应用会增加用量。

## 验证

```sh
python3 scripts/test-x86_64.py --console --firmware bios --memory 1024
python3 scripts/test-x86_64.py --console --firmware uefi --memory 1024
python3 scripts/test-x86_64.py --arch i386 --console --memory 512
python3 scripts/test-x86_64.py --terminal-load 1000 --memory 4096 --timeout 900
python3 scripts/test-x86_64.py --memory-pressure --memory 512
python3 scripts/test-x86_64.py --arch i386 --memory-pressure --memory 512
```

`guitest.bin terminal` 在外部 term 中运行 TTY 测试，覆盖逐字节 ANSI、控制字符串、备用屏幕、光标位置、清屏、区域填色、自动换行和滚屏。宿主的 `--console` 继续通过真实键鼠事件和连续像素比较验证默认 psh 的提示符、回显、退格、滚屏、滚轮历史回看以及多次关闭和重启；测试命令只由临时 `init.mst` 选择。

`rpctest.bin` 的 `tty_rpc` 项另外验证原始 ANSI 字节没有被内核截获，且输入通知竞态、RPC 超时、迟到应答和会话回收仍然成立。

`--terminal-load COUNT` 持续保留所有成功启动的 term，记录终端、shell、任务及物理页数量，并验证全部创建完成后仍能运行 `timetest.bin`，最后通过真实键盘输入和像素比较检查最上层终端。它不关闭或重启窗口。`--memory-pressure` 留下不足以完成线程创建的物理页，验证 64 次失败申请不增加已用页数，同时验证耗尽内存时 COW 失败只终止测试子进程；释放压力后再次创建线程和运行程序。
