# RenderTM 终端与 SDL3 渲染器

源码来自 [xiaoyi1212/RenderTM](https://github.com/xiaoyi1212/RenderTM)，固定到
`68fddea51e0d4cfd55f7f1091005f7dd4b5fff72`。移植保留原始光照、阴影、
TAA/GI、输入解析和 C++ modules，通过 `term.bin` 显示 ANSI 真彩色与 UTF-8
半块字符。独立的 `renderhd.bin` 使用 SDL3 窗口；两个前端共用 CPU 渲染核心，
终端版不依赖 SDL。上游此版本未提供许可证文件。

## 体素光照展厅

默认地形是南北（Z 轴两端）开放的房间，中央走道贯通。暖白墙面和浅色地砖
承接红、青墙板及彩色物品的间接光；前部天窗与后部有顶展区形成明暗对照。
沙发、靠垫、桌椅、陶器、书架、阶梯雕塑和盆栽均由体素组成。两个前端共用
此场景，相机、天空、光照与输入逻辑保持不变；使用 G / O 比较 GI / AO。

地形占用以三维 `block_index` 为准，允许屋顶、桌面和书架下方留空。完成全部
体素放置后再计算顶点遮蔽，网格剔面与光线查询共用同一占用数据。

## 构建与运行

```sh
make -C apps/rendertm ARCH=x86_64
make -C apps/rendertm ARCH=i386
make -C apps/renderhd ARCH=x86_64
make -C apps/renderhd ARCH=i386
```

构建需要支持 C++26 modules 的 Clang（已验证 21.1.8），使用对应架构的原生
libc++ 头文件和 `libcpp.so`。模块依赖从源码 import 声明生成，PCM、对象和
依赖文件分架构保存在 `apps/out/` 下；编译器与参数指纹控制重编。应用仍经统一
`application` 宏生成 PIE、链接 `/lib/ld.so`，由 `applications.list` 收入 LiveCD。
i386 磁盘镜像也显式收录该应用。

在 GUI 的终端内执行 `rendertm.bin`；也可以在 GUI 服务已启动时执行
`term.bin rendertm.bin`，打开专用终端。默认 80×25 字符终端生成 80×48 渲染像素，
首行为状态栏，余下每个字符显示上下两个像素。状态栏按终端宽度截断。

| 输入 | 行为 |
| --- | --- |
| WASD / 方向键 | 移动 |
| R / F | 上升 / 下降 |
| 鼠标位置 | 偏离画面中心时转向，中心区域为死区 |
| P | 暂停太阳、月亮运动 |
| G / O | 切换全局光照 / 环境遮蔽 |
| Q | 退出并恢复原终端屏幕和光标 |

## 平台适配

### SDL3 窗口版

GUI 启动后运行 `renderhd.bin`。默认客户区 **480×300**，小于 term 的 640×400
客户区；逐像素渲染，不把终端版的字符画面放大。WASD/方向键、R/F、鼠标位置
转向、P、G、O、Q 与终端版对应，另外支持 Escape 退出。保留阴影、AO、GI、TAA
及其历史缓冲；相机和开关状态显示在窗口标题中。

```sh
renderhd.bin --width 480 --height 300 --workers 3
renderhd.bin --width 320 --height 200 --workers 0
renderhd.bin --frames 30
```

`--width`/`--height` 调整实际渲染尺寸，窗口受当前桌面可用尺寸约束；不改变系统
显示模式。`--workers` 指渲染线程之外的工作线程数，0 为串行。x86_64 默认使用
在线 CPU 数减一，i386 默认 0，因为目前同一地址空间的线程只能在一个 CPU 上
执行。`--frames` 暂停太阳/月亮轨道，渲染指定帧数后退出并报告累计渲染耗时。
标题中的帧率由渲染耗时计算，不包括呈现和事件处理时间。

SDL 事件循环与渲染线程独立，通过共享控制邮箱传递输入。阴影采样、阴影过滤、
直接光照合并、GI 和三阶段后处理使用常驻工作线程按行分配任务；阶段之间同步，
保证邻域过滤与 TAA 历史读取一致。终端版默认仍串行执行同一套核心。
编译使用 `-O3`，不启用 fast-math，也不改变架构浮点 ABI。

原生 ARGB/XRGB 窗口 surface 直接作为输出，遵循实际 pitch；其他布局通过 SDL
surface 转换。主线程收到完整帧后同步提交窗口，应答完成后才允许下一帧覆盖
共享缓冲。退出先停止并等待渲染线程，再释放窗口；工作线程创建失败会回收
已创建的线程。两个前端的应用清单及 i386 磁盘镜像均包含对应 bin。

### 终端版

`platform.hpp` 将原生 `getch()` 的方向键转成上游解析器接受的 ANSI 字节，字符
保持原样。空闲输入通过运行库的阻塞 sleep 等待，保留原版 8 ms 输入采样周期；
不使用宿主 termios、fcntl 或 poll。终端尺寸取自当前 TTY，SIGINT/SIGTERM 通过
原生信号接口请求退出。两个架构均输出 UTF-8，i386 渲染仍只使用 x87。

渲染线程与输出线程继续通过最新帧队列和 condition_variable 协作。原生输出
使用 `print()` 的批量 fartty RPC，复用格式化缓冲，避免 `write(1)` 逐字节 RPC；
同步输出无需原来的非阻塞 fd 状态和待写缓冲。状态栏使用原生 `snprintf`，
三角函数使用标准 sin/cos，避免依赖宿主运行库扩展。

鼠标坐标由 `term.bin` 通过 `TTY_RPC_POINTER` 提供，`tty_get_pointer()` 返回当前
TTY 内从 1 开始的字符列、行；未收到窗口内指针事件、非远程终端或无效用户地址
返回 -1。它复用既有服务身份校验和 RPC 等待路径，不改变 GUI 输入 owner。
预编译 os-terminal 没有鼠标运动报告接口，因此这里适配平台坐标查询，不增加
ANSI 解析器或 Linux ABI。传统 TTY 不具备该指针接口，也不能替代 UTF-8 真彩色
GUI 终端的显示能力。

## 验证

```sh
python3 scripts/test-x86_64.py --rendertm --memory 2048 --timeout 300
python3 scripts/test-x86_64.py --arch i386 --rendertm --memory 512 --timeout 300
python3 scripts/test-x86_64.py --renderhd --memory 1024 --timeout 300
python3 scripts/test-x86_64.py --arch i386 --renderhd --memory 512 --timeout 300
```

回归先运行 `rpctest.bin`，覆盖 TTY 数据传输、指针查询、空地址拒绝和等待路径；
再通过 `guitest.bin` 在真实 `term.bin` 中启动 RenderTM。QMP 只发送交互事件，
验证键盘移动、鼠标转向、暂停、AO/GI 状态、两帧终端像素及退出后的光标恢复。
`--test` 仅开启串口诊断，普通启动不输出逐帧或输入日志。上游 Catch2 单元测试
保留在源码目录，本次原生回归不构建 Catch2。

SDL3 回归使用默认 480×300 客户区，核对串行与工作线程的三个场景逐像素一致、
输出行尾填充不被覆盖，以及六帧实际屏幕像素与提交缓冲的完整 RGB 校验和。
实际键鼠事件覆盖移动、转向、AO/GI 和暂停。i386 回归显式创建两个工作线程验证
同步正确性，不将它当作多核加速。共享核心修改后也运行终端版回归。

性能比较须固定宿主、CPU、QEMU 和分辨率；`RENDERHD VERIFY` 日志分别记录同一
场景串行与并行耗时，首帧包含地形生成，不宜当作稳定帧耗时。QEMU TCG 的帧率
不能直接代表实机性能。
