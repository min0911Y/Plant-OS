# USB / xHCI

Plant OS 的 i386 和 x86_64 共用 USB 栈。xHCI 控制器在
`kernel/drivers/xhci.c`，枚举、接口绑定和请求队列在 `usb.c`；
`usb_hub.c`、`usb_hid.c` 和 `usb_storage.c` 通过 `usb_host_ops_t` 访问端点。

## 支持范围

- xHCI 1.x：固件接管、复位、scratchpad/DCBAA、命令/事件/传输环、
  32/64-byte context、AC64、Supported Protocol/PSI、MSI-X/MSI、共享 INTx、端口拔插。
- 控制、bulk、interrupt 端点；第一个 configuration 的 alternate setting 0。
- USB 1.x/2.0 Hub：描述符、端口供电/PowerGood、状态变化中断、消抖/复位、
  多级下游枚举、xHCI Route String/TT 和断开后的递归回收。
- HID 键盘与相对鼠标：Report ID、Usage、数组及位图按键、有符号坐标、
  修饰键、方向键、滚轮、重复输入和拔出后的按键/按钮释放。
- BOT/SCSI U 盘：多个 LUN、容量查询、READ/WRITE(10/16)、同步缓存、
  STALL 恢复、64 位容量和设备 LBA；较大的原生块通过读改写接入系统的
  512-byte 逻辑扇区。支持无分区介质和 MBR 主分区，并自动尝试挂载既有
  FAT12/16/32、PFS 文件系统。

尚未实现 SuperSpeed Hub 分支、UAS、绝对坐标 HID、GPT/扩展分区及
exFAT/NTFS。没有对应文件系统的介质会注册为未格式化的数据盘，驱动不会
自动格式化。`enumerated` 表示描述符读取完成，`usb-hid: ... active` 和
`usb-storage: ... mounted` 才表示相应功能已接入。

## 真机与 PCIe

xHCI 能管理连接到自身端口的 USB 1.x、2.0 和 3.x 设备；只有
EHCI/UHCI/OHCI 的旧控制器仍需自己的后端。主板通常已经内置 xHCI，
不需要额外购买 PCIe USB 卡。驱动需要通过 PCI/PCIe 配置空间发现设备、
取得 BAR 并启用 bus mastering，仓库已有这套基础枚举；无需在软件中实现
PCIe 链路层。

USB 3.x 插口同时提供 USB 2.0 信号。普通键鼠换到这种插口后仍以低速或
全速工作；主板内置的 Hub 也可能位于插口与 xHCI 之间，即使没有外接扩展坞。
之前的驱动只枚举根端口，遇到 Hub 打印 `hub unsupported` 后停止向下查找，
因而可能出现 USB 2.0 插口可用、另一组插口键鼠失效的现象。现在会枚举
Hub 的 USB 2.0 分支；USB 3.x Hub 中独立的 SuperSpeed 分支仍不支持。
这条缺失路径已在 QEMU 复现并修复；用户已确认新版在 270KP 原先失效的
USB 3.x 插口上可以使用同一套键鼠，具体内部拓扑尚未通过真机日志核对。

PCI 配置访问优先采用 ACPI MCFG 描述的 ECAM 窗口，保留 segment/bus/device/
function 完整地址；没有对应 ECAM 的 segment 0 使用 CF8/CFC。MCFG 给出
配置空间覆盖范围，并不列出其中的独立根桥。枚举覆盖这些范围内尚未访问的
总线，再按已配置桥及 multifunction 信息继续，每条总线只访问一次，空
slot 只探测 function 0。只从 StartBus 沿桥遍历会漏掉范围中间的独立根，
例如某些平台上的 bus 0x80；驱动不会硬编码这个总线号。MCFG 的原始基址
以 bus 0 为基准，非零 StartBus 只限制可访问范围。固件仍在准备设备而返回
PCIe CRS Vendor ID `0001` 时，最多等待 1 秒重试。i386 会保留已映射的
ACPI/设备页，避免按可用 RAM 上限裁剪恒等映射时丢失顶部保留区中的固件表。

屏幕上只出现 `8086:7ec0`（Meteor Lake 雷电 USB 控制器）且根端口 `0/3`
时，只能确认该控制器没有连接设备，不能据此认定 USB-A 键盘损坏。应查看
PCI 配置访问方式，以及主板的另一个 USB 控制器是否被枚举。诊断版显示
MCFG segment/总线范围、访问方式与根端口的 PORTSC、连接、供电及链路状态。

PCI 层优先选择 MSI-X，其次 MSI，最后使用有效的共享 INTx 路由。
固件把 PCI Interrupt Line 留为 `255` 时，驱动仍可通过消息中断工作。
每个 xHCI 只使用 primary interrupter：MSI 配置一个消息，MSI-X 只开启
entry 0，其他条目保持屏蔽。两种 x86 架构共用向量分配与 PCI capability
配置，设备停止后关闭中断源并释放 handler；MSI-X 表须位于合法 BAR 内，
MSI 的 32/64-bit 布局和可选 mask 寄存器均按 capability 处理。

