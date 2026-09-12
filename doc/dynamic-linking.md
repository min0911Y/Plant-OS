# 用户态动态链接

Plant OS 的 ELF 动态链接器是独立程序 `/lib/ld.so`，源文件位于
`apps/ldso/`。i386 与 x86_64 共用装载、依赖、符号查找及生命周期实现，
分别处理 ELF32/REL 和 ELF64/RELA。它使用 Plant OS ABI，不能运行宿主 Linux
的 libc 或程序。

## 进程启动边界

内核在 `kernel/dos/task/execute.c` 中读取原生 ELF 的 `PT_INTERP`。
没有解释器的静态程序继续经架构 ELF loader 启动；有解释器时，内核只装载
该解释器，以正常 C 调用约定将 `loader_start_t *` 传给它的 `Main`。
结构定义在 `apps/include/loader.h`，包含结构大小、已打开的原始可执行文件
描述符、实际执行路径及解释器的带盘符路径。解释器从系统启动盘打开，
默认库目录取解释器所在目录，因此当前目录位于其他盘时也能执行系统程序。
命令行保持原样，因此 `argv[0]` 可以与文件名不同。
解释器描述符在装载结束后关闭，原程序描述符由用户态解释器读取并关闭；
任务退出也会关闭未完成装载的所有描述符。

`ld.so` 本身是无 `PT_INTERP`、无 `PT_DYNAMIC` 的静态 ELF，不需要自举
重定位或另一份链接器。它使用独立匿名映射保存元数据，不初始化或占用应用
的 malloc arena。随后由它装载 PIE 和依赖库，完成重定位、设置页面权限，
最后调用程序的 `Main(runtime_linker_t *)`。`libp` 先建立当前线程的 TLS，
再初始化分配器、stdio 和命令行，随后调用链接器的初始化钩子；普通 `exit()` 调用析构钩子。
内核不搜索共享库、不解释动态符号，也不执行用户 ELF 重定位。

`init`、shell 和普通应用使用同一启动路径。内核不再缓存静态 shell 镜像或维护
专用 shell loader。`system()` 使用 `EXECUTE_COMMAND`，在正常退出时把 cwd
返回调用者；普通 `exec()` 保持目录隔离。该状态由任务内部的 `return_cwd`
记录，fork 清除它，不再通过可写的用户状态页控制退出行为。

普通执行从内核模板建立新的用户地址空间，只有 fork 复制父进程的用户映射。
启动参数及输入队列在发布任务前准备，任务持有它们直到退出，调用线程提前退出
不会遗留分配。初始用户栈和堆以共享零页保留虚拟空间，首次写入时按 COW 分配
物理页；匿名 VM 区域也使用同一共享零页和写时分配机制。

## 已实现的 ELF 行为

- 原生 `ET_DYN` PIE 与共享库；按 program headers 装载、清零 BSS，无需
  section headers。校验文件范围、地址溢出、对齐、页重叠、可执行入口、
  字符串和动态表范围。装载地址由 VM 在空闲地址空间内选择。
- `DT_NEEDED` 依赖图、SONAME/规范路径去重，以及广度优先的全局符号范围。
  支持 SysV 和 GNU hash，包括只有导入符号的空 GNU hash。
- 全局/弱符号、未定义弱符号、单一全局范围内的 GNU UNIQUE、符号覆盖、hidden/protected visibility 和
  `DT_SYMBOLIC`。所有 PLT 槽都在进入程序前解析，不依赖延迟绑定 trampoline。
- REL/RELA 中的相对、绝对、PC-relative、GLOB_DAT、JUMP_SLOT 和 COPY
  重定位；x86_64 的 32-bit 结果检查溢出。COPY 在其他重定位完成后执行。
- `PT_TLS`、STT_TLS 和 x86 的 DTPMOD/DTPOFF/TPOFF 重定位。每个线程拥有
  独立 TLS 模板副本，初始化数据和 TBSS 按 ELF 对齐布局。
- `DT_RUNPATH` 的直接依赖搜索、无 RUNPATH 时沿装载祖先搜索 `DT_RPATH`、
  `$ORIGIN`/`${ORIGIN}`，最后搜索系统解释器所在的 `lib` 目录。包含路径分隔符的 NEEDED 项直接
  按路径查找；空搜索项表示当前目录。
