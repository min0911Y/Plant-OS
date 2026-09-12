# ACPI 关机

[子系统边界](subsystems.md)

在 psh 中执行 `shutdown`，或通过 `psh.bin -c shutdown` 请求关机。命令不接受参数；失败输出错误并返回状态 1。用户程序包含 `<power.h>` 后调用 `power_off()`；对应两种架构共享的 syscall `0x6a`，成功不返回，失败返回 -1。当前系统没有用户权限分级，该接口与其他设备管理接口一样可由普通应用调用。

PC 平台通过 FADT 的 `X_Dsdt`（非零时优先）或 `Dsdt` 定位 DSDT，验证签名、长度和校验和后读取静态 `_S5` 包。DSDT 不属于 RSDT/XSDT 的直接子表。AML 读取器沿声明边界解析，校验包长和整数编码；方法、字段、Buffer 和字符串内容会被跳过，Scope/Device 等声明包只扫描其声明区。遇到无法解析的声明或无效睡眠类型时返回失败，不猜测关机端口或 SLP_TYP。

PM1a/PM1b 控制寄存器采用 FADT 的扩展 GAS 或传统 I/O 地址；目前支持 16 位 System I/O 和对齐的 System Memory 寄存器。仅在关机时按需通过 SMI_CMD 启用 ACPI，等待 SCI_EN 有单调时钟超时并阻塞让出 CPU。请求 S5 前同步已注册块设备的缓存，任何同步失败均中止关机。写寄存器保留其他控制位，先设置 SLP_TYP，再设置 SLP_EN；未断电时返回失败。

S5 类型、控制寄存器和 ACPI 启用参数在平台早期初始化时解析并保存，关机路径不再读取 FADT/DSDT。i386 DOSLDR 启动没有固件内存保留区信息，且用户地址空间不保证固件物理地址的恒等映射；不能把启动期可访问的固件表指针留到用户系统调用时解析。

这是静态 ACPI S5 支持，不包含完整 AML 解释器：不执行 `_PTS`/`_GTS`，不求值动态 `_S5`，不加载 SSDT 或建立完整命名空间，不支持硬件精简型 ACPI 的 Sleep Control 协议。此类固件可能无法关机，不能据此声称覆盖所有实体 PC。关机不会替其他应用执行退出回调或刷新其用户态 stdio 缓冲；需要保存的数据应先由应用提交。

## 验证

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  scripts/tests/acpi-aml.c -o /tmp/acpi-aml
ASAN_OPTIONS=detect_leaks=0 /tmp/acpi-aml
python3 scripts/test-x86_64.py --shutdown --firmware bios --machine pc
python3 scripts/test-x86_64.py --shutdown --firmware uefi --machine q35
python3 scripts/test-x86_64.py --arch i386 --shutdown --memory 512
python3 scripts/test-x86_64.py --arch i386 --boot disk --shutdown --memory 4096
```

宿主测试覆盖包长、整数、逐字节截断、睡眠类型范围，以及方法/缓冲区中的伪 `_S5`。QEMU 测试通过临时 `init.mst` 执行真实用户态命令，并同时确认串口 S5 标记和 QMP `shutdown` 状态；仅打印关机消息或 CPU 停机不算成功。脚本最后恢复正常启动配置及镜像。

默认测试 LiveCD；`--boot disk` 使用与 `make img_run` 相同的 DOSLDR 双硬盘启动路径，当前用于 i386 BIOS/PC 关机回归，QEMU 磁盘写入使用临时快照。两条启动路径均须覆盖。