消息通过 Local APIC 投递到 BSP，适用于 xAPIC 和 x2APIC。当前未实现
interrupt remapping，因此目标 APIC ID 必须能以标准 MSI 的 8-bit physical
地址表达；超出范围会拒绝配置，不能截断地址。当前验证平台是 QEMU，
固件/芯片组端口路由和电源管理仍需真机验证。USB 盘作为数据盘使用，
现有 Limine + initramfs 启动流程保持独立。

## 输入与 I/O 生命周期

控制器命令与每个端点有独立的完成状态。USB service task 负责枚举、端点
配置、SCSI 和同步请求；HID interrupt 请求长期挂起，由 IRQ 消费已解析的
报告并重新提交。IRQ 不分配、不等待、不解析描述符，正常构建的纯输入不会唤醒空闲的
USB service task。命令/控制传输等待 1 秒，bulk 等待 5 秒，均通过中断及
一次性 timer 阻塞，不轮询数据完成。

根端口和 Hub 子端口共用 `usb_port_t`，下游设备使用同一套 xHCI 构造和释放
流程。Hub IRQ 只合并端口变化，service task 执行状态查询、确认变化、供电、
100ms 消抖及有界复位；空闲时不轮询 Hub。Route String 保留五级路径，低/全速
子设备的 TT 配置来自最近的高速祖先；中间经过全速 Hub 时仍保留正确的 TT
端口。连接检查包含祖先，断开 Hub 时先释放子设备，再禁用并释放父 Hub。

同步命令返回不会覆盖后台 GUI 的输入所有权；只有调用者自己的鼠标所有权
会随前台子进程交接。

PS/2 和 USB 共用 `kernel/io/input_device.c`。键盘先转换为逻辑键码，再
投递给唯一输入 owner 或前台 TTY；各设备状态按引用合并。鼠标使用
`mouse_read(mouse_event_t *)` 一次交付 16-byte 的 x/y/wheel/buttons
事件，GUI 不再解码 PS/2 数据。该接口替换了原来的 `mouse_dat_get`。

TRB 先填写数据再交出 cycle ownership，控制请求最后发布 Setup TRB。
EP0 缓冲按需增长，bulk 缓冲满足 64 KiB 边界。断开先停止类驱动输入并回收后代，再禁用 slot、释放
context、ring 和描述符；不能确认控制器 halt 时关闭 bus master
并隔离仍可能被 DMA 引用的页。

磁盘请求拥有自己的缓冲和引用，等待者退出会清理请求。USB 自己串行化
BOT，通用磁盘层不再给它叠加同一个等待队列。底层失败会传递到 VFS，
失效设备不再返回缓存数据；有旧挂载引用的盘符不能立即复用，重新插入的
介质不会被旧文件句柄访问。文件同步操作继续下传 SCSI SYNCHRONIZE CACHE。

## 无串口的屏幕诊断

正常交付使用 `make -C kernel ARCH=x86_64 USB_DEBUG=0 livecd`，输出
`kernel/plant-os-x86_64.iso`；默认关闭 USB 屏幕日志、端口状态转储和 HID
报告追踪，串口保留设备生命周期与错误信息。只有排查故障时才开启下面的诊断构建。

```sh
make -C kernel ARCH=x86_64 USB_DEBUG=1 livecd
```

`USB_DEBUG` 默认关闭，并纳入两种架构的构建指纹。开启后，USB 日志同时
写入串口与屏幕，进入 shell 前保留启动画面。诊断包含 PCI segment/BDF、设备 ID、
IRQ、BAR、固件所有权、Supported Protocol、连接端口、失败阶段和 HID 绑定。
`IRQ ... command self-test passed` 表示控制器的命令完成确实经中断返回。
超时输出 `cmd/sts/iman/event/cycle`，用于区分控制器没有执行和完成事件
没有送达 IRQ；不会轮询消费事件来掩盖中断问题。

直接插入键盘，进入 shell 后按下并松开一次 A，再拍摄 USB 日志。
每个 HID 接口最多输出前 8 次 `usb-input` 摘要：`ptr` 表示含指针字段，
`len/need` 是实际/所需字节数，`ok` 表示报告长度和 ID 有效，`keys` 是按键状态变化数量，`wake`
表示输入投递请求了调度。摘要不含输入字符、按键值或原始报告。IRQ 仅向
固定队列复制摘要，USB service task 在任务上下文打印；正常构建不包含
这条诊断路径。拔插键盘会重新建立接口并重新计数。