- 主程序 PREINIT_ARRAY、依赖先于使用者的 INIT/INIT_ARRAY，逆序
  FINI_ARRAY/FINI。图遍历使用显式栈，循环依赖不会无限递归，初始化一次。
- 重定位完成后应用 LOAD 权限及 GNU RELRO。x86_64 同时执行写保护和 NX；
  当前非 PAE i386 没有 NX，但执行只读写保护。两种架构都拒绝 W+X LOAD、
  文本重定位和可执行栈。GNU RELRO 可以覆盖 LOAD 末尾的页内零填充；
  校验基于实际 LOAD 页面，不能误拒绝 GNU ld 生成的这种合法布局。

当前仅接收 PIE 动态主程序，普通固定地址 `ET_EXEC` 主程序仍采用静态链接。
支持 `dlopen(NULL)`、`RTLD_DEFAULT`、`dlsym`、`dladdr` 和对应的 `dlerror`/`dlclose`；
查找 TLS 符号返回当前线程的地址。非 NULL 路径的 `dlopen`、IFUNC、符号版本、
RELR、`LD_PRELOAD` 和 `LD_LIBRARY_PATH` 尚未实现。不支持的 ELF 元数据会明确失败，
不会假装已完成链接。用户态装载错误打印 `ld.so:` 诊断并退出 `127`；
内核无法打开解释器或解析启动 ELF 时返回 `-1`。

## 构建与使用

当前全部正式应用（具体清单由 `make -C apps ARCH=... list-apps` 生成）均为
`ET_DYN` PIE，带 `/lib/ld.so` 的 `PT_INTERP`，通过 `DT_NEEDED` 使用
`libp.so`。C++ 应用另导入 `libcpp.so`，C/C++ 都使用 `libp/entry.c` 的唯一
`Main` 包装；C++ 支持来自原生 libc++/libc++abi，禁用异常和 RTTI，
不再使用旧的手写 ABI 或 GNU C++ 头文件副本。`ld.so` 自身保持静态自举。

```sh
make -C apps ARCH=i386 -j8
make -C apps ARCH=x86_64 -j8
make -C apps/psh ARCH=i386
make -C apps/gui ARCH=x86_64
```

两种架构共用 `apps/build.mk` 的编译、链接与依赖规则，`apps/native-apps.mk`
记录应用源文件和私有库依赖。子目录 Makefile 只选择逻辑目标并转发到同一
依赖图。新增应用通过 `application` 宏注册，实现通常的 `main(argc, argv)`，
不手写固定地址链接参数。C++ 源文件会自动引入对应共享运行库。

系统调用表由 `libp/arch/syscalls.inc` 在两种架构上展开；i386 包装完整保存
PIC 的 GOT 寄存器及其他 callee-saved 寄存器。运行库的有符号 64 位除法辅助
函数同样遵守商向零截断、余数随被除数符号的 ABI。

运行库和解释器构建由 `apps/dynamic.mk` 提供，单独构建可用
`make -C apps -f dynamic.mk ARCH=i386 dynamic -j8`。i386 运行时产物为
`apps/out/lib/{ld.so,libp.so,libcpp.so}`；x86_64 位于
`apps/out/x86_64/lib/`。所有应用及应用私有的 SDL、MST 等归档都使用 PIC，
应用只包含所用私有库代码，公共 C/C++ 运行库经 DSO 导入。对象不写回第三方
源码目录，也不与另一架构复用。`-z text` 把文本重定位视为链接失败。

`apps/out[/x86_64]/applications.list` 直接从构建依赖图生成；LiveCD 按这份
清单收录应用并整体复制 `lib/`，不会把遗留输出当作有效程序。i386 磁盘镜像
也包含动态运行库。镜像中的 `apps.lst` 由 `mshortname` 记录每个应用的真实
FAT 路径，包括 `/games/doom.bin` 与长文件名别名，供逐项装载验证使用；
安装清单继续从镜像内容自动生成。

i386 的 `sdk` 目标额外生成 TCC 使用的 `libp.a`、`libcpps.a`、`libabi.a`、
`crti.obj`，以及内核使用的独立非 PIC `libtcc1.a`；这些是编译器 SDK 归档，
正常应用链接规则不使用它们。SDK 在 `out/sdk/` 单独编译非 PIC 对象，
不复用动态运行库的 PIC 归档。TCC 预定义整数/指针位宽、cdecl 可变参数和
ABI 断言所需的名字，可用当前基础 C 头文件编译、链接并运行程序。
TCC 自身已动态链接，其现有代码生成器仍按原来的
固定地址静态 ABI 生成新程序；`crti.c` 完整转发入口参数。

