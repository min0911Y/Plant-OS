# 管道与事件等待

[开发指南](development.md) · [并发约束](concurrency.md)

`pipe`/`pipe2` 创建 VFS 描述符，读写端共享内核环形缓冲，支持 fork 继承、
非阻塞标志、EOF、FIONREAD、fstat 和关闭清理。小于等于 4096 字节的单次 write
保持原子性；缓冲不足时阻塞或返回 EAGAIN。最后一个写端关闭后，读端先消费剩余
数据再返回 EOF；最后一个读端关闭后，write 返回 EPIPE 并投递 SIGPIPE。
FD_CLOEXEC 属于描述符，O_NONBLOCK 属于打开的文件描述，fork 后按各自语义处理。
普通 exec 当前仍创建新的描述符表，不提供 POSIX exec/dup 的完整接口。

`poll` 使用统一 VFS syscall，可同时等待文件、管道和原生 socket。普通文件
立即就绪；负描述符忽略，无效描述符产生 POLLNVAL。零超时查询即返回，正超时
使用绝对单调 deadline，负超时无限等待。管道报告数据、空间、HUP/ERR；socket
依据监听队列、接收队列、发送空间、连接完成及错误状态报告事件，不能把连接中
误报为已可写。现有控制台输入没有可等待的字节流接口，对 stdin 返回 POLLERR；
stdout/stderr 可写。不实现 POLLPRI/OOB，也不提供 epoll/kqueue。

内核 `io_poll.c` 为一次等待保存描述符快照及资源订阅，管道和 socket 各自维护
等待队列。就绪检查与订阅、阻塞原子进行，资源事件仅唤醒其订阅者；不轮询 sleep，
不为每次 I/O 扫描全部任务。定时器只处理有限 deadline，信号使 poll/管道等待返回
EINTR。关闭资源先摘除订阅；任务终止清理等待记录及其内存，避免悬挂内核栈或资源指针。
返回结果以及阻塞读写重试前重新校验用户内存。

```sh
python3 scripts/test-x86_64.py --dynamic --memory 3072
python3 scripts/test-x86_64.py --arch i386 --dynamic --memory 512
```

`IOPOLL PASS` 覆盖超时、混合描述符、管道背压/EOF、fork 共享及描述符标志隔离、
SIGPIPE/EINTR、跨线程唤醒、socketpair 独立端点、socket 关闭与连接失败事件。
真实 Java Selector 验证见 [OpenJDK](openjdk.md#nio-与动态加载回归)。
