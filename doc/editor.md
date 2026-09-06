# pl_editor

`editor.bin` 使用 [plos-clan/pl_editor](https://github.com/plos-clan/pl_editor)
的 C 编辑器核心，替换原有 `apps/editor/editor.cpp`。上游版本和许可证位于
`apps/third_party/pl_editor/UPSTREAM.md` 和 `LICENSE`；Plant OS 平台接口和入口
位于 `apps/editor/`。

## 使用

在 GUI 的 term 内运行：

```text
editor file.c
editor
```

也可以用 `term.bin editor.bin file.c` 创建一个独立终端窗口。编辑器使用 ANSI
备用屏幕，退出后恢复原画面、颜色和光标；绘制仍由 term 显式刷新，auto-flush
保持关闭。编辑器不占用 GUI 的键鼠设备，也不直接操作 framebuffer。

| 按键 | 操作 |
| --- | --- |
| Ctrl-S | 保存；未命名文件会提示输入文件名 |
| Ctrl-Q | 退出；有未保存修改时显示确认提示 |
| Ctrl-Z / Ctrl-Y | 撤销 / 重做 |
| Ctrl-F | 搜索 |
| Ctrl-N / Ctrl-P | 搜索下一项 / 上一项 |
| Escape | 退出搜索或取消文件名输入 |
| Ctrl-R | 显示或隐藏行号 |
| 方向键、Home、End、Page Up/Down | 移动光标 |
| Backspace / Delete | 删除前一个 / 当前字符 |

文件操作统一通过 libp/VFS，目录和读取错误会报错，文件不存在时才打开为空文档。
保存会检查写入、同步和关闭结果，失败时保留未保存状态，另存为失败可以重新输入
文件名。编辑器继承上游按字节
定位、逐行保存并添加 LF 的文本模型，尚不提供 Unicode 字符宽度和字素导航。

## 构建与接口

```sh
make -C apps/editor ARCH=i386
make -C apps/editor ARCH=x86_64
```

两种架构使用同一份 C 源码，生成依赖 `libp.so` 的原生 PIE，不依赖宿主 termios、
ioctl 或 libc。LiveCD 应用清单和 i386 两个磁盘镜像均收录 `editor.bin`。

`getch()` 的公共键值在 `apps/include/key_input.h` 单源定义。普通字符和 Ctrl
组合返回字符/控制字符；Escape 为 ASCII 27，导航键使用对应的负键值。
PS/2、USB 和 fartty 共用 `input_device.c` 的转换，编辑器平台层只映射逻辑键值。
原有 `pwsh` 的 Escape 调用方已同步。

## 验证

```sh
python3 scripts/test-x86_64.py --editor --firmware bios --memory 1024
python3 scripts/test-x86_64.py --editor --firmware uefi --memory 1024
python3 scripts/test-x86_64.py --arch i386 --editor --memory 512
```

测试由临时 `init.mst` 启动 `guitest.bin editor`，在 term 中通过 QMP 发送实际编辑
按键，覆盖修改、Delete、撤销/重做、搜索、行号切换、保存、另存为、重新打开、
未保存退出确认和新文件创建。每次退出后核对文件内容并检查原终端画面的像素；
也检查拒绝打开目录和空文档导航。键盘公共接口变化还应运行普通双架构回归及
`--console`，结束后恢复启动脚本和正常镜像。

便携核心的宿主内存检查使用隔离的测试平台，不链接 Plant OS 的平台实现：

```sh
gcc -std=gnu17 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iapps/third_party/pl_editor apps/editor/tests/core.c \
  apps/third_party/pl_editor/pleditor.c apps/third_party/pl_editor/syntax.c \
  -o /tmp/pl-editor-core-test
/tmp/pl-editor-core-test
```

该测试覆盖高亮频繁变化时的输出缓冲增长、窄视口、保存内容、换行撤销/重做和
空文档导航/搜索。