## 虚拟内存 ABI

`apps/include/vm.h` 提供 `vm_map`、`vm_map_aligned`、`vm_map_file`、
`vm_protect`、`vm_discard`、`vm_sync` 和 `vm_unmap`，统一经过
`SYSCALL_VM` (`0x66`)。请求包含地址、长度、对齐、权限、标志、文件偏移和
descriptor，i386 为 36 字节，x86_64 为 48 字节，两侧共用声明和大小断言。
`vm_map` 默认 RW；`vm_map_aligned` 接受不小于页大小的 2 次幂对齐，指定地址
默认不覆盖，显式 `VM_REPLACE` 才替换整个范围。失败映射返回 NULL，其他操作
返回 -1，并设置 errno。

`sys/mman.h` 支持匿名和文件映射、`MAP_PRIVATE` 与 `MAP_SHARED`、
`PROT_NONE`/R/RW/RX、`MAP_FIXED`、`MAP_FIXED_NOREPLACE`、`MADV_DONTNEED`
和 `msync`；普通地址是可回退的 hint。始终拒绝 W+X。非 PAE i386 没有 NX，
不能硬件禁止可读页执行；`PROT_NONE` 在两个架构都禁止读、写和执行。

HotSpot 平台层可按以下生命周期使用：

- 预留：`mmap(..., PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`；
  需要大于页的对齐时用 `vm_map_aligned`，或额外预留后释放两端。
- 提交：`mprotect(..., PROT_READ | PROT_WRITE)`。物理页在首次写入时分配；
  这不是保证未来写入一定成功的物理内存承诺，耗尽时仍终止进程。
- 保护页：`mprotect(..., PROT_NONE)`，保留原有内容，恢复权限后内容仍在。
- 撤销提交：用匿名 `MAP_FIXED | PROT_NONE` 原位替换，保留地址并释放内容。
  `madvise(..., MADV_DONTNEED)` 则保留每页当前权限，丢弃内容；再次读取为零。
  不可读的页没有可保留的权限语义，因此拒绝而不是静默改变权限。
- 代码缓存：RW 写入后改成 RX，再发布入口；修改前解除执行权限。
- 释放：`munmap` 接受部分范围和空洞，释放预留地址及其内容。

VM 自上向下查找满足对齐的空闲区，同一进程线程共享查找游标；heap 自下向上
增长并检查冲突。页表兼作地址分配登记：用户叶 PTE 的 U 位表示地址被占用，
P 位单独表示可访问；非 present 用户叶仍持有物理页引用。预留和未写匿名页
引用永久共享零页，仅消耗页表空间，零页引用不随预留页数累加。
fork、exec、退出和线程托管区域回收均保留这一约定。

保护及替换先验证整个范围、准备私有页表，再改变叶映射，避免失败留下部分权限
或丢失原内容；分配失败须回收新建的空页表，包括未完成的中间层。
x86_64 替换/撤销提交必须在同步 TLB shootdown 完成后释放旧页；
空页表分支须先从上级表摘除，再同步失效分页结构缓存，最后回收页表页。
i386 沿用同一地址空间固定 CPU 的约束。GUI 共享页禁止保护、丢弃或原位替换，
创建者仍可解除自己的映射，物理页等最后一个引用释放后回收。

文件范围遍历须区分匿名空隙和文件区间；私有文件页 discard 按当前权限的连续
区间恢复 backing，不能把首个页面的权限扩展到整个 extent。
文件映射单独登记：匿名映射仍只由页表表示，文件映射在全局 extent 链表中按
`address_space` 记录地址、长度、起始页和 backing。backing 持有 open-file 引用
和固定的 VFS cache 页数组，因此映射既不消耗也不移动 descriptor offset，关闭
descriptor、甚至 unlink 之后仍然有效。私有映射以同一 cache 页作为 COW 来源，
共享映射直接映射同一批页；普通 read/write 与该 cache 共用，映射写和文件 I/O
互相可见。共享可写映射经 `msync`/`fsync` 回写有效文件字节，末页 EOF 之后不写。
`MAP_SHARED | MAP_ANONYMOUS` 明确不支持。为避免映射 straddle 到文件末尾之外
留下未定义内容，范围必须落在“文件长度向上取整到页”之内，否则返回 ENOTSUP；
有映射时 `ftruncate`/`O_TRUNC` 返回 EBUSY。文件页当前在映射时预读并 pin，不是
按需缺页装载。`MS_ASYNC` 与 `MS_INVALIDATE` 目前和 `MS_SYNC` 一样同步完成。

