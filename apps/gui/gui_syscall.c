#include <gui.h>
#include <dos.h>
enum { EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX };
void gui_api(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax) {
  struct TASK *task = current_task();
  int ds_base = task->ds_base;
  int alloc_size = task->alloc_size;
  void *alloc_addr = task->alloc_addr;
  memory *current_mm = task->mm;
  task->mm = get_shell_mm(task);
  task->alloc_addr = get_shell_alloc_addr(task);
  task->alloc_size = get_shell_alloc_size(task);
  uint32_t *reg = &eax + 1; /* eax后面的地址*/
                            /*强行改写通过PUSHAD保存的值*/
  /* reg[0] : EDI,   reg[1] : ESI,   reg[2] : EBP,   reg[3] : ESP */
  /* reg[4] : EBX,   reg[5] : EDX,   reg[6] : ECX,   reg[7] : EAX */
  desktop_t *desktop = get_now_desktop();
  if (eax == 0x01) {
	if (running_mode == POWERDESKTOP) {
	  reg[EAX] = 1;
	} else {
	  reg[EAX] = 0;
	}
  } else if (eax == 0x02) {
	if (ebx == 0x01) {
	  int base = edx + ds_base + 8;
	  char *title = *(char **)(base + 4) + ds_base;
	  int xsize = *(int *)(base + 8), ysize = *(int *)(base + 12);
	  int x = *(int *)(base + 16), y = *(int *)(base + 20);
	  window_t *w = create_window(desktop, title, xsize, ysize, task);
	  w->display(w, x, y, w->sht->ctl->top - 1);
	  reg[EAX] = (uint32_t)w;
	} else if (ebx == 0x02) {
	  int base = edx + ds_base + 12;
	  window_t *w = *(window_t **)(base + 4);
	  int x = *(int *)(base + 8), y = *(int *)(base + 12);
	  int x1 = *(int *)(base + 16), y1 = *(int *)(base + 20);
	  color_t color = *(color_t *)(base + 24);
	  w->draw(w, x, y, x1, y1, color);
	} else if (ebx == 0x03) {
	  int base = edx + ds_base + 12;
	  window_t *w = *(window_t **)(base + 4);
	  char *s = *(char **)(base + 8) + ds_base;
	  int x = *(int *)(base + 12), y = *(int *)(base + 16);
	  color_t color = *(color_t *)(base + 20);
	  w->puts(w, s, x, y, color);
	}
  }
  task->mm = current_mm;
  task->alloc_addr = alloc_addr;
  task->alloc_size = alloc_size;
  return;
}