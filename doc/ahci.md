# AHCI

`kernel/drivers/ahci.c` 在 i386 和 x86_64 上共用控制器、端口及命令状态。
PCI 注册表提供控制器和 BAR，PCI IRQ 层选择 MSI-X、MSI 或共享 INTx。

初始化先进行 BIOS/OS 交接和控制器复位，再为已连接的端口配置 DMA、
恢复 SATA 链路并读取 IDENTIFY。每个端口使用独立的 4 KiB 命令/FIS 区和
64 KiB 数据缓冲，单个非 NCQ 命令使用 slot 0。读、写、IDENTIFY 和 FLUSH
共用命令构造与中断完成路径；普通 I/O 不忙等，也不增加固定轮询延迟。
磁盘层按端口命名的信号量串行访问，同一控制器的不同端口拥有独立缓冲。

所有硬件等待都有单调时间 deadline：固件交接 2 秒，复位、引擎停机、
设备就绪各 1 秒，命令完成 5 秒。停机严格先清 ST、等待 CR 清零，再清
FRE、等待 FR 清零。错误会撤下对应磁盘；无法确认停机时禁用整个函数的
bus master，保留可能仍被 DMA 引用的页。不会自动重放失败的写操作。

命令完成通过 IRQ 唤醒等待者，timer 仅负责超时；等待者按 TID/generation
查找，退出或复用的任务不会被旧完成事件唤醒。DMA 始终指向端口持有的
缓冲区，成功后才向读调用者复制数据。

IDENTIFY 使用 LBA28/48 容量，只有 512-byte 逻辑扇区的 SATA 磁盘会注册为
块设备。`fsync` 会下传设备声明支持的 FLUSH CACHE/EXT；启用写缓存但不
提供刷新能力的设备会明确拒绝。ATAPI、端口倍增器和 SATA 热插拔尚未支持。

## 验证

```sh
python3 scripts/test-x86_64.py --ahci --machine q35 --firmware uefi
python3 scripts/test-x86_64.py --arch i386 --ahci --machine q35 --memory 512
python3 scripts/test-x86_64.py --ahci --ahci-no-irq --machine q35
```

正常测试使用两个临时 FAT SATA 盘，第二个是 160 GiB 的稀疏镜像，实际仅
占用测试文件与元数据的空间。测试读取宿主预置文件、写入新文件、在偏移
513 覆盖四字节、执行 flush 并读回；QEMU 退出后再以 mtools 核对实际介质。

故障测试通过 QEMU 的硬件断点在 IDENTIFY 前暂停，使用 qtest 清除 GHC.IE。
控制器仍可完成 DMA，但内核必须通过超时报告失败、停止端口并继续启动，
不能把读取 CI 清零当作收到完成中断。所有系统内命令由临时 `init.mst`
注入，结束后恢复启动脚本和正常镜像。
