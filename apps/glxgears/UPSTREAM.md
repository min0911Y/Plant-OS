# glxgears

来自 Brian Paul 的经典 `glxgears`，Mesa demos 9.0.0：

- 源码：https://archive.mesa3d.org/demos/mesa-demos-9.0.0.tar.xz
- 归档 SHA256：`3046a3d26a7b051af7ebdd257a5f23bfeb160cad6ed952329cdff1e9f1ed496b`
- 原文件：`src/xdemos/glxgears.c`，MIT 许可，版权声明保留在源码中。

保留原始齿轮几何、三个齿轮的变换、固定功能光照和显示列表。
X11/GLX 窗口与事件循环替换为 SDL3 和原生 EGL；保持经典单目视图，
使用单调时间计时，提供方向键旋转、空格暂停、Escape 退出和 FPS 输出。
这是原生窗口移植，不提供 GLX 或 X11 API。

`--test` 先验证 EGL 离屏像素、GLSL、上下文共享与生命周期，再在两次
窗口提交时输出读回校验值，由宿主校验实际窗口像素并投递方向键和 Escape。
运行入口与支持范围见 [OpenGL](../../doc/opengl.md)。