`--dynamic` 中的 `DYNTEST MMAN PASS` 覆盖保护页读/写/执行故障、fork 后隐藏内容
和 COW、丢弃后的零填充、含空洞的替换/释放、溢出和对齐，以及 x86_64 4 GiB /
i386 64 MiB 预留的物理页开销；`DYNTEST FILEMAP PASS` 覆盖共享/私有映射、
fd offset、COW、alias 一致性、普通 I/O 可见性、私有丢弃、fork、只读 fd 权限、
EOF 边界、msync/fsync、部分取消映射、close/exec/unlink 生命周期。
`--threads` 还运行 libc 保护页边界和并发回归。

## TLS、线程与运行库生命周期

TLS 使用 x86 ELF variant II：模块存储位于 TCB 前方，TCB 包含自身指针、模块
地址表和原生线程运行时。x86_64 经 FS base 访问，i386 使用每 CPU 的 GS 描述符。
内核在线程切换和用户返回路径中保存、恢复线程指针；`__tls_get_addr` 从当前
TCB 查找模块。所有依赖在进入 main 前装载，没有固定大小的动态 TLS 余量。

`runtime_linker_t` 统一提供初始化/析构、TLS 分配、符号与地址查询，以及可执行
文件的规范路径。每个 ELF 对象链接独立的 hidden `__dso_handle`。C++ 全局对象、
局部静态对象和 thread_local 析构使用同一套原生运行库。

线程创建、join/detach、栈和 TLS 映射的托管由原生 syscall `0x68` 完成；
pthread mutex/condvar/rwlock/barrier/once 使用进程私有 futex `0x67`。
errno、locale、TSS 与浮点环境按线程隔离。fork 协调分配器、环境、退出回调、
TSS 和 stdio 锁，子进程更新自身线程身份。普通 `exit` 执行回调和析构，
`_Exit`/`abort` 终止整个进程组；低层 `_exit` 保留线程退出入口的语义。

`setenv`/`unsetenv` 管理进程内覆盖值，fork 复制覆盖值；普通 exec 当前从系统
配置获取环境，不继承这些覆盖。线程及运行库回归使用 `--threads`，渲染依赖与
具体支持边界见 [lavapipe](lavapipe.md)。

## 验证

```sh
python3 scripts/test-x86_64.py --dynamic
python3 scripts/test-x86_64.py --arch i386 --dynamic --memory 512
python3 scripts/test-x86_64.py --all-apps --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --all-apps --memory 512
python3 scripts/test-x86_64.py --threads --memory 1024
python3 scripts/test-x86_64.py --threads --arch i386 --memory 512
```

默认完整回归也会运行 `dyntest.bin`。它覆盖跨库调用、两种哈希表、符号覆盖、
弱符号、带空格和空参数的 argv、构造/析构顺序、fork/COW、RELRO、VM 页权限、
创建线程退出后的映射存活、缺库/未定义符号，以及损坏的 LOAD、动态表、
重定位长度、ELF machine 和 program header。宿主脚本另行核对串口中的析构
次序。`--dynamic` 和 `--all-apps` 在命令结束后执行 `shutdown`，宿主确认
ACPI S5 并核验结果后退出；超时仅用于检测启动或测试卡死。
测试只临时修改 `kernel/res/init.mst`，结束后恢复脚本及正常启动镜像。

`--all-apps` 先核对清单中的 ELF class/machine、PIE、PT_INTERP 与 DT_NEEDED，
再通过 `dyntest --all` 逐项调用 `/lib/ld.so --verify`，实际完成依赖装载、符号
解析、重定位及权限设置，不执行被检查应用的构造函数或 main。负例 `dynbad`
必须返回 127，其他应用必须成功。该模式也验证跨盘运行、system 的 cwd 返回
和普通 exec 的 cwd 隔离；正常完整回归继续执行实际程序功能。
