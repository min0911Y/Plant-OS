// farcall
// Warning: 使用farcall执行的代码都在r0运行
// farcall 必须返回一个uint32_t 作为返回值
#include <arch/x86/control.h>
#include <dos.h>
// 跨页执行，即farcall
void signal_deal();
uint32_t call_across_page(uint32_t (*f)(void *arg), unsigned cr3, void *a) {
  current_task()->signal_disable = 1;
  unsigned backup = current_task()->pde;
  current_task()->pde = cr3;
  x86_cr3_write(cr3);
  uint32_t ret = f(a);
  current_task()->pde = backup;
  x86_cr3_write(backup);
  current_task()->signal_disable = 0;
  return ret;
}
