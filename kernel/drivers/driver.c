/**
 * @file driver.c
 * @author zhouzhihao & min0911_
 * @note 用于使用外部的驱动程序
 */
#include <dos.h>
#include <drivers.h>

#if 0

struct driver_ctl *drv_ctl;

mtask *start_drv(char *cmdline) {
  return NULL;
}

void init_driver() {
  drv_ctl = (struct driver_ctl *)page_malloc(sizeof(struct driver_ctl));
  // printk("drv_ctl=%08x\n", drv_ctl);
  for (int i = 0; i < MAX_DRIVERS; i++) {
    drv_ctl->drivers[i].flags = DRIVER_FREE;
  }
  drv_ctl->driver_num = 0;
  return;
}

drv_t driver_malloc(char *drv_file, drv_type_t drv_type) {
  // 先为驱动程序申请个位置（drv_ctl）
  drv_t drv;
  for (int i = 0; i < MAX_DRIVERS; i++) {
    if (drv_ctl->drivers[i].flags == DRIVER_FREE) {
      drv        = &(drv_ctl->drivers[i]);
      drv->flags = DRIVER_USE;
      break;
    }
  }
  // 创建驱动程序的任务
  mtask *drv_task = start_drv(drv_file);
  if (drv_task == 0) { return 0; }
  drv->drv_task = drv_task;
  drv->drv_type = drv_type;
  drv_ctl->driver_num++;
  return drv;
}

void driver_free(drv_t driver) {
  driver->drv_task->running = 0; // 让他自生自灭（
  while (get_task(get_tid(driver->drv_task)))
    ;
  driver->flags = DRIVER_FREE;
  drv_ctl->driver_num--;
  return;
}

void driver_call(drv_t driver, int func, void *arg) {
  struct arg_struct args;
  args.func_num = func;
  args.arg      = arg;
  args.tid      = get_tid(current_task());
  send_ipc_message(get_tid(driver->drv_task), &args, sizeof(struct arg_struct), synchronous);
  while (true) {
    while (ipc_message_len(get_tid(driver->drv_task)) == -1)
      ;
    int result;
    get_ipc_message(&result, get_tid(driver->drv_task));
    if (result == 0x200) {
      break;
    } else if (result == 0x404) {
      printk("Driver call error\n");
      break;
    }
  }
  return;
}

void driver_set_handler(drv_t driver, int func_addr, int handler_num) {
  struct GATE_DESCRIPTOR *idt = (struct GATE_DESCRIPTOR *)ADR_IDT;
  set_gatedesc(idt + handler_num, (int)func_addr, driver->drv_task->sel, AR_INTGATE32);
}

drv_t driver_find(drv_type_t type) {
  for (int i = 0; i < MAX_DRIVERS; i++) {
    if (drv_ctl->drivers[i].drv_type == type) { return &drv_ctl->drivers[i]; }
  }
  return NULL;
}

#endif