PCI 探测行的 `irq=255` 只是固件没有给出 INTx 线号；后续
`transport=msix` 或 `transport=msi` 才是驱动实际选择的传输方式，`irq` 是
内核分配的消息 IRQ 标识。若显示 `PCI interrupt setup (MSI-X/MSI/INTx)`
失败，则三种方式均不可用。`usb-hub: ... active` 表示 Hub 已启用状态端点，
后续 `status/changes` 显示子端口状态，设备枚举行的 `route/tt` 显示主控路由。
`unsupported hub speed=4` 表示当前尚未支持的 SuperSpeed Hub 分支；
USB 2.0 分支会独立枚举。仍不能把 Hub 或设备描述符枚举成功当作键盘可用。

诊断回归使用 `python3 scripts/test-x86_64.py --usb --usb-debug`，可组合
`--firmware uefi`、`--apic x2apic` 或 `--arch i386 --memory 512`。脚本结束后
恢复普通启动脚本，交付镜像仍保留所选的诊断开关。

## 验证

```sh
python3 scripts/test-x86_64.py --usb --firmware bios
python3 scripts/test-x86_64.py --usb --usb-hubs --machine q35 --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --usb --usb-hubs --machine q35 --memory 512
python3 scripts/test-x86_64.py --usb --firmware uefi --memory 6144
python3 scripts/test-x86_64.py --arch i386 --usb --memory 512
python3 scripts/test-x86_64.py --usb --usb-irq msix --usb-no-intx --firmware uefi
python3 scripts/test-x86_64.py --usb --usb-irq msi --usb-no-intx --firmware bios
python3 scripts/test-x86_64.py --usb --usb-irq intx --firmware bios
python3 scripts/test-x86_64.py --usb --machine q35 --firmware uefi --usb-no-intx
python3 scripts/test-x86_64.py --arch i386 --usb --machine q35 --memory 512
python3 scripts/test-x86_64.py --usb --machine q35 --usb-root-bus 0x80 --usb-no-intx --firmware uefi
```

脚本禁用 QEMU 的 i8042，确保输入来自 USB。两个 xHCI 连接 USB 键鼠、
用于 EP0 回归的音频设备，以及三个临时 FAT 盘：MBR、无分区和 4 KiB
原生块。`--usb-irq intx` 的完整网络测试覆盖网卡与 USB 共用 INTx。

`--usb-irq` 通过 QEMU 的 MSI/MSI-X capability 开关选择待验证路径，并核对
内核实际采用的模式。`--usb-no-intx` 使用 QEMU 自带的 GDB/qtest 接口，
在 xHCI 初始化前将两个控制器的真实 PCI Interrupt Line 改为 `255`，
读回确认后恢复运行；测试期间的命令与输入均通过正常中断完成，不增加
轮询兜底，也不修改内核的 PCI 查询结果。

`--usb-root-bus` 使用 QEMU PCIe expander 建立独立根桥，并把两个控制器放在
两个 root port 后。测试检查它们确实位于独立根下，且内核输出了相应的
`ECAM discovery entry`。原先只从 bus 0 遍历的镜像在这个场景中会进入 shell
但报告 `PCI USB controllers=0`；此回归直接覆盖该漏扫问题。

`--usb-hubs` 将键鼠放在两级启用供电控制的 Hub 后面，上游 xHCI 同时提供
USB 2.0/3.x 端口。除原有输入、存储和连续拔插测试外，还整体移除根 Hub
及其后代，再重建并检查两个 Hub 和键鼠的路由。QEMU Hub 仅支持 full-speed；
高速 Hub 的 TT 字段通过宿主 ASan/UBSan 测试检查，实际 split transaction
仍需真机验证。宿主测试还覆盖截断描述符、255 端口边界、供电、祖先断开、
复位超时与过流，不能用这些测试替代真实链路验证。

`usbtest.bin` 写入并读回数据，检查非对齐写入、fsync、拔出后的错误和旧
句柄隔离；QEMU 退出后，宿主用 mtools 独立核对三份实际磁盘文件。
`guitest.bin usb` 校验精确鼠标坐标、左右键、滚轮、键盘修饰键和方向键、
重复输入及拔出释放。随后验证 GUI shell 回显、退格、滚屏，以及 100 次
键盘拔插和环回绕。

测试命令通过临时 `init.mst` 注入，按键只用于输入验证；结束后恢复
`init.mst`、`sys.cfg` 与正常镜像。

PCI 访问与桥遍历对照了 [na-kernel 的 PCI 实现](https://github.com/aether-os-studio/na-kernel/blob/main/kernel/src/drivers/bus/pci.c)。
MCFG 非零起始总线的基址处理同时核对了 [Linux x86 MMCONFIG](https://github.com/torvalds/linux/blob/v6.12/arch/x86/pci/mmconfig_64.c)。
这里只在驱动接管设备时探测 BAR、启用 decode/bus master，不在普通总线枚举时
启动所有设备。

独立根发现同时对照了 [CoolPotOS 的 scan_region](https://github.com/plos-clan/CoolPotOS-x64-V/blob/479ff303f86053fdc22e94912fe34e9ffa452310/kernel/modules/driver/pcie/scan.v)。
