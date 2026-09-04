# 内核 CPU 火焰图

Plant OS 的诊断构建可以在时钟中断中对所有 CPU 的执行位置采样，并沿
x86 frame pointer 展开内核调用栈。采样器在 IRQ 路径中不分配内存，也不
输出串口日志；相同的调用栈会直接合并计数，因此采样时长不受原始样本缓冲
区大小限制。

## 构建与采集

先按正常顺序构建应用、加载器和带采样器的内核：

```sh
make -C apps
make -C loader
make -C kernel PERF=1
```

`PERF=1` 会为全部内核 C/C++ 代码保留 frame pointer 并关闭 sibling-call
优化。构建配置会被记录在 `kernel/obj/.build-config`；在普通构建和诊断构建
间切换时，make 会自动重编受影响的内核对象，不需要手工清理整个仓库。
固定表默认保留 4096 个哈希槽，并在 75% 负载时停止接收新的调用栈，以维持
IRQ 查询性能；受内核链接布局限制需要调整时，可传入例如
`PERF_STACKS=2048`，其值必须是至少为 4 的 2 次幂。

通过串口保存一次 QEMU 运行，例如：

```sh
make -C kernel img_run PERF=1 2>&1 | tee /tmp/plant-perf.log
```

没有 KVM 时，按仓库运行指南使用同一镜像手工启动 QEMU，并去掉
`-enable-kvm -cpu host`。采集必须包含完整的 `PERF_BEGIN` 到 `PERF_END`；
不要在 `PERF_END` 出现前终止 QEMU。

诊断内核会在启动早期自动开始采样，并保持运行，直到 psh 中明确执行：

```text
perf status
perf stop
```

因此 GUI、后台服务和其他用户态启动工作不会在首次进入 `psh.bin` 时被截断。
停止启动会话后，可继续分析任意工作负载：

```text
perf start
要分析的命令及操作
perf status
perf stop
```

`perf stop` 总是结束当前活动会话并将聚合结果写入串口；只有停止后才能再次
`perf start`。一次运行可以包含多段结果，转换器默认选择最后一段完整结果。

## 生成火焰图

在仓库根目录运行：

```sh
python3 scripts/kernel-perf.py \
  --kernel kernel/obj/kernel.bin \
  --serial /tmp/plant-perf.log \
  --out /tmp/plant-perf.folded
```

命令会同时生成 `/tmp/plant-perf.svg`。SVG 只依赖 Python 标准库；folded
文件也可以直接交给 Brendan Gregg 的 `flamegraph.pl` 或其他兼容工具。
`--kernel` 必须指向采样时运行的同一份 PERF ELF；转换器会校验采样记录中的
内核文本边界，拒绝明显不匹配的 ELF。在转换前重建成普通内核会令校验失败。

直接在浏览器中打开 SVG 后，可以点击任意栈帧把其子树放大到完整宽度；标签会
按放大后的矩形宽度重新排版。悬停时底部详情栏始终显示完整名称、样本数和占比，
点击 `Reset Zoom` 或按 `Esc` 恢复全图。以 `<img>` 嵌入或经过安全净化的预览器
通常会禁用 SVG 脚本，此时应直接打开文件以使用交互缩放。

常用选项：

- `--dump 0`：选择启动采样；负数从最后一段倒数。
- `--group-by cpu`、`task` 或 `cpu-task`：按 CPU/TID 分组。
- `--exclude-user`：只保留内核样本。
- `--show-offsets`：在函数名后保留指令偏移，适合定位函数内部热点。
- `--no-svg`：只生成 folded stacks。

## 结果含义与限制

矩形宽度代表定时采样命中次数，近似该调用路径消耗的 CPU 时间；它不表示
阻塞或等待时间。每个在线 CPU 都参与采样。内核使用统一符号表展开；用户程序
目前统一链接到同一虚拟地址且默认省略 frame pointer，因此用户态样本合并为
`[user]`，不会被错误地套用内核符号。若 `dropped` 非零，说明不同的
CPU/TID/调用栈组合已填满固定聚合表，应缩短采样区间后重新采集。
