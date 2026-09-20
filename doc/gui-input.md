# GUI 鼠标与窗口尺寸

[子系统边界](subsystems.md)

## 鼠标

`window_get_event(window, &event)` 返回完整 `gui_event_t`，成功取出返回 1，
队列为空返回 0，参数错误返回负值。`GUI_EVENT_POINTER` 包含窗口坐标、设备
双轴位移、五键位图、带符号的滚轮步数和相对模式标志；关闭请求为
`GUI_EVENT_CLOSE_WINDOW`。事件按记录发布，不能分次读取其字段。

客户区按键按下后自动捕获至所有按键释放，拖出窗口后仍向原窗口发送移动和
释放事件。标题栏移动与尺寸拖拽使用同一个手势状态，不向客户区泄漏装饰操作。

`window_control(window, GUI_WINDOW_MOUSE_MODE, mode, 0)` 设置显式模式：

- `GUI_MOUSE_CAPTURE`：在窗口外继续接收鼠标事件。
- `GUI_MOUSE_RELATIVE`：隐藏并固定桌面指针，持续传递未经屏幕边缘裁剪的设备位移。
- `GUI_MOUSE_HIDDEN`：隐藏指针。
- `GUI_MOUSE_CONFINED`：将指针限制在客户区。
- `GUI_MOUSE_NORMAL`：取消显式模式；其他标志可组合。

只有可见且聚焦的窗口可获取捕获。失焦、隐藏、关闭及 owner 退出均释放捕获、
结束手势并恢复指针。共享状态的 `buttons` 是最近投递给窗口的按钮状态，
客户端在排空队列后用它校正队列溢出造成的漏释放；相对位移队列仍有容量上限。
GLFW 支持 NORMAL、HIDDEN、DISABLED、CAPTURED；SDL 支持自动/显式捕获、
相对模式和鼠标限制。相对模式不等同于 raw motion 能力声明。

`GUI_WINDOW_WARP_POINTER` 接受包含装饰的窗口坐标，仅允许聚焦且可见的
窗口在非相对模式下调用。它复用鼠标事件分派，保留按键状态、裁剪与限制，
同步更新真实光标和共享状态，不在 GLFW 内伪造位置。

`window_set_icon` 接收 `GUI_ICON_SIZE`（16）平方个 ARGB8888 像素，NULL 清除。
图标通过带 owner 校验的 RPC 复制到服务端，透明度与标题背景合成；修改同步
更新两块绘图平面的装饰，缩放保留图标，关闭释放存储。

## 游戏鼠标视角测试

在 x86_64 GUI 的终端执行 `glfwtest.bin --capture`。鼠标进入客户区自动隐藏并
锁定，通过相对位移转动网格场景，中心十字保持不动。Esc 释放并恢复光标；
左键点击客户区，或离开后重新进入，再次捕获；Q 或关闭按钮退出。失焦时释放。

此行为由游戏主动调用 `glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED)`
控制，菜单或暂停时切回 `GLFW_CURSOR_NORMAL`。SDL 对应
`SDL_SetWindowRelativeMouseMode(window, true/false)`。普通桌面窗口不自动锁定。
LWJGL 使用同一个原生 GLFW 接口；本测试验证游戏鼠标输入路径，不代表 Minecraft
整体运行支持已经完成。

`--glfw` 回归也运行 `glfwtest.bin --capture-test`：真实输入累计 1400、−900 的
双轴位移，核对转动前后的 OpenGL 像素与屏幕一致（无鼠标光标覆盖），并验证
Esc 后仅原锁定位置出现光标。

## 尺寸与缓冲生命周期

`GUI_CREATE_RESIZABLE` 和 `GUI_WINDOW_SET_RESIZABLE` 控制用户拖拽调整尺寸。
拖动右边框、下边框或右下角，服务端在一致快照中发布
`requested_width/requested_height` 并唤醒订阅者；`width/height` 仍是已提交尺寸。
客户端在安全的绘图边界调用 `window_resize` 接受请求，也可主动调用它设置尺寸。
SDL/GLFW 在事件泵中完成这个过程，并发出尺寸与重绘通知。

尺寸包含装饰，最小为 40×29，受有符号 16 位坐标及共享映射区域容量限制。
调整尺寸需要同时容纳旧、新映射；分配失败时原窗口、尺寸和绘图平面保持有效。
RPC 传输失败使缓冲所有权不确定时，客户端停止暴露绘图平面，只能关闭窗口。

服务端先分配独立双平面、绘制装饰、复制旧客户区的交集及事件队列，再映射给
经过 TID/generation 校验的 owner，最后原子替换图层和状态。客户端收到应答后
解除旧映射。缩小时重新合成旧窗口暴露区域；新区域使用窗口背景色。
成功调整尺寸后，所有旧 framebuffer/surface 指针失效，必须重新获取。
创建、绘制、呈现、事件读取和尺寸变更由窗口 owner 串行执行；通知线程只接收唤醒。

EGL 在原生描述符更新后通过 `eglWaitNative` 同步附件，在重新绑定或交换时也检查
尺寸，重建颜色和深度缓冲并使 GL framebuffer 状态失效。Vulkan surface 按原客户区
边距计算新 extent；旧 swapchain 呈现返回 `VK_ERROR_OUT_OF_DATE_KHR`，由应用重建。

`lite.bin` 默认允许边框拖拽并重新布局。固定布局的原生应用（包括目前没有终端重排接口的 `term.bin`）不设置可调整标志。
修改窗口尺寸不会拉伸像素；应用根据新客户区重新布局和绘制。

## 验证

```sh
python3 scripts/test-x86_64.py --mouse --accel kvm --cpu host --memory 3072
python3 scripts/test-x86_64.py --arch i386 --mouse --accel kvm --cpu host --memory 512
python3 scripts/test-x86_64.py --sdl --accel kvm --cpu host --memory 3072
python3 scripts/test-x86_64.py --glfw --accel kvm --cpu host --memory 3072
python3 scripts/test-x86_64.py --lavapipe --accel kvm --cpu host --memory 3072
python3 scripts/test-x86_64.py --desktop-app lite --accel kvm --cpu host --memory 3072
```

`--mouse` 使用真实 QMP 输入验证逐像素标题拖动、双键客户区捕获和窗外释放、
超过屏幕范围的相对移动、焦点/隐藏释放、右下角拖拽，以及扩大、缩小、失败回滚
与双平面交换；宿主逐像素核对扩大后的整个客户区。SDL 回归覆盖 surface 重建、
相对模式和捕获；GLFW 回归覆盖原生尺寸、EGL 新增区域像素与正常输入。Vulkan
回归在扩大、缩小后重新呈现并读回校验；Lite 回归拖拽缩小、放大后编辑和保存。
QMP 不注入系统命令。
