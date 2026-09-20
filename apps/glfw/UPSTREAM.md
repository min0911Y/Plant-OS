# GLFW 3.4

- 上游：https://github.com/glfw/glfw
- 固定归档：https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
- SHA256：`c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01`
- 许可：zlib，完整文本见 [LICENSE.md](LICENSE.md)。

保留上游公开头文件、通用核心、EGL/OSMesa、显式 Null 平台及 POSIX
线程实现；不导入其他桌面平台的源码和宿主 CMake 构建。原生构建使用
`build.mk`，应用通过仓库统一构建图链接 `libglfw.so`。

Plant OS 后端在 `src/plantos_*.{c,h}`。平台表参考上游 `null_init.c`，
实际窗口、状态和输入来自 GUI RPC，OpenGL 使用原有 Mesa EGL；
没有 SDL、X11 或 Wayland 适配层。

上游文件的局部修改：

- `platform.h`、`platform.c`、`glfw3.h` 注册 Plant OS 平台和状态。
- `window.c` 在共享上下文校验前将 Plant OS native context 规范化为 EGL。
- `init.c`、`window.c`、`monitor.c`、`input.c` 处理初始化、窗口、字符串和
  错误记录分配失败，避免空指针访问或递归报告分配错误。
- `egl_context.c` 检查配置数组分配，并在 surface 创建前建立 context
  清理入口，保证失败回滚；与 `internal.h` 一起装载 `eglWaitNative`，
  用于尺寸变更后的原生 framebuffer 同步。

线程/TLS 使用上游 POSIX 实现和原生 pthread。计时、`/lib/libEGL.so`/`/lib/libGL.so`
的真实模块查询、独立通知线程和 futex 等待在 `plantos_runtime.c` 中实现。
功能边界与回归见 [GLFW 移植](../../doc/glfw.md)。
