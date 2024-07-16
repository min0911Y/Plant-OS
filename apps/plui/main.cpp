#include <config.h>
#include <cpp.hpp>
#include <define.h>
#include <pl2d.hpp>
#include <plds.hpp>
#include <plui.hpp>
#include <type.hpp>

extern "C" {
void abort();
void next_event();
void screen_flush();
}

extern "C" void __cxa_pure_virtual()
{
    // Do nothing or print an error message.
}

void *operator new(size_t size)
{
    void *p = malloc(size);
    return p;
}
 
void *operator new[](size_t size)
{
    void *p = malloc(size);
    return p;
}
 
void operator delete(void *p,unsigned int size)
{
    free(p);
}
 
void operator delete[](void *p,unsigned int size)
{
    free(p);
}
void operator delete(void *p)
{
    free(p);
}
 
void operator delete[](void *p)
{
    free(p);
}

#include <pl2d/fb.h>

namespace plds {

pl2d::FrameBuffer screen_fb;
pl2d::TextureB    screen_tex;

static auto &fb  = screen_fb;
static auto &tex = screen_tex;

auto init(void *buffer, u32 width, u32 height, pl2d::PixFmt fmt) -> int {
  return on::screen_resize(buffer, width, height, fmt);
}

int nframe = 0;

void flush() {
  nframe++;

  float        i = (f32)nframe * .01f;
  pl2d::PixelF p = {.8, cpp::cos(i) * .1f, cpp::sin(i) * .1f, 1};
  p.LAB2RGB();
  tex.fill(p);
  tex.transform([](pl2d::PixelB &pix, i32 x, i32 y) {
    if ((x + y) / 25 % 2 == 0) pix.mix_ratio(0xffffffff, 64);
  });
  fb.flush(tex);
  screen_flush();
}

void deinit() {
  tex = {};
}

} // namespace plds

namespace plds::on {

// 屏幕大小重设
auto screen_resize(void *buffer, u32 width, u32 height, pl2d::PixFmt fmt) -> int {
  if (fb.ready) fb.reset();
  fb.pix[0] = buffer;
  fb.pixfmt = fmt;
  fb.width  = width;
  fb.height = height;
  fb.init();
  if (!fb.ready) return 1; // 初始化失败

  fb.init_texture(tex);
  if (!tex.ready()) return 1; // 初始化失败

  return 0;
}
// 鼠标移动
void mouse_move(i32 x, i32 y) {}
// 鼠标按键按下
void button_down(i32 button, i32 x, i32 y) {}
// 鼠标按键释放
void button_up(i32 button, i32 x, i32 y) {}
// 键盘按键按下
void key_down(i32 key) {}
// 键盘按键释放
void key_up(i32 key) {
  if (key == event::Escape) program_exit();
}
// 鼠标滚轮，nrows 表示内容可见区域向下 N 行
void scroll(i32 nrows) {}

} // namespace plds::on
